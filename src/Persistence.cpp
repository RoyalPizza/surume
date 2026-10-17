#include "Persistence.h"
#include "Platform.h"
#include "Log.h"
#include <cstdio>
#include <string>

extern "C" {
#include "tomlc17.h"
}

static std::string EscapeToml(const std::string& s) {
    std::string r;
    for (char c : s) {
        if      (c == '\\') r += "\\\\";
        else if (c == '"')  r += "\\\"";
        else                r += c;
    }
    return r;
}

void Persistence::Load(AppState& state) {
    std::string dir = Platform::GetAppDataDir();
    if (dir.empty()) {
        LOG_WARN("Persistence::Load: no app data dir");
        return;
    }
    LOG_INFO("Persistence::Load from '%s'", dir.c_str());

    // --- repositories.toml ---
    {
        std::string path = dir + "repositories.toml";
        toml_result_t r  = toml_parse_file_ex(path.c_str());
        if (r.ok) {
            toml_datum_t active = toml_get(r.toptab, "active");
            if (active.type == TOML_STRING) {
                state.activeRepo = active.u.s;
                LOG_INFO("Persistence: loaded activeRepo='%s'", state.activeRepo.c_str());
            }

            toml_datum_t repos = toml_get(r.toptab, "repos");
            if (repos.type == TOML_ARRAY) {
                for (int i = 0; i < repos.u.arr.size; ++i) {
                    if (repos.u.arr.elem[i].type == TOML_STRING)
                        state.recentRepos.push_back(repos.u.arr.elem[i].u.s);
                }
                LOG_INFO("Persistence: loaded %d recent repos", (int)state.recentRepos.size());
            }
        } else {
            LOG_DEBUG("Persistence: repositories.toml not found or invalid (first run?)");
        }
        toml_free(r);
    }

    // --- settings.toml ---
    {
        std::string path = dir + "settings.toml";
        toml_result_t r  = toml_parse_file_ex(path.c_str());
        if (r.ok) {
            toml_datum_t w = toml_get(r.toptab, "left_column_width");
            if (w.type == TOML_FP64) {
                state.leftColumnWidth = (float)w.u.fp64;
                LOG_DEBUG("Persistence: leftColumnWidth=%.1f", state.leftColumnWidth);
            }

            toml_datum_t gp = toml_get(r.toptab, "git_path");
            if (gp.type == TOML_STRING) state.settings.gitPath = gp.u.s;

            toml_datum_t dod = toml_get(r.toptab, "default_open_dir");
            if (dod.type == TOML_STRING) state.settings.defaultOpenDir = dod.u.s;

            toml_datum_t af = toml_get(r.toptab, "auto_fetch_enabled");
            if (af.type == TOML_BOOLEAN) state.settings.autoFetchEnabled = af.u.boolean;

            toml_datum_t afi = toml_get(r.toptab, "auto_fetch_interval_min");
            if (afi.type == TOML_INT64) state.settings.autoFetchIntervalMin = (int)afi.u.int64;

            LOG_INFO("Persistence: settings loaded");
        } else {
            LOG_DEBUG("Persistence: settings.toml not found or invalid (first run?)");
        }
        toml_free(r);
    }
}

void Persistence::Save(const AppState& state) {
    std::string dir = Platform::GetAppDataDir();
    if (dir.empty()) {
        LOG_WARN("Persistence::Save: no app data dir");
        return;
    }
    LOG_INFO("Persistence::Save to '%s'", dir.c_str());

    // --- repositories.toml ---
    {
        std::string path = dir + "repositories.toml";
        FILE* f = nullptr;
        fopen_s(&f, path.c_str(), "w");
        if (f) {
            fprintf(f, "active = \"%s\"\n", EscapeToml(state.activeRepo).c_str());
            fprintf(f, "repos  = [\n");
            for (const auto& repo : state.recentRepos)
                fprintf(f, "  \"%s\",\n", EscapeToml(repo).c_str());
            fprintf(f, "]\n");
            fclose(f);
            LOG_DEBUG("Persistence: repositories.toml saved (%d repos)", (int)state.recentRepos.size());
        } else {
            LOG_ERROR("Persistence: failed to open repositories.toml for writing");
        }
    }

    // --- settings.toml ---
    {
        std::string path = dir + "settings.toml";
        FILE* f = nullptr;
        fopen_s(&f, path.c_str(), "w");
        if (f) {
            fprintf(f, "left_column_width       = %g\n",   state.leftColumnWidth);
            fprintf(f, "git_path                = \"%s\"\n", EscapeToml(state.settings.gitPath).c_str());
            fprintf(f, "default_open_dir        = \"%s\"\n", EscapeToml(state.settings.defaultOpenDir).c_str());
            fprintf(f, "auto_fetch_enabled      = %s\n",   state.settings.autoFetchEnabled ? "true" : "false");
            fprintf(f, "auto_fetch_interval_min = %d\n",   state.settings.autoFetchIntervalMin);
            fclose(f);
            LOG_DEBUG("Persistence: settings.toml saved");
        } else {
            LOG_ERROR("Persistence: failed to open settings.toml for writing");
        }
    }
}
