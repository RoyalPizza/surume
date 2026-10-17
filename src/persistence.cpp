#include "persistence.hpp"

#include "log.hpp"
#include "tomlc17.h"

#include <cstdio>
#include <string>

static std::string escape_toml(const std::string& text)
{
    std::string escaped;
    for (char c : text)
    {
        if (c == '\\')
        {
            escaped += "\\\\";
        }
        else if (c == '"')
        {
            escaped += "\\\"";
        }
        else
        {
            escaped += c;
        }
    }
    return escaped;
}

static void load_repositories(AppState& state, const std::string& path)
{
    toml_result_t toml = toml_parse_file_ex(path.c_str());
    if (!toml.ok)
    {
        LOG_DEBUG("persistence: repositories.toml not found or invalid (first run?)");
        toml_free(toml);
        return;
    }

    toml_datum_t active = toml_get(toml.toptab, "active");
    if (active.type == TOML_STRING)
    {
        state.active_repo = active.u.s;
        LOG_INFO("persistence: active_repo='%s'", state.active_repo.c_str());
    }

    toml_datum_t repos = toml_get(toml.toptab, "repos");
    if (repos.type == TOML_ARRAY)
    {
        for (int i = 0; i < repos.u.arr.size; ++i)
        {
            if (repos.u.arr.elem[i].type == TOML_STRING)
            {
                state.recent_repos.push_back(repos.u.arr.elem[i].u.s);
            }
        }
        LOG_INFO("persistence: loaded %d recent repos", (int)state.recent_repos.size());
    }

    toml_free(toml);
}

static void load_settings(AppState& state, const std::string& path)
{
    toml_result_t toml = toml_parse_file_ex(path.c_str());
    if (!toml.ok)
    {
        LOG_DEBUG("persistence: settings.toml not found or invalid (first run?)");
        toml_free(toml);
        return;
    }

    toml_datum_t width = toml_get(toml.toptab, "left_column_width");
    if (width.type == TOML_FP64)
    {
        state.left_column_width = (float)width.u.fp64;
    }

    toml_datum_t git_path = toml_get(toml.toptab, "git_path");
    if (git_path.type == TOML_STRING)
    {
        state.settings.git_path = git_path.u.s;
    }

    toml_datum_t open_directory = toml_get(toml.toptab, "default_open_dir");
    if (open_directory.type == TOML_STRING)
    {
        state.settings.default_open_directory = open_directory.u.s;
    }

    toml_datum_t auto_fetch = toml_get(toml.toptab, "auto_fetch_enabled");
    if (auto_fetch.type == TOML_BOOLEAN)
    {
        state.settings.is_auto_fetch_enabled = auto_fetch.u.boolean;
    }

    toml_datum_t interval = toml_get(toml.toptab, "auto_fetch_interval_min");
    if (interval.type == TOML_INT64)
    {
        state.settings.auto_fetch_interval_minutes = (int)interval.u.int64;
    }

    LOG_INFO("persistence: settings loaded");
    toml_free(toml);
}

static bool save_repositories(const AppState& state, const std::string& path)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "w") != 0 || file == nullptr)
    {
        LOG_ERROR("persistence: failed to open repositories.toml for writing");
        return false;
    }

    fprintf(file, "active = \"%s\"\n", escape_toml(state.active_repo).c_str());
    fprintf(file, "repos  = [\n");
    for (const std::string& repo : state.recent_repos)
    {
        fprintf(file, "  \"%s\",\n", escape_toml(repo).c_str());
    }
    fprintf(file, "]\n");
    fclose(file);
    return true;
}

static bool save_settings(const AppState& state, const std::string& path)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "w") != 0 || file == nullptr)
    {
        LOG_ERROR("persistence: failed to open settings.toml for writing");
        return false;
    }

    const AppSettings& settings = state.settings;
    fprintf(file, "left_column_width       = %g\n", state.left_column_width);
    fprintf(file, "git_path                = \"%s\"\n", escape_toml(settings.git_path).c_str());
    fprintf(file, "default_open_dir        = \"%s\"\n",
        escape_toml(settings.default_open_directory).c_str());
    fprintf(file, "auto_fetch_enabled      = %s\n",
        settings.is_auto_fetch_enabled ? "true" : "false");
    fprintf(file, "auto_fetch_interval_min = %d\n", settings.auto_fetch_interval_minutes);
    fclose(file);
    return true;
}

void persistence_load(AppState& state, const std::string& dir)
{
    if (dir.empty())
    {
        LOG_WARN("persistence_load: no app data dir");
        return;
    }

    LOG_INFO("persistence_load from '%s'", dir.c_str());
    load_repositories(state, dir + "repositories.toml");
    load_settings(state, dir + "settings.toml");
}

bool persistence_save(const AppState& state, const std::string& dir)
{
    if (dir.empty())
    {
        LOG_WARN("persistence_save: no app data dir");
        return false;
    }

    LOG_INFO("persistence_save to '%s'", dir.c_str());
    bool is_repositories_saved = save_repositories(state, dir + "repositories.toml");
    bool is_settings_saved = save_settings(state, dir + "settings.toml");
    return is_repositories_saved && is_settings_saved;
}
