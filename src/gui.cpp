#include "gui.hpp"

#include "log.hpp"
#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_MixedValue

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Layout
constexpr float HEADER_HEIGHT = 40.0f;
constexpr float HEADER_PADDING_X = 8.0f;
constexpr float HEADER_BUTTON_SPACING = 4.0f;
constexpr float SYNC_BUTTON_EXTRA_WIDTH = 16.0f;
constexpr float SPLITTER_WIDTH = 4.0f;
constexpr float MIN_COLUMN_WIDTH = 160.0f;
constexpr float MAX_COLUMN_WIDTH = 600.0f;
constexpr float COMMIT_FOOTER_HEIGHT = 148.0f;
constexpr float COMMIT_FOOTER_PADDING_X = 8.0f;
constexpr float COMMIT_FOOTER_PADDING_Y = 6.0f;
constexpr float COMMIT_DESCRIPTION_HEIGHT = 52.0f;
constexpr float EMPTY_STATE_BUTTON_WIDTH = 200.0f;
constexpr float EMPTY_HINT_Y_FRACTION = 0.45f;
constexpr float STATUS_MESSAGE_MARGIN_X = 8.0f;
constexpr float STATUS_MESSAGE_MARGIN_BOTTOM = 4.0f;
constexpr float COMMIT_INFO_FRACTION = 0.35f; // share of the History pane for commit info
constexpr size_t LARGE_DIFF_BYTES = 4 * 1024 * 1024; // placeholder above this
constexpr float BYTES_PER_MEGABYTE = 1024.0f * 1024.0f;
constexpr float DIFF_BACKGROUND_OVERSCAN = 8000.0f; // reaches past horizontal scroll

// Modals
constexpr float DIALOG_BUTTON_WIDTH = 100.0f;
constexpr float ABOUT_WIDTH = 320.0f;
constexpr float ABOUT_BUTTON_WIDTH = 80.0f;
constexpr float SETTINGS_WIDTH = 480.0f;
constexpr float SETTINGS_LABEL_WIDTH = 160.0f;
constexpr float AUTO_FETCH_INPUT_WIDTH = 60.0f;
constexpr int AUTO_FETCH_MIN_MINUTES = 1;
constexpr int AUTO_FETCH_MAX_MINUTES = 120;
constexpr int AUTO_FETCH_STEP = 1;
constexpr int AUTO_FETCH_FAST_STEP = 5;
constexpr float BRANCH_MODAL_WIDTH = 380.0f;
constexpr float CREATE_BRANCH_BUTTON_WIDTH = 150.0f;
constexpr float DELETE_LIST_HEIGHT = 120.0f;
constexpr float DELETE_LIST_MAX_HEIGHT = 150.0f;

// Text buffers
constexpr int PICKER_LABEL_SIZE = 160;
constexpr int COMMIT_LABEL_SIZE = 128;
constexpr int BRANCH_NAME_SIZE = 128;
constexpr int SETTINGS_PATH_SIZE = 512;

// Colors: hardcoded only for semantic Git states
constexpr ImVec4 COLOR_CONFLICTED = ImVec4(1.0f, 0.35f, 0.2f, 1.0f);
constexpr ImVec4 COLOR_UNTRACKED = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
constexpr ImVec4 COLOR_DELETED = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
constexpr ImVec4 COLOR_RENAMED = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
constexpr ImVec4 COLOR_ADDED = ImVec4(0.3f, 0.85f, 0.3f, 1.0f);
constexpr ImVec4 COLOR_MODIFIED = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
constexpr ImVec4 COLOR_DIFF_ADDED_TEXT = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
constexpr ImVec4 COLOR_DIFF_REMOVED_TEXT = ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
constexpr ImVec4 COLOR_DIFF_HUNK_TEXT = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
constexpr ImVec4 COLOR_ERROR_TEXT = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
constexpr ImU32 COLOR_DIFF_ADDED_BACKGROUND = IM_COL32(30, 70, 30, 255);
constexpr ImU32 COLOR_DIFF_REMOVED_BACKGROUND = IM_COL32(70, 25, 25, 255);

// UTF-8 arrows (Segoe UI has them)
constexpr const char* ARROW_UP = "\xE2\x86\x91";
constexpr const char* ARROW_DOWN = "\xE2\x86\x93";

constexpr ImGuiWindowFlags MAIN_WINDOW_FLAGS =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoBringToFrontOnFocus;

struct FileBadge
{
    char letter = 'M';
    ImVec4 color = COLOR_MODIFIED;
    const char* tooltip = "Modified";
};

// Single GUI instance: widget text and selections that live across frames
static float ui_scale = 1.0f; // DPI scale for this frame, from the style
static char new_branch_name[BRANCH_NAME_SIZE] = {};
static int delete_branch_selection = -1;
static char settings_git_path[SETTINGS_PATH_SIZE] = {};
static char settings_open_directory[SETTINGS_PATH_SIZE] = {};
static bool settings_is_auto_fetch_enabled = false;
static int settings_auto_fetch_minutes = 15;
static bool is_settings_loaded = false;
static std::string pending_discard_path; // empty means every change
static bool is_discard_modal_requested = false;
static NavMode shown_nav_mode = NavMode::CHANGES;
static bool should_scroll_to_selection = false; // set by arrow keys; the row scrolls into view
static std::vector<int> visible_files;          // filtered file indices, rebuilt every frame
static bool is_first_frame = true;

// Pixel sizes above are for 96 DPI; scale them for the current monitor.
static float scaled(float pixels)
{
    return pixels * ui_scale;
}

// Opening repositories needs git. Allowed until the startup check says it's missing.
static bool can_open_repos(const AppState& state)
{
    return state.is_git_found || !state.is_git_checked;
}

static void request(AppState& state, RequestKind kind)
{
    Request request;
    request.kind = kind;
    state.requests.push_back(request);
}

static void request_text(AppState& state, RequestKind kind, const std::string& text)
{
    Request request;
    request.kind = kind;
    request.text = text;
    state.requests.push_back(request);
}

// Ask for confirmation before discarding path, or every change when path is empty.
static void request_discard(const std::string& path)
{
    pending_discard_path = path;
    is_discard_modal_requested = true;
}

// -------------------------------------------------------------------------
// Modals
// -------------------------------------------------------------------------

static void render_about_modal()
{
    ImGui::SetNextWindowSize(ImVec2(scaled(ABOUT_WIDTH), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("About Surume", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::Text("Surume");
    ImGui::TextDisabled("v0.1.0 — Milestone 1");
    ImGui::Spacing();
    ImGui::TextWrapped("A native C++ desktop Git client built with Dear ImGui.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - scaled(ABOUT_BUTTON_WIDTH)) * 0.5f);
    if (ImGui::Button("OK", ImVec2(scaled(ABOUT_BUTTON_WIDTH), 0)))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void load_settings_fields(const AppSettings& settings)
{
    strncpy_s(settings_git_path, settings.git_path.c_str(), sizeof(settings_git_path) - 1);
    strncpy_s(settings_open_directory, settings.default_open_directory.c_str(),
        sizeof(settings_open_directory) - 1);
    settings_is_auto_fetch_enabled = settings.is_auto_fetch_enabled;
    settings_auto_fetch_minutes = settings.auto_fetch_interval_minutes;
    is_settings_loaded = true;
}

static void render_settings_modal(AppState& state)
{
    ImGui::SetNextWindowSize(ImVec2(scaled(SETTINGS_WIDTH), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (!is_settings_loaded)
    {
        load_settings_fields(state.settings);
    }

    float input_width = ImGui::GetContentRegionAvail().x - scaled(SETTINGS_LABEL_WIDTH) -
        ImGui::GetStyle().ItemSpacing.x;

    ImGui::Text("Git executable path");
    ImGui::SameLine(scaled(SETTINGS_LABEL_WIDTH));
    ImGui::SetNextItemWidth(input_width);
    ImGui::InputTextWithHint("##gitpath", "Leave empty to use PATH",
        settings_git_path, sizeof(settings_git_path));

    ImGui::Text("Default open directory");
    ImGui::SameLine(scaled(SETTINGS_LABEL_WIDTH));
    ImGui::SetNextItemWidth(input_width);
    ImGui::InputTextWithHint("##opendir", "Leave empty for last used",
        settings_open_directory, sizeof(settings_open_directory));

    ImGui::Separator();
    ImGui::Text("Auto-fetch");
    ImGui::SameLine(scaled(SETTINGS_LABEL_WIDTH));
    ImGui::Checkbox("##autofetch", &settings_is_auto_fetch_enabled);
    if (settings_is_auto_fetch_enabled)
    {
        ImGui::SameLine();
        ImGui::Text("every");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(AUTO_FETCH_INPUT_WIDTH));
        ImGui::InputInt("##autofetchminutes", &settings_auto_fetch_minutes,
            AUTO_FETCH_STEP, AUTO_FETCH_FAST_STEP);
        settings_auto_fetch_minutes = std::clamp(settings_auto_fetch_minutes,
            AUTO_FETCH_MIN_MINUTES, AUTO_FETCH_MAX_MINUTES);
        ImGui::SameLine();
        ImGui::Text("min");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Save", ImVec2(scaled(DIALOG_BUTTON_WIDTH), 0)))
    {
        LOG_INFO("Settings: saved");
        state.settings.git_path = settings_git_path;
        state.settings.default_open_directory = settings_open_directory;
        state.settings.is_auto_fetch_enabled = settings_is_auto_fetch_enabled;
        state.settings.auto_fetch_interval_minutes = settings_auto_fetch_minutes;
        request(state, RequestKind::SAVE_SETTINGS);
        is_settings_loaded = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(DIALOG_BUTTON_WIDTH), 0)))
    {
        is_settings_loaded = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void create_branch(AppState& state)
{
    LOG_INFO("NewBranchModal: creating branch '%s'", new_branch_name);
    request_text(state, RequestKind::CREATE_BRANCH, new_branch_name);
}

static void render_new_branch_modal(AppState& state)
{
    ImGui::SetNextWindowSize(ImVec2(scaled(BRANCH_MODAL_WIDTH), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("New Branch", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    const char* source = state.repo.head.name.empty() ? "(detached HEAD)" :
        state.repo.head.name.c_str();
    ImGui::Text("Create from: %s", source);
    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool is_enter_pressed = ImGui::InputTextWithHint("##newbranch", "Branch name",
        new_branch_name, sizeof(new_branch_name), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    bool is_valid = new_branch_name[0] != '\0';
    ImGui::BeginDisabled(!is_valid);
    ImVec2 create_size = ImVec2(scaled(CREATE_BRANCH_BUTTON_WIDTH), 0);
    bool is_create_clicked = ImGui::Button("Create Branch", create_size);
    ImGui::EndDisabled();

    if (is_valid && (is_create_clicked || is_enter_pressed))
    {
        create_branch(state);
        new_branch_name[0] = '\0';
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(DIALOG_BUTTON_WIDTH), 0)))
    {
        new_branch_name[0] = '\0';
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void delete_branch(AppState& state, const std::string& name)
{
    LOG_INFO("DeleteBranchModal: deleting '%s'", name.c_str());
    request_text(state, RequestKind::DELETE_BRANCH, name);
}

static void render_delete_branch_modal(AppState& state)
{
    ImGui::SetNextWindowSize(ImVec2(scaled(BRANCH_MODAL_WIDTH), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Delete Branch", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    // Local branches other than the current one
    std::vector<const BranchInfo*> deletable;
    for (const BranchInfo& branch : state.repo.branches)
    {
        if (!branch.is_current && branch.location != BranchLocation::REMOTE)
        {
            deletable.push_back(&branch);
        }
    }

    ImGui::Text("Select a local branch to delete:");
    ImGui::Spacing();

    if (deletable.empty())
    {
        ImGui::TextDisabled("No other local branches.");
    }
    else
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
            ImVec2(FLT_MAX, scaled(DELETE_LIST_MAX_HEIGHT)));
        if (ImGui::BeginChild("##deletelist", ImVec2(0, scaled(DELETE_LIST_HEIGHT)), true))
        {
            for (int i = 0; i < (int)deletable.size(); ++i)
            {
                if (ImGui::Selectable(deletable[i]->name.c_str(), delete_branch_selection == i))
                {
                    delete_branch_selection = i;
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Safe delete only — fails if branch has unmerged commits.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool is_valid = delete_branch_selection >= 0 &&
        delete_branch_selection < (int)deletable.size();
    ImGui::BeginDisabled(!is_valid);
    if (ImGui::Button("Delete", ImVec2(scaled(DIALOG_BUTTON_WIDTH), 0)))
    {
        delete_branch(state, deletable[delete_branch_selection]->name);
        delete_branch_selection = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(DIALOG_BUTTON_WIDTH), 0)))
    {
        delete_branch_selection = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void render_discard_modal(AppState& state)
{
    ImGui::SetNextWindowSize(ImVec2(scaled(BRANCH_MODAL_WIDTH), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Discard Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (pending_discard_path.empty())
    {
        ImGui::TextWrapped("Discard all %d changed files?", (int)state.repo.files.size());
    }
    else
    {
        ImGui::TextWrapped("Discard changes to %s?", pending_discard_path.c_str());
    }
    ImGui::TextDisabled("New files are deleted. This can't be undone.");
    ImGui::Spacing();

    if (ImGui::Button("Discard", ImVec2(scaled(DIALOG_BUTTON_WIDTH), 0)))
    {
        request_text(state, RequestKind::DISCARD, pending_discard_path);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(DIALOG_BUTTON_WIDTH), 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Open any modals the GUI requested last frame. Must run before the modals are drawn.
static void open_requested_modals(AppState& state)
{
    if (is_discard_modal_requested)
    {
        ImGui::OpenPopup("Discard Changes");
        is_discard_modal_requested = false;
    }
    if (state.is_about_modal_requested)
    {
        ImGui::OpenPopup("About Surume");
        state.is_about_modal_requested = false;
    }
    if (state.is_settings_modal_requested)
    {
        ImGui::OpenPopup("Settings");
        state.is_settings_modal_requested = false;
    }
    if (state.is_new_branch_modal_requested)
    {
        ImGui::OpenPopup("New Branch");
        state.is_new_branch_modal_requested = false;
    }
    if (state.is_delete_branch_modal_requested)
    {
        ImGui::OpenPopup("Delete Branch");
        state.is_delete_branch_modal_requested = false;
    }
}

// -------------------------------------------------------------------------
// Menu bar
// -------------------------------------------------------------------------

static void render_file_menu(AppState& state)
{
    if (!ImGui::BeginMenu("File"))
    {
        return;
    }

    if (ImGui::MenuItem("Open Repository...", nullptr, false, can_open_repos(state)))
    {
        request(state, RequestKind::OPEN_REPO_DIALOG);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit", "Alt+F4"))
    {
        request(state, RequestKind::EXIT);
    }
    ImGui::EndMenu();
}

static void render_edit_menu()
{
    if (!ImGui::BeginMenu("Edit"))
    {
        return;
    }

    ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
    ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
    ImGui::Separator();
    ImGui::MenuItem("Cut", "Ctrl+X", false, false);
    ImGui::MenuItem("Copy", "Ctrl+C", false, false);
    ImGui::MenuItem("Paste", "Ctrl+V", false, false);
    ImGui::EndMenu();
}

static void render_view_menu(AppState& state)
{
    if (!ImGui::BeginMenu("View"))
    {
        return;
    }

    if (ImGui::MenuItem("Changes", "Ctrl+1", state.nav_mode == NavMode::CHANGES))
    {
        state.nav_mode = NavMode::CHANGES;
    }
    if (ImGui::MenuItem("History", "Ctrl+2", state.nav_mode == NavMode::HISTORY))
    {
        state.nav_mode = NavMode::HISTORY;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Refresh", "F5", false, !state.active_repo.empty()))
    {
        request(state, RequestKind::REFRESH);
    }
    ImGui::EndMenu();
}

static void render_repository_menu(AppState& state)
{
    if (!ImGui::BeginMenu("Repository"))
    {
        return;
    }

    bool has_repo = !state.active_repo.empty();
    if (ImGui::MenuItem("Open in Explorer", nullptr, false, has_repo))
    {
        request(state, RequestKind::OPEN_IN_EXPLORER);
    }
    if (ImGui::MenuItem("Open in Terminal", nullptr, false, has_repo))
    {
        request(state, RequestKind::OPEN_IN_TERMINAL);
    }
    ImGui::Separator();
    bool can_discard = has_repo && !state.repo.files.empty() && !state.is_mutation_running;
    if (ImGui::MenuItem("Discard all changes...", nullptr, false, can_discard))
    {
        request_discard("");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Settings..."))
    {
        state.is_settings_modal_requested = true;
    }
    ImGui::EndMenu();
}

static void render_branch_menu(AppState& state)
{
    if (!ImGui::BeginMenu("Branch"))
    {
        return;
    }

    bool has_repo = !state.active_repo.empty();
    if (ImGui::MenuItem("New Branch...", nullptr, false, has_repo))
    {
        state.is_new_branch_modal_requested = true;
    }
    if (ImGui::MenuItem("Delete Branch...", nullptr, false, has_repo))
    {
        state.is_delete_branch_modal_requested = true;
    }
    ImGui::EndMenu();
}

static void render_help_menu(AppState& state)
{
    if (!ImGui::BeginMenu("Help"))
    {
        return;
    }

    if (ImGui::MenuItem("About Surume"))
    {
        state.is_about_modal_requested = true;
    }
    ImGui::EndMenu();
}

static void render_menu_bar(AppState& state)
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    render_file_menu(state);
    render_edit_menu();
    render_view_menu(state);
    render_repository_menu(state);
    render_branch_menu(state);
    render_help_menu(state);
    ImGui::EndMainMenuBar();
}

// -------------------------------------------------------------------------
// Header bar
// -------------------------------------------------------------------------

static std::string folder_name(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    return slash != std::string::npos ? path.substr(slash + 1) : path;
}

static void render_repo_picker(AppState& state)
{
    const char* name = state.active_repo_name.empty() ? "No Repository" :
        state.active_repo_name.c_str();
    char label[PICKER_LABEL_SIZE];
    snprintf(label, sizeof(label), " %s  v ##repobutton", name);
    if (ImGui::Button(label))
    {
        ImGui::OpenPopup("RepoPicker");
    }

    if (!ImGui::BeginPopup("RepoPicker"))
    {
        return;
    }

    ImGui::TextDisabled("Recent Repositories");
    ImGui::Separator();
    ImGui::BeginDisabled(!can_open_repos(state));
    if (state.recent_repos.empty())
    {
        ImGui::TextDisabled("No recent repositories");
    }
    else
    {
        for (const std::string& repo : state.recent_repos)
        {
            if (ImGui::Selectable(folder_name(repo).c_str()))
            {
                request_text(state, RequestKind::SWITCH_REPO, repo);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", repo.c_str());
            }
        }
        ImGui::Separator();
    }

    if (ImGui::Selectable("  Open..."))
    {
        request(state, RequestKind::OPEN_REPO_DIALOG);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

// Dimmed text after a branch name saying where it lives.
static const char* branch_marker(const BranchInfo& branch)
{
    switch (branch.location)
    {
        case BranchLocation::LOCAL:
            return "local only";
        case BranchLocation::REMOTE:
            return "remote";
        case BranchLocation::LOCAL_AND_REMOTE:
            return branch.upstream.c_str();
    }
    return "";
}

static void checkout_branch(AppState& state, const BranchInfo& branch)
{
    LOG_INFO("BranchPicker: checking out '%s'", branch.name.c_str());
    Request checkout;
    checkout.kind = RequestKind::CHECKOUT_BRANCH;
    checkout.text = branch.name;
    checkout.flag = branch.location == BranchLocation::REMOTE;
    state.requests.push_back(checkout);
}

static void render_branch_picker_popup(AppState& state)
{
    ImGui::TextDisabled("Switch Branch");
    ImGui::Separator();

    const std::vector<BranchInfo>& branches = state.repo.branches;
    if (ImGui::BeginTable("##branches", 2, ImGuiTableFlags_SizingFixedFit))
    {
        for (int i = 0; i < (int)branches.size(); ++i)
        {
            const BranchInfo& branch = branches[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char entry[PICKER_LABEL_SIZE];
            snprintf(entry, sizeof(entry), "%s%s", branch.is_current ? "* " : "  ",
                branch.name.c_str());
            if (ImGui::Selectable(entry, branch.is_current,
                ImGuiSelectableFlags_SpanAllColumns) && !branch.is_current)
            {
                checkout_branch(state, branch);
                ImGui::CloseCurrentPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", branch_marker(branch));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!branches.empty())
    {
        ImGui::Separator();
    }
    if (ImGui::Selectable("  New Branch..."))
    {
        state.is_new_branch_modal_requested = true;
        ImGui::CloseCurrentPopup();
    }
}

static void render_branch_picker(AppState& state)
{
    // No switching while another change to the repository is still running
    bool has_repo = !state.active_repo.empty();
    ImGui::BeginDisabled(!has_repo || state.is_mutation_running);

    const std::string& current = state.repo.head.name;
    const char* name = current.empty() ? "No Branch" : current.c_str();
    char label[PICKER_LABEL_SIZE];
    snprintf(label, sizeof(label), " %s  v ##branchbutton", name);
    if (ImGui::Button(label))
    {
        ImGui::OpenPopup("BranchPicker");
    }

    if (ImGui::BeginPopup("BranchPicker"))
    {
        render_branch_picker_popup(state);
        ImGui::EndPopup();
    }

    ImGui::EndDisabled();
}

static void render_sync_button(const AppState& state, float padding_y)
{
    // Placeholder until sync is implemented
    const char* label = "Fetch origin";
    float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2 +
        scaled(SYNC_BUTTON_EXTRA_WIDTH);
    float x = ImGui::GetWindowWidth() - width - scaled(HEADER_PADDING_X);

    // What the worker is doing, left of the button
    if (state.is_busy && !state.busy_label.empty())
    {
        char busy[PICKER_LABEL_SIZE];
        snprintf(busy, sizeof(busy), "%s...", state.busy_label.c_str());
        float busy_width = ImGui::CalcTextSize(busy).x;
        ImGui::SetCursorPos(ImVec2(x - busy_width - scaled(HEADER_PADDING_X),
            padding_y + ImGui::GetStyle().FramePadding.y));
        ImGui::TextDisabled("%s", busy);
    }

    ImGui::SetCursorPos(ImVec2(x, padding_y));
    ImGui::BeginDisabled(state.active_repo.empty());
    if (ImGui::Button(label, ImVec2(width, 0)))
    {
        LOG_INFO("Header: sync button clicked (not implemented)");
    }
    ImGui::EndDisabled();
}

// "up 2 down 1" arrows next to the branch picker, or a note when the branch isn't published.
static void render_ahead_behind(const AppState& state)
{
    const BranchStatus& head = state.repo.head;
    if (state.active_repo.empty() || head.name.empty())
    {
        return;
    }

    ImGui::SameLine(0, scaled(HEADER_BUTTON_SPACING) * 2);
    ImGui::AlignTextToFramePadding();
    if (head.upstream.empty())
    {
        ImGui::TextDisabled("not published");
        return;
    }

    ImGui::TextDisabled("%s%d  %s%d", ARROW_UP, head.ahead, ARROW_DOWN, head.behind);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%d ahead of and %d behind %s", head.ahead, head.behind,
            head.upstream.c_str());
    }
}

static void render_header_bar(AppState& state)
{
    float padding_y = (scaled(HEADER_HEIGHT) - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorPos(ImVec2(scaled(HEADER_PADDING_X), padding_y));

    render_repo_picker(state);
    ImGui::SameLine(0, scaled(HEADER_BUTTON_SPACING));
    render_branch_picker(state);
    render_ahead_behind(state);
    render_sync_button(state, padding_y);

    ImVec2 line_start = ImGui::GetWindowPos();
    line_start.y += scaled(HEADER_HEIGHT);
    ImVec2 line_end = ImVec2(line_start.x + ImGui::GetWindowWidth(), line_start.y);
    ImGui::GetWindowDrawList()->AddLine(line_start, line_end,
        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
}

// -------------------------------------------------------------------------
// Changes tab
// -------------------------------------------------------------------------

static FileBadge file_badge(const FileStatus& file)
{
    FileBadge badge;
    if (file.is_conflicted)
    {
        badge = { '!', COLOR_CONFLICTED, "Conflicted: finish the merge in an editor or terminal" };
    }
    else if (file.index_status == '?' && file.worktree_status == '?')
    {
        badge = { '?', COLOR_UNTRACKED, "Untracked" };
    }
    else if (file.index_status == 'D' || file.worktree_status == 'D')
    {
        badge = { 'D', COLOR_DELETED, "Deleted" };
    }
    else if (file.index_status == 'R')
    {
        badge = { 'R', COLOR_RENAMED, "Renamed" };
    }
    else if (file.index_status == 'A')
    {
        const char* tooltip = file.worktree_status == 'M' ? "New file (modified)" : "New file";
        badge = { 'A', COLOR_ADDED, tooltip };
    }
    return badge;
}

static bool is_ticked(const AppState& state, const FileStatus& file)
{
    return state.unticked_paths.count(file.path) == 0;
}

static int count_ticked(const AppState& state)
{
    int count = 0;
    for (const FileStatus& file : state.repo.files)
    {
        count += is_ticked(state, file) ? 1 : 0;
    }
    return count;
}

// x positions of the badge and the path, after the checkbox
struct FileRowLayout
{
    float badge_x = 0.0f;
    float path_x = 0.0f;
    float path_width = 0.0f;
};

// Case-insensitive (ASCII) substring match; an empty filter matches everything.
static bool matches_filter(const std::string& path, const char* filter)
{
    if (filter[0] == '\0')
    {
        return true;
    }

    size_t filter_len = strlen(filter);
    for (size_t start = 0; start + filter_len <= path.size(); ++start)
    {
        if (_strnicmp(path.c_str() + start, filter, filter_len) == 0)
        {
            return true;
        }
    }
    return false;
}

// Indices into state.repo.files of the files the filter shows.
static void filtered_files(const AppState& state, std::vector<int>& indices)
{
    indices.clear();
    for (int i = 0; i < (int)state.repo.files.size(); ++i)
    {
        if (matches_filter(state.repo.files[i].path, state.file_filter))
        {
            indices.push_back(i);
        }
    }
}

static void select_file(AppState& state, const FileStatus& file)
{
    state.selected_file_path = file.path;
    state.diff = {};
    request(state, RequestKind::LOAD_DIFF);
}

// Right-click menu on a changed file.
static void render_file_context_menu(AppState& state, const FileStatus& file)
{
    if (!ImGui::BeginPopupContextItem("##filemenu"))
    {
        return;
    }

    if (ImGui::MenuItem("Discard changes...", nullptr, false, !state.is_mutation_running))
    {
        request_discard(file.path);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy path"))
    {
        ImGui::SetClipboardText(file.path.c_str());
    }
    bool exists_on_disk = file.worktree_status != 'D';
    if (ImGui::MenuItem("Show in Explorer", nullptr, false, exists_on_disk))
    {
        request_text(state, RequestKind::SHOW_IN_EXPLORER, file.path);
    }
    ImGui::EndPopup();
}

static void render_file_row(AppState& state, int index, const FileRowLayout& layout)
{
    const FileStatus& file = state.repo.files[index];

    bool ticked = is_ticked(state, file);
    if (ImGui::Checkbox("##tick", &ticked))
    {
        if (ticked)
        {
            state.unticked_paths.erase(file.path);
        }
        else
        {
            state.unticked_paths[file.path] = true;
        }
    }

    FileBadge badge = file_badge(file);
    ImGui::SameLine(layout.badge_x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(badge.color, "%c", badge.letter);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", badge.tooltip);
    }
    ImGui::SameLine(layout.path_x);

    std::string display_path = file.path;
    if (!display_path.empty() && (display_path.back() == '/' || display_path.back() == '\\'))
    {
        display_path.pop_back();
    }

    bool is_selected = state.selected_file_path == file.path;
    ImVec2 row_size = ImVec2(layout.path_width, ImGui::GetFrameHeight());
    if (ImGui::Selectable(display_path.c_str(), is_selected, ImGuiSelectableFlags_None,
        row_size) && !is_selected)
    {
        select_file(state, file);
    }
    if (is_selected && should_scroll_to_selection)
    {
        ImGui::SetScrollHereY();
        should_scroll_to_selection = false;
    }
    if (!file.old_path.empty() && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Renamed from: %s", file.old_path.c_str());
    }
    render_file_context_menu(state, file);
}

// Tick-all checkbox (mixed when only some files are ticked) and the file count.
// Tick-all checkbox for the visible files (mixed when only some are ticked), and the count.
static void render_tick_all(AppState& state)
{
    int total = (int)state.repo.files.size();
    int visible = (int)visible_files.size();
    int ticked = 0;
    for (int index : visible_files)
    {
        ticked += is_ticked(state, state.repo.files[index]) ? 1 : 0;
    }
    bool is_mixed = ticked > 0 && ticked < visible;
    bool is_all = visible > 0 && ticked == visible;

    ImGui::BeginDisabled(visible == 0);
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, is_mixed);
    if (ImGui::Checkbox("##tickall", &is_all))
    {
        for (int index : visible_files)
        {
            const std::string& path = state.repo.files[index].path;
            if (is_all)
            {
                state.unticked_paths.erase(path);
            }
            else
            {
                state.unticked_paths[path] = true;
            }
        }
    }
    ImGui::PopItemFlag();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (visible == total)
    {
        ImGui::TextDisabled("%d changed file%s", total, total == 1 ? "" : "s");
    }
    else
    {
        ImGui::TextDisabled("%d of %d changed files", visible, total);
    }
    ImGui::Separator();
}

// Up/Down move the selection through the visible files when no text field has focus.
static void handle_file_list_keys(AppState& state)
{
    if (ImGui::GetIO().WantTextInput || visible_files.empty())
    {
        return;
    }

    int step = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        step = 1;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        step = -1;
    }
    if (step == 0)
    {
        return;
    }

    int current = -1;
    for (int i = 0; i < (int)visible_files.size(); ++i)
    {
        if (state.repo.files[visible_files[i]].path == state.selected_file_path)
        {
            current = i;
        }
    }
    int next = current < 0 ? 0 : std::clamp(current + step, 0, (int)visible_files.size() - 1);
    if (next != current)
    {
        select_file(state, state.repo.files[visible_files[next]]);
        should_scroll_to_selection = true;
    }
}

// Conflicts block normal work until resolved, so say so above the list.
static void render_conflict_banner(const AppState& state)
{
    for (const FileStatus& file : state.repo.files)
    {
        if (file.is_conflicted)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, COLOR_CONFLICTED);
            ImGui::TextWrapped("This repository has merge conflicts. Resolve them in an editor "
                "or terminal, then commit.");
            ImGui::PopStyleColor();
            ImGui::Separator();
            return;
        }
    }
}

static void render_file_list(AppState& state)
{
    if (state.active_repo.empty())
    {
        return;
    }
    if (state.repo.files.empty())
    {
        ImGui::TextDisabled("No local changes");
        return;
    }
    render_conflict_banner(state);

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter files", state.file_filter,
        sizeof(state.file_filter));
    filtered_files(state, visible_files);
    handle_file_list_keys(state);
    render_tick_all(state);
    if (visible_files.empty())
    {
        ImGui::TextDisabled("No files match the filter");
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    FileRowLayout layout;
    layout.badge_x = ImGui::GetCursorPosX() + ImGui::GetFrameHeight() + style.ItemSpacing.x;
    layout.path_x = layout.badge_x + ImGui::CalcTextSize("M").x + style.ItemSpacing.x * 2;
    layout.path_width = ImGui::GetContentRegionAvail().x - layout.path_x;

    ImGuiListClipper clipper;
    clipper.Begin((int)visible_files.size());
    for (int i = 0; i < (int)visible_files.size() && should_scroll_to_selection; ++i)
    {
        if (state.repo.files[visible_files[i]].path == state.selected_file_path)
        {
            clipper.IncludeItemByIndex(i); // so the selected row exists to scroll to
        }
    }
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            ImGui::PushID(visible_files[i]);
            render_file_row(state, visible_files[i], layout);
            ImGui::PopID();
        }
    }
}

static void commit(AppState& state)
{
    LOG_INFO("Commit: summary='%s' branch='%s'", state.commit_summary,
        state.repo.head.name.c_str());
    request(state, RequestKind::COMMIT);
}

static void render_commit_footer(AppState& state, float width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(scaled(COMMIT_FOOTER_PADDING_X), scaled(COMMIT_FOOTER_PADDING_Y)));
    ImGui::BeginChild("##commitfooter", ImVec2(width, scaled(COMMIT_FOOTER_HEIGHT)), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    bool has_repo = !state.active_repo.empty();
    float input_width = ImGui::GetContentRegionAvail().x;
    ImGui::BeginDisabled(!has_repo);

    ImGui::SetNextItemWidth(input_width);
    ImGui::InputTextWithHint("##summary", "Summary (required)",
        state.commit_summary, sizeof(state.commit_summary));
    ImGui::Spacing();
    ImGui::InputTextMultiline("##description", state.commit_description,
        sizeof(state.commit_description),
        ImVec2(input_width, scaled(COMMIT_DESCRIPTION_HEIGHT)));
    ImGui::Spacing();

    int ticked = count_ticked(state);
    bool can_commit = has_repo && ticked > 0 && state.commit_summary[0] != '\0' &&
        !state.is_mutation_running;
    ImGui::BeginDisabled(!can_commit);
    char label[COMMIT_LABEL_SIZE];
    const std::string& current = state.repo.head.name;
    const char* branch = current.empty() ? "branch" : current.c_str();
    snprintf(label, sizeof(label), "Commit %d file%s to %s", ticked, ticked == 1 ? "" : "s",
        branch);
    if (ImGui::Button(label, ImVec2(input_width, 0)))
    {
        commit(state);
    }
    ImGui::EndDisabled();

    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

static void render_changes_tab(AppState& state)
{
    ImVec2 available = ImGui::GetContentRegionAvail();

    float list_height = available.y - scaled(COMMIT_FOOTER_HEIGHT);
    ImGui::BeginChild("##filelist", ImVec2(available.x, list_height),
        false);
    render_file_list(state);
    ImGui::EndChild();

    render_commit_footer(state, available.x);
}

// -------------------------------------------------------------------------
// History tab
// -------------------------------------------------------------------------

// Two fixed-height lines: the subject, then hash, author and date. Nothing is right-aligned,
// so long relative dates ("2 years, 3 months ago") can't overlap other text.
static void select_commit(AppState& state, const CommitInfo& commit)
{
    state.selected_commit_hash = commit.full_hash;
    state.commit_details = {};
    state.selected_commit_file.clear();
    state.commit_diff = {};
    request(state, RequestKind::LOAD_COMMIT);
}

// Two fixed-height lines: the subject, then hash, author and date. Nothing is right-aligned,
// so long relative dates ("2 years, 3 months ago") can't overlap other text.
static void render_commit_row(AppState& state, const CommitInfo& commit)
{
    bool is_selected = commit.full_hash == state.selected_commit_hash;
    ImVec2 row_start = ImGui::GetCursorPos();
    float row_height = ImGui::GetTextLineHeightWithSpacing() * 2;
    if (ImGui::Selectable("##commit", is_selected, ImGuiSelectableFlags_AllowOverlap,
        ImVec2(0, row_height)) && !is_selected)
    {
        select_commit(state, commit);
    }
    if (is_selected && should_scroll_to_selection)
    {
        ImGui::SetScrollHereY();
        should_scroll_to_selection = false;
    }

    ImGui::SetCursorPos(row_start);
    ImGui::TextUnformatted(commit.subject.c_str());
    ImGui::TextDisabled("%s  %s, %s", commit.hash.c_str(), commit.author.c_str(),
        commit.date.c_str());
    ImGui::Separator();
}

// Up/Down move the commit selection when no text field has focus.
static void handle_history_keys(AppState& state)
{
    const std::vector<CommitInfo>& commits = state.repo.commits;
    if (ImGui::GetIO().WantTextInput || commits.empty())
    {
        return;
    }

    int step = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        step = 1;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        step = -1;
    }
    if (step == 0)
    {
        return;
    }

    int current = -1;
    for (int i = 0; i < (int)commits.size(); ++i)
    {
        if (commits[i].full_hash == state.selected_commit_hash)
        {
            current = i;
        }
    }
    int next = current < 0 ? 0 : std::clamp(current + step, 0, (int)commits.size() - 1);
    if (next != current)
    {
        select_commit(state, commits[next]);
        should_scroll_to_selection = true;
    }
}

static void render_history_tab(AppState& state)
{
    ImGui::BeginChild("##historylist", ImGui::GetContentRegionAvail(), false);

    if (state.active_repo.empty())
    {
        ImGui::EndChild();
        return;
    }
    if (state.repo.commits.empty())
    {
        ImGui::TextDisabled("No commits on this branch.");
        ImGui::EndChild();
        return;
    }

    handle_history_keys(state);
    ImGuiListClipper clipper;
    clipper.Begin((int)state.repo.commits.size());
    for (int i = 0; i < (int)state.repo.commits.size() && should_scroll_to_selection; ++i)
    {
        if (state.repo.commits[i].full_hash == state.selected_commit_hash)
        {
            clipper.IncludeItemByIndex(i);
        }
    }
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            ImGui::PushID(i);
            render_commit_row(state, state.repo.commits[i]);
            ImGui::PopID();
        }
    }

    ImGui::EndChild();
}

// -------------------------------------------------------------------------
// Left column
// -------------------------------------------------------------------------

// Tab flags that force the tab for state.nav_mode open when it differs from the tab shown
// last frame (the View menu or a shortcut changed it).
static ImGuiTabItemFlags tab_flags(const AppState& state, NavMode tab)
{
    bool is_forced = state.nav_mode != shown_nav_mode && state.nav_mode == tab;
    return is_forced ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
}

// Record that tab is the one ImGui is showing. A click on a tab updates state.nav_mode; a
// pending forced switch wins until ImGui has applied it.
static void on_tab_shown(AppState& state, NavMode tab)
{
    if (state.nav_mode == shown_nav_mode)
    {
        state.nav_mode = tab;
    }
    shown_nav_mode = tab;
}

static void render_left_column(AppState& state)
{
    if (!ImGui::BeginTabBar("##navtabs"))
    {
        return;
    }

    if (ImGui::BeginTabItem("Changes", nullptr, tab_flags(state, NavMode::CHANGES)))
    {
        on_tab_shown(state, NavMode::CHANGES);
        render_changes_tab(state);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("History", nullptr, tab_flags(state, NavMode::HISTORY)))
    {
        on_tab_shown(state, NavMode::HISTORY);
        render_history_tab(state);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

// -------------------------------------------------------------------------
// Right content
// -------------------------------------------------------------------------

static void render_diff_line(const char* start, const char* end, DiffLineKind kind,
    float line_height, float window_x, float window_width)
{
    float y = ImGui::GetCursorScreenPos().y;
    ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImU32 background = 0;

    switch (kind)
    {
        case DiffLineKind::ADDED:
            text_color = COLOR_DIFF_ADDED_TEXT;
            background = COLOR_DIFF_ADDED_BACKGROUND;
            break;
        case DiffLineKind::REMOVED:
            text_color = COLOR_DIFF_REMOVED_TEXT;
            background = COLOR_DIFF_REMOVED_BACKGROUND;
            break;
        case DiffLineKind::HUNK:
            text_color = COLOR_DIFF_HUNK_TEXT;
            break;
        case DiffLineKind::HEADER:
            text_color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            break;
        case DiffLineKind::CONTEXT:
            break;
    }

    if (background != 0)
    {
        ImVec2 top_left = ImVec2(window_x, y);
        ImVec2 bottom_right = ImVec2(window_x + window_width + DIFF_BACKGROUND_OVERSCAN,
            y + line_height);
        ImGui::GetWindowDrawList()->AddRectFilled(top_left, bottom_right, background);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::TextUnformatted(start, end);
    ImGui::PopStyleColor();
}

// The monospace font main.cpp loads second, or nullptr if it couldn't be loaded.
static ImFont* mono_font()
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    return atlas->Fonts.Size > 1 ? atlas->Fonts[1] : nullptr;
}

// Only the visible lines are drawn; the content width comes from the widest line so the
// horizontal scroll range covers lines that aren't drawn.
// Only the visible lines are drawn; the content width comes from the widest line so the
// horizontal scroll range covers lines that aren't drawn.
static void render_diff(const DiffView& view, const char* id)
{
    ImGui::PushFont(mono_font(), 0.0f);
    float char_width = ImGui::CalcTextSize("M").x;
    ImGui::SetNextWindowContentSize(ImVec2(view.max_columns * char_width, 0.0f));
    ImGui::BeginChild(id, ImGui::GetContentRegionAvail(), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    if (view.lines.empty())
    {
        ImGui::TextDisabled("(no diff output)");
        ImGui::EndChild();
        ImGui::PopFont();
        return;
    }

    float line_height = ImGui::GetTextLineHeightWithSpacing();
    float window_x = ImGui::GetWindowPos().x;
    float window_width = ImGui::GetWindowSize().x;
    const char* text = view.text.c_str();

    ImGuiListClipper clipper;
    clipper.Begin((int)view.lines.size(), line_height);
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const DiffLine& line = view.lines[i];
            const char* start = text + line.start;
            render_diff_line(start, start + line.length, line.kind, line_height, window_x,
                window_width);
        }
    }

    ImGui::EndChild();
    ImGui::PopFont();
}

static void render_centered_hint(const char* text, float y)
{
    float text_width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPos(ImVec2((ImGui::GetContentRegionAvail().x - text_width) * 0.5f, y));
    ImGui::TextDisabled("%s", text);
}

static void render_git_missing(ImVec2 available)
{
    const char* lines[] = {
        "Git is not installed or could not be found.",
        "Install Git for Windows from git-scm.com and make sure git is on PATH,",
        "then restart Surume.",
    };
    float line_height = ImGui::GetTextLineHeightWithSpacing();
    float y = (available.y - line_height * 3) * 0.5f;
    for (const char* line : lines)
    {
        float text_width = ImGui::CalcTextSize(line).x;
        ImGui::SetCursorPos(ImVec2((available.x - text_width) * 0.5f, y));
        ImGui::TextUnformatted(line);
        y += line_height;
    }
}

static void render_empty_state(AppState& state, ImVec2 available)
{
    if (!can_open_repos(state))
    {
        render_git_missing(available);
        return;
    }

    float button_height = ImGui::GetFrameHeight();
    ImGui::SetCursorPos(ImVec2((available.x - scaled(EMPTY_STATE_BUTTON_WIDTH)) * 0.5f,
        (available.y - button_height * 2) * 0.5f));
    if (ImGui::Button("Open repository...", ImVec2(scaled(EMPTY_STATE_BUTTON_WIDTH), 0)))
    {
        request(state, RequestKind::OPEN_REPO_DIALOG);
    }

    float text_width = ImGui::CalcTextSize("No repository open").x;
    ImGui::SetCursorPosX((available.x - text_width) * 0.5f);
    ImGui::TextDisabled("No repository open");
}

// Git prints "Binary files a/x and b/x differ" instead of hunks for binary changes.
static bool is_binary_diff(const std::string& diff)
{
    bool has_binary_line = diff.rfind("Binary files ", 0) == 0 ||
        diff.find("\nBinary files ") != std::string::npos;
    return has_binary_line && diff.find("\n@@") == std::string::npos;
}

// Show the diff, or a placeholder for binary files and diffs over the size limit.
// Show the diff, or a placeholder for binary files and diffs over the size limit.
static void render_diff_or_placeholder(DiffView& view, ImVec2 available, const char* id)
{
    float hint_y = available.y * EMPTY_HINT_Y_FRACTION;
    if (is_binary_diff(view.text))
    {
        render_centered_hint("Binary file changed. No text diff to show.", hint_y);
        return;
    }
    if (view.text.size() > LARGE_DIFF_BYTES && !view.is_large_confirmed)
    {
        char message[PICKER_LABEL_SIZE];
        snprintf(message, sizeof(message), "This diff is large (%.1f MB).",
            view.text.size() / BYTES_PER_MEGABYTE);
        render_centered_hint(message, hint_y);
        float button_width = scaled(EMPTY_STATE_BUTTON_WIDTH);
        ImGui::SetCursorPosX((available.x - button_width) * 0.5f);
        if (ImGui::Button("Show anyway", ImVec2(button_width, 0)))
        {
            view.is_large_confirmed = true;
        }
        return;
    }
    render_diff(view, id);
}

// Commit header, changed files, and the selected file's diff for the History tab.
static void render_commit_pane(AppState& state, ImVec2 available)
{
    const CommitDetails& details = state.commit_details;
    if (details.full_hash.empty())
    {
        render_centered_hint("Loading commit...", available.y * EMPTY_HINT_Y_FRACTION);
        return;
    }

    ImGui::BeginChild("##commitinfo", ImVec2(0, available.y * COMMIT_INFO_FRACTION), false);
    ImGui::TextWrapped("%s", details.message.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("%s <%s>  %s  %s", details.author.c_str(), details.email.c_str(),
        details.date.c_str(), details.full_hash.c_str());
    ImGui::Separator();
    for (int i = 0; i < (int)details.files.size(); ++i)
    {
        const FileStatus& file = details.files[i];
        ImGui::PushID(i);
        FileBadge badge = file_badge(file);
        ImGui::TextColored(badge.color, "%c", file.index_status);
        ImGui::SameLine();
        bool is_selected = file.path == state.selected_commit_file;
        if (ImGui::Selectable(file.path.c_str(), is_selected) && !is_selected)
        {
            state.selected_commit_file = file.path;
            state.commit_diff = {};
            request(state, RequestKind::LOAD_COMMIT_DIFF);
        }
        if (!file.old_path.empty() && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Renamed from: %s", file.old_path.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();

    ImVec2 diff_area = ImGui::GetContentRegionAvail();
    if (state.selected_commit_file.empty())
    {
        render_centered_hint("Select a file to view its changes",
            ImGui::GetCursorPosY() + diff_area.y * EMPTY_HINT_Y_FRACTION);
        return;
    }
    render_diff_or_placeholder(state.commit_diff, diff_area, "##commitdiff");
}

static void render_right_content(AppState& state)
{
    ImVec2 available = ImGui::GetContentRegionAvail();

    if (state.active_repo.empty())
    {
        render_empty_state(state, available);
    }
    else if (state.nav_mode == NavMode::HISTORY)
    {
        if (state.selected_commit_hash.empty())
        {
            float hint_y = available.y * EMPTY_HINT_Y_FRACTION;
            render_centered_hint("Select a commit to view its changes", hint_y);
        }
        else
        {
            render_commit_pane(state, available);
        }
    }
    else if (state.selected_file_path.empty())
    {
        float hint_y = available.y * EMPTY_HINT_Y_FRACTION;
        render_centered_hint("Select a file to view its diff", hint_y);
    }
    else
    {
        render_diff_or_placeholder(state.diff, available, "##diffview");
    }

    if (!state.status_message.empty())
    {
        float line_height = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetCursorPos(ImVec2(scaled(STATUS_MESSAGE_MARGIN_X),
            available.y - line_height - scaled(STATUS_MESSAGE_MARGIN_BOTTOM)));
        ImGui::TextColored(COLOR_ERROR_TEXT, "%s", state.status_message.c_str());
    }
}

// -------------------------------------------------------------------------
// Splitter
// -------------------------------------------------------------------------

static void render_splitter(AppState& state, float height)
{
    ImVec2 top_left = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##splitter", ImVec2(scaled(SPLITTER_WIDTH), height));

    bool is_active = ImGui::IsItemActive();
    bool is_hovered = ImGui::IsItemHovered();
    float delta = ImGui::GetIO().MouseDelta.x;
    if (is_active && delta != 0.0f)
    {
        // Stored unscaled so the layout is the same on every monitor
        state.left_column_width = std::clamp(state.left_column_width + delta / ui_scale,
            MIN_COLUMN_WIDTH, MAX_COLUMN_WIDTH);
    }
    if (ImGui::IsItemDeactivated())
    {
        request(state, RequestKind::SAVE_SETTINGS);
    }
    if (is_active || is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGuiCol color_id = ImGuiCol_Separator;
    if (is_active)
    {
        color_id = ImGuiCol_SeparatorActive;
    }
    else if (is_hovered)
    {
        color_id = ImGuiCol_SeparatorHovered;
    }
    ImVec2 bottom_right = ImVec2(top_left.x + scaled(SPLITTER_WIDTH), top_left.y + height);
    ImGui::GetWindowDrawList()->AddRectFilled(top_left, bottom_right,
        ImGui::GetColorU32(color_id));
}

// -------------------------------------------------------------------------

// App-wide keyboard shortcuts.
static void handle_shortcuts(AppState& state)
{
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !state.active_repo.empty())
    {
        request(state, RequestKind::REFRESH);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_1))
    {
        state.nav_mode = NavMode::CHANGES;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_2))
    {
        state.nav_mode = NavMode::HISTORY;
    }
}

void gui_render(AppState& state)
{
    ui_scale = ImGui::GetStyle().FontScaleDpi;
    if (is_first_frame)
    {
        LOG_INFO("gui_render: first frame, display=%.0fx%.0f",
            ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        is_first_frame = false;
    }

    handle_shortcuts(state);

    open_requested_modals(state);
    render_about_modal();
    render_settings_modal(state);
    render_new_branch_modal(state);
    render_delete_branch_modal(state);
    render_discard_modal(state);

    render_menu_bar(state);

    const ImGuiIO& io = ImGui::GetIO();
    float menu_height = ImGui::GetFrameHeight();
    float total_width = io.DisplaySize.x;
    float total_height = io.DisplaySize.y - menu_height;
    float content_height = total_height - scaled(HEADER_HEIGHT);
    float left_width = scaled(state.left_column_width);
    float right_width = total_width - left_width - scaled(SPLITTER_WIDTH);

    ImGui::SetNextWindowPos(ImVec2(0, menu_height));
    ImGui::SetNextWindowSize(ImVec2(total_width, total_height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##main", nullptr, MAIN_WINDOW_FLAGS);
    ImGui::PopStyleVar(3);

    render_header_bar(state);
    ImGui::SetCursorPos(ImVec2(0, scaled(HEADER_HEIGHT)));

    ImGui::BeginChild("##leftcolumn", ImVec2(left_width, content_height), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    render_left_column(state);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    render_splitter(state, content_height);
    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##rightcolumn", ImVec2(right_width, content_height), false);
    render_right_content(state);
    ImGui::EndChild();

    ImGui::End();
}
