#include "gui.hpp"
#include "git.hpp"
#include "log.hpp"
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static constexpr float kHeaderHeight       = 40.0f;
static constexpr float kSplitterWidth      = 4.0f;
static constexpr float kCommitFooterHeight = 148.0f;
static constexpr float kMinColumnWidth     = 160.0f;
static constexpr float kMaxColumnWidth     = 600.0f;

// -------------------------------------------------------------------------
// Modals
// -------------------------------------------------------------------------

static void RenderAboutModal() {
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("About Surume", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::Text("Surume");
    ImGui::TextDisabled("v0.1.0 — Milestone 1");
    ImGui::Spacing();
    ImGui::TextWrapped("A native C++ desktop Git client built with Dear ImGui.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    float btnW = 80.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - btnW) * 0.5f);
    if (ImGui::Button("OK", ImVec2(btnW, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

static void RenderSettingsModal(AppState& state) {
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    static char s_gitPath[512]       = {};
    static char s_defaultDir[512]    = {};
    static bool s_autoFetch          = false;
    static int  s_autoFetchInterval  = 15;
    static bool s_initialized        = false;

    if (!s_initialized) {
        strncpy_s(s_gitPath,    state.settings.gitPath.c_str(),       sizeof(s_gitPath) - 1);
        strncpy_s(s_defaultDir, state.settings.defaultOpenDir.c_str(), sizeof(s_defaultDir) - 1);
        s_autoFetch         = state.settings.autoFetchEnabled;
        s_autoFetchInterval = state.settings.autoFetchIntervalMin;
        s_initialized       = true;
    }

    float labelW = 160.0f;
    float inputW = ImGui::GetContentRegionAvail().x - labelW - ImGui::GetStyle().ItemSpacing.x;

    ImGui::Text("Git executable path");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputTextWithHint("##gitpath", "Leave empty to use PATH", s_gitPath, sizeof(s_gitPath));

    ImGui::Text("Default open directory");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputTextWithHint("##defdir", "Leave empty for last used", s_defaultDir, sizeof(s_defaultDir));

    ImGui::Separator();
    ImGui::Text("Auto-fetch");
    ImGui::SameLine(labelW);
    ImGui::Checkbox("##autofetch", &s_autoFetch);
    if (s_autoFetch) {
        ImGui::SameLine();
        ImGui::Text("every");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::InputInt("##afi", &s_autoFetchInterval, 1, 5);
        if (s_autoFetchInterval < 1)  s_autoFetchInterval = 1;
        if (s_autoFetchInterval > 120) s_autoFetchInterval = 120;
        ImGui::SameLine();
        ImGui::Text("min");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Save", ImVec2(100, 0))) {
        LOG_INFO("Settings: saved");
        state.settings.gitPath           = s_gitPath;
        state.settings.defaultOpenDir    = s_defaultDir;
        state.settings.autoFetchEnabled  = s_autoFetch;
        state.settings.autoFetchIntervalMin = s_autoFetchInterval;
        s_initialized = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        LOG_INFO("Settings: cancelled");
        s_initialized = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void RenderNewBranchModal(AppState& state) {
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("New Branch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    static char s_name[128] = {};

    ImGui::Text("Create from: %s", state.currentBranch.empty() ? "(detached HEAD)" : state.currentBranch.c_str());
    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool pressedEnter = ImGui::InputTextWithHint("##newbranch", "Branch name", s_name, sizeof(s_name),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    bool valid = s_name[0] != '\0';
    if (!valid) ImGui::BeginDisabled();
    bool create = ImGui::Button("Create Branch", ImVec2(150, 0)) || (valid && pressedEnter);
    if (!valid) ImGui::EndDisabled();

    if (create) {
        LOG_INFO("NewBranchModal: creating branch '%s'", s_name);
        GitResult r = Git::CreateBranch(state.activeRepo, s_name);
        if (r.Ok()) {
            LOG_INFO("NewBranchModal: success");
            state.statusMessage.clear();
            Git::RefreshRepoState(state);
        } else {
            LOG_ERROR("NewBranchModal: failed — %s", r.err.c_str());
            state.statusMessage = "Could not create branch: " + r.err;
        }
        s_name[0] = '\0';
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        s_name[0] = '\0';
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void RenderDeleteBranchModal(AppState& state) {
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Delete Branch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    static int s_selected = -1;

    // Build list of local non-current branches
    std::vector<const BranchInfo*> deletable;
    for (const auto& b : state.branches)
        if (!b.isCurrent && b.location != BranchLocation::Remote)
            deletable.push_back(&b);

    ImGui::Text("Select a local branch to delete:");
    ImGui::Spacing();

    if (deletable.empty()) {
        ImGui::TextDisabled("No other local branches.");
    } else {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, 150));
        if (ImGui::BeginChild("##deletelist", ImVec2(0, 120), true)) {
            for (int i = 0; i < (int)deletable.size(); ++i) {
                bool sel = (s_selected == i);
                if (ImGui::Selectable(deletable[i]->name.c_str(), sel))
                    s_selected = i;
            }
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Safe delete only — fails if branch has unmerged commits.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool valid = s_selected >= 0 && s_selected < (int)deletable.size();
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Delete", ImVec2(100, 0))) {
        const std::string name = deletable[s_selected]->name;
        LOG_INFO("DeleteBranchModal: deleting '%s'", name.c_str());
        GitResult r = Git::DeleteBranch(state.activeRepo, name);
        if (r.Ok()) {
            LOG_INFO("DeleteBranchModal: success");
            state.statusMessage.clear();
            Git::RefreshRepoState(state);
        } else {
            LOG_ERROR("DeleteBranchModal: failed — %s", r.err.c_str());
            state.statusMessage = "Could not delete branch: " + r.err;
        }
        s_selected = -1;
        ImGui::CloseCurrentPopup();
    }
    if (!valid) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        s_selected = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// -------------------------------------------------------------------------
// Menu bar
// -------------------------------------------------------------------------
static void RenderMenuBar(AppState& state) {
    if (!ImGui::BeginMainMenuBar()) return;

    bool hasRepo = !state.activeRepo.empty();

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Repository...")) {
            LOG_INFO("Menu: File > Open Repository...");
            state.requestOpenRepo = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
            LOG_INFO("Menu: File > Exit");
            // TODO: set quit flag via platform
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Undo",  "Ctrl+Z", false, false);
        ImGui::MenuItem("Redo",  "Ctrl+Y", false, false);
        ImGui::Separator();
        ImGui::MenuItem("Cut",   "Ctrl+X", false, false);
        ImGui::MenuItem("Copy",  "Ctrl+C", false, false);
        ImGui::MenuItem("Paste", "Ctrl+V", false, false);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Changes", nullptr, state.navMode == NavMode::Changes)) {
            LOG_INFO("Menu: View > Changes");
            state.navMode = NavMode::Changes;
        }
        if (ImGui::MenuItem("History", nullptr, state.navMode == NavMode::History)) {
            LOG_INFO("Menu: View > History");
            state.navMode = NavMode::History;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Repository")) {
        if (ImGui::MenuItem("Open in Explorer", nullptr, false, hasRepo)) {
            LOG_INFO("Menu: Repository > Open in Explorer");
            state.requestOpenInExplorer = true;
        }
        if (ImGui::MenuItem("Open in Terminal", nullptr, false, hasRepo)) {
            LOG_INFO("Menu: Repository > Open in Terminal");
            state.requestOpenInTerminal = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...")) {
            LOG_INFO("Menu: Repository > Settings");
            state.showSettingsModal = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Branch")) {
        if (ImGui::MenuItem("New Branch...", nullptr, false, hasRepo)) {
            LOG_INFO("Menu: Branch > New Branch");
            state.showNewBranchModal = true;
        }
        if (ImGui::MenuItem("Delete Branch...", nullptr, false, hasRepo)) {
            LOG_INFO("Menu: Branch > Delete Branch");
            state.showDeleteBranchModal = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About Surume")) {
            LOG_INFO("Menu: Help > About");
            state.showAboutModal = true;
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// -------------------------------------------------------------------------
// Header bar
// -------------------------------------------------------------------------
static void RenderHeaderBar(AppState& state) {
    bool  hasRepo = !state.activeRepo.empty();
    float btnH    = ImGui::GetFrameHeight();
    float padY    = (kHeaderHeight - btnH) * 0.5f;
    float padX    = 8.0f;
    float winW    = ImGui::GetWindowWidth();

    ImGui::SetCursorPos(ImVec2(padX, padY));

    // Repo picker button
    {
        const char* label = state.activeRepoName.empty() ? "No Repository" : state.activeRepoName.c_str();
        char buf[160];
        std::snprintf(buf, sizeof(buf), " %s  v ##repobtn", label);
        if (ImGui::Button(buf))
            ImGui::OpenPopup("RepoPicker");

        if (ImGui::BeginPopup("RepoPicker")) {
            ImGui::TextDisabled("Recent Repositories");
            ImGui::Separator();
            if (state.recentRepos.empty()) {
                ImGui::TextDisabled("No recent repositories");
            } else {
                for (const auto& repo : state.recentRepos) {
                    // Show just the folder name but use full path for switching
                    size_t slash = repo.find_last_of("\\/");
                    std::string name = slash != std::string::npos ? repo.substr(slash + 1) : repo;
                    if (ImGui::Selectable(name.c_str())) {
                        LOG_INFO("RepoPicker: switching to '%s'", repo.c_str());
                        state.requestSwitchRepoPath = repo;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", repo.c_str());
                }
                ImGui::Separator();
            }
            if (ImGui::Selectable("  Open...")) {
                LOG_INFO("RepoPicker: Open... selected");
                state.requestOpenRepo = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::SameLine(0, 4.0f);

    // Branch picker button
    {
        if (!hasRepo) ImGui::BeginDisabled();

        const char* label = state.currentBranch.empty() ? "No Branch" : state.currentBranch.c_str();
        char buf[160];
        std::snprintf(buf, sizeof(buf), " %s  v ##branchbtn", label);
        if (ImGui::Button(buf))
            ImGui::OpenPopup("BranchPicker");

        if (ImGui::BeginPopup("BranchPicker")) {
            ImGui::TextDisabled("Switch Branch");
            ImGui::Separator();

            for (const auto& b : state.branches) {
                // Visual indicator prefix
                const char* indicator =
                    b.location == BranchLocation::Local          ? "  " :
                    b.location == BranchLocation::Remote         ? "  " :
                                                                   "  ";
                char entry[160];
                std::snprintf(entry, sizeof(entry), "%s%s%s",
                    indicator,
                    b.isCurrent ? "* " : "  ",
                    b.name.c_str());

                if (ImGui::Selectable(entry, b.isCurrent)) {
                    if (!b.isCurrent) {
                        LOG_INFO("BranchPicker: checking out '%s'", b.name.c_str());
                        GitResult r = Git::CheckoutBranch(state.activeRepo, b.name,
                            b.location == BranchLocation::Remote);
                        if (r.Ok()) {
                            LOG_INFO("BranchPicker: checkout succeeded");
                            state.statusMessage.clear();
                            Git::RefreshRepoState(state);
                        } else {
                            LOG_ERROR("BranchPicker: checkout failed — %s", r.err.c_str());
                            state.statusMessage = "Checkout failed: " + r.err;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            if (!state.branches.empty()) ImGui::Separator();
            if (ImGui::Selectable("  New Branch...")) {
                LOG_INFO("BranchPicker: New Branch selected");
                state.showNewBranchModal = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (!hasRepo) ImGui::EndDisabled();
    }

    // Sync button — right-aligned
    {
        const char* syncLabel = "Fetch origin";
        float syncW = ImGui::CalcTextSize(syncLabel).x + ImGui::GetStyle().FramePadding.x * 2 + 16.0f;
        ImGui::SetCursorPosY(padY);
        ImGui::SetCursorPosX(winW - syncW - padX);

        if (!hasRepo) ImGui::BeginDisabled();
        if (ImGui::Button(syncLabel, ImVec2(syncW, 0)))
            LOG_INFO("Header: sync button clicked (not yet implemented)");
        if (!hasRepo) ImGui::EndDisabled();
    }

    // Bottom separator
    ImVec2 p = ImGui::GetWindowPos();
    p.y += kHeaderHeight;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + winW, p.y),
        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
}

// -------------------------------------------------------------------------
// Changes tab
// -------------------------------------------------------------------------
static void RenderChangesTab(AppState& state) {
    bool    hasRepo  = !state.activeRepo.empty();
    ImVec2  avail    = ImGui::GetContentRegionAvail();
    float   listH    = avail.y - kCommitFooterHeight;

    ImGui::BeginChild("##filelist", ImVec2(avail.x, listH), false);
    if (!hasRepo) {
        // nothing
    } else if (state.changedFiles.empty()) {
        ImGui::TextDisabled("No local changes");
    } else {
        ImGuiStyle& style = ImGui::GetStyle();
        float       winW  = ImGui::GetContentRegionAvail().x;
        // Badge column wide enough for a single letter + padding
        float       tagW  = ImGui::CalcTextSize("M").x + style.ItemSpacing.x * 3;

        for (int i = 0; i < (int)state.changedFiles.size(); ++i) {
            const FileStatus& f = state.changedFiles[i];
            ImGui::PushID(i);

            // Badge
            char     badgeCh;
            ImVec4   badgeCol;
            const char* badgeTip;
            if (f.x == '?' && f.y == '?') {
                badgeCh = '?'; badgeCol = ImVec4(0.55f, 0.55f, 0.55f, 1); badgeTip = "Untracked";
            } else if (f.x == 'D' || f.y == 'D') {
                badgeCh = 'D'; badgeCol = ImVec4(0.9f, 0.3f, 0.3f, 1);   badgeTip = "Deleted";
            } else if (f.x == 'R') {
                badgeCh = 'R'; badgeCol = ImVec4(0.4f, 0.7f, 1.0f, 1);   badgeTip = "Renamed";
            } else if (f.x == 'A') {
                badgeCh = 'A'; badgeCol = ImVec4(0.3f, 0.85f, 0.3f, 1);  badgeTip = f.y == 'M' ? "New file (modified)" : "New file";
            } else {
                badgeCh = 'M'; badgeCol = ImVec4(0.9f, 0.7f, 0.2f, 1);   badgeTip = "Modified";
            }

            ImGui::TextColored(badgeCol, "%c", badgeCh);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", badgeTip);
            ImGui::SameLine(tagW);

            // Strip trailing slash
            std::string displayPath = f.path;
            if (!displayPath.empty() && (displayPath.back() == '/' || displayPath.back() == '\\'))
                displayPath.pop_back();

            // Selectable row — highlight if selected
            bool selected = (state.selectedFileIdx == i);
            float rowW = winW - tagW - style.ItemSpacing.x;
            if (ImGui::Selectable(displayPath.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns, ImVec2(rowW, 0))) {
                if (state.selectedFileIdx != i) {
                    state.selectedFileIdx = i;
                    state.requestDiff     = true;
                    LOG_INFO("File selected: '%s'", f.path.c_str());
                }
            }
            if (!f.oldPath.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("Renamed from: %s", f.oldPath.c_str());

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::BeginChild("##commitfooter", ImVec2(avail.x, kCommitFooterHeight), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float inputW = ImGui::GetContentRegionAvail().x;
    if (!hasRepo) ImGui::BeginDisabled();

    static char s_summary[256] = {};
    static char s_desc[1024]   = {};

    ImGui::SetNextItemWidth(inputW);
    ImGui::InputTextWithHint("##summary", "Summary (required)", s_summary, sizeof(s_summary));
    ImGui::Spacing();
    ImGui::InputTextMultiline("##desc", s_desc, sizeof(s_desc), ImVec2(inputW, 52.0f));
    ImGui::Spacing();

    bool canCommit = hasRepo && s_summary[0] != '\0';
    if (!canCommit) ImGui::BeginDisabled();

    char commitLabel[128];
    const char* br = state.currentBranch.empty() ? "branch" : state.currentBranch.c_str();
    std::snprintf(commitLabel, sizeof(commitLabel), "Commit to %s", br);

    if (ImGui::Button(commitLabel, ImVec2(inputW, 0))) {
        LOG_INFO("Commit: summary='%s' branch='%s'", s_summary, br);
        GitResult r = Git::Commit(state.activeRepo, s_summary, s_desc);
        if (r.Ok()) {
            LOG_INFO("Commit succeeded");
            s_summary[0] = '\0';
            s_desc[0]    = '\0';
            state.statusMessage.clear();
            Git::RefreshRepoState(state);
        } else {
            LOG_ERROR("Commit failed — %s", r.err.c_str());
            state.statusMessage = "Commit failed: " + r.err;
        }
    }

    if (!canCommit) ImGui::EndDisabled();
    if (!hasRepo)   ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// -------------------------------------------------------------------------
// History tab
// -------------------------------------------------------------------------
static void RenderHistoryTab(AppState& state) {
    ImGui::BeginChild("##historylist", ImGui::GetContentRegionAvail(), false);

    if (state.activeRepo.empty()) {
        ImGui::EndChild();
        return;
    }

    if (state.commits.empty()) {
        ImGui::TextDisabled("No commits on this branch.");
        ImGui::EndChild();
        return;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    float       padX  = style.ItemSpacing.x;
    float       winW  = ImGui::GetContentRegionAvail().x;

    // Reserve enough width for the widest relative date string
    float dateW = ImGui::CalcTextSize("12 months ago").x + padX * 2;

    for (int i = 0; i < (int)state.commits.size(); ++i) {
        const CommitInfo& c = state.commits[i];
        ImGui::PushID(i);

        // Row 1: hash (left) + date (right-aligned)
        ImGui::TextDisabled("%s", c.hash.c_str());
        ImGui::SameLine(winW - dateW);
        ImGui::TextDisabled("%s", c.date.c_str());

        // Row 2: subject — full width, wraps naturally
        ImGui::PushTextWrapPos(winW - padX);
        ImGui::TextUnformatted(c.subject.c_str());
        ImGui::PopTextWrapPos();

        // Row 3: author (dimmed)
        ImGui::TextDisabled("%s", c.author.c_str());

        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

// -------------------------------------------------------------------------
// Left column
// -------------------------------------------------------------------------
static void RenderLeftColumn(AppState& state) {
    static NavMode s_lastMode = NavMode::Changes;

    if (ImGui::BeginTabBar("##navtabs")) {
        if (ImGui::BeginTabItem("Changes")) {
            if (s_lastMode != NavMode::Changes) {
                LOG_INFO("Tab: switched to Changes");
                s_lastMode = NavMode::Changes;
            }
            RenderChangesTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("History")) {
            if (s_lastMode != NavMode::History) {
                LOG_INFO("Tab: switched to History");
                s_lastMode = NavMode::History;
            }
            RenderHistoryTab(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// -------------------------------------------------------------------------
// Right content
// -------------------------------------------------------------------------
static void RenderDiff(const std::string& diff) {
    // Scrollable diff view with horizontal scrollbar for long lines
    ImGui::BeginChild("##diffview", ImGui::GetContentRegionAvail(), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    if (diff.empty()) {
        ImGui::TextDisabled("(no diff output)");
        ImGui::EndChild();
        return;
    }

    float lineH = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Parse and render line by line
    const char* p   = diff.c_str();
    const char* end = p + diff.size();
    int lineIdx = 0;
    while (p < end) {
        const char* lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n') ++lineEnd;

        // Measure the line cursor Y before rendering
        float cursorY = ImGui::GetCursorScreenPos().y;

        char   firstCh = p < lineEnd ? p[0] : ' ';
        ImVec4 col;
        bool   hasBg   = false;
        ImU32  bgCol   = 0;

        if (firstCh == '+' && !(lineEnd - p > 2 && p[1] == '+' && p[2] == '+')) {
            col = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
            hasBg  = true;
            bgCol  = IM_COL32(30, 70, 30, 255);
        } else if (firstCh == '-' && !(lineEnd - p > 2 && p[1] == '-' && p[2] == '-')) {
            col = ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
            hasBg  = true;
            bgCol  = IM_COL32(70, 25, 25, 255);
        } else if (firstCh == '@') {
            col = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
        } else if (firstCh == 'd' || firstCh == 'i' || firstCh == 'B' ||
                   (firstCh == '-' || firstCh == '+')) {
            // diff --git / index / --- / +++ headers
            col = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        } else {
            col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }

        // Draw background highlight strip for + / - lines
        if (hasBg) {
            float x0 = winPos.x;
            float x1 = winPos.x + winSize.x + 8000.0f; // extend past scroll
            dl->AddRectFilled(ImVec2(x0, cursorY), ImVec2(x1, cursorY + lineH), bgCol);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(p, lineEnd);
        ImGui::PopStyleColor();

        p = lineEnd < end ? lineEnd + 1 : end;
        ++lineIdx;
    }

    ImGui::EndChild();
}

static void RenderRightContent(AppState& state) {
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (state.activeRepo.empty()) {
        float btnW = 200.0f;
        float btnH = ImGui::GetFrameHeight();
        ImGui::SetCursorPos(ImVec2((avail.x - btnW) * 0.5f, (avail.y - btnH * 2) * 0.5f));
        if (ImGui::Button("Open repository...", ImVec2(btnW, 0))) {
            LOG_INFO("Empty state: Open repository... clicked");
            state.requestOpenRepo = true;
        }
        float textW = ImGui::CalcTextSize("No repository open").x;
        ImGui::SetCursorPosX((avail.x - textW) * 0.5f);
        ImGui::TextDisabled("No repository open");
    } else if (state.selectedFileIdx < 0) {
        float textW = ImGui::CalcTextSize("Select a file to view its diff").x;
        ImGui::SetCursorPos(ImVec2((avail.x - textW) * 0.5f, avail.y * 0.45f));
        ImGui::TextDisabled("Select a file to view its diff");
    } else {
        RenderDiff(state.currentDiff);
    }

    // Status message bar at bottom
    if (!state.statusMessage.empty()) {
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetCursorPos(ImVec2(8.0f, avail.y - lineH - 4.0f));
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", state.statusMessage.c_str());
    }
}

// -------------------------------------------------------------------------
// Splitter
// -------------------------------------------------------------------------
static void RenderSplitter(AppState& state, float height) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##vsplitter", ImVec2(kSplitterWidth, height));

    if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.x;
        if (delta != 0.0f) {
            state.leftColumnWidth = std::clamp(state.leftColumnWidth + delta, kMinColumnWidth, kMaxColumnWidth);
        }
    }

    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImU32 col = ImGui::IsItemActive()  ? ImGui::GetColorU32(ImGuiCol_SeparatorActive)
              : ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered)
                                       : ImGui::GetColorU32(ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + kSplitterWidth, p0.y + height), col);
}

// -------------------------------------------------------------------------
// Main entry point
// -------------------------------------------------------------------------
void Gui::Render(AppState& state) {
    static bool s_firstFrame = true;
    if (s_firstFrame) {
        LOG_INFO("Gui::Render — first frame, display=%.0fx%.0f",
            ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        s_firstFrame = false;
    }

    // Open modals before rendering the main window so they work correctly
    if (state.showAboutModal)        { ImGui::OpenPopup("About Surume");  state.showAboutModal        = false; }
    if (state.showSettingsModal)     { ImGui::OpenPopup("Settings");         state.showSettingsModal     = false; }
    if (state.showNewBranchModal)    { ImGui::OpenPopup("New Branch");       state.showNewBranchModal    = false; }
    if (state.showDeleteBranchModal) { ImGui::OpenPopup("Delete Branch");    state.showDeleteBranchModal = false; }

    RenderAboutModal();
    RenderSettingsModal(state);
    RenderNewBranchModal(state);
    RenderDeleteBranchModal(state);

    RenderMenuBar(state);

    ImGuiIO& io    = ImGui::GetIO();
    float menuH    = ImGui::GetFrameHeight();
    float totalW   = io.DisplaySize.x;
    float totalH   = io.DisplaySize.y - menuH;
    float contentH = totalH - kHeaderHeight;
    float leftW    = state.leftColumnWidth;
    float rightW   = totalW - leftW - kSplitterWidth;

    ImGui::SetNextWindowPos(ImVec2(0, menuH));
    ImGui::SetNextWindowSize(ImVec2(totalW, totalH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));

    constexpr ImGuiWindowFlags kMainFlags =
        ImGuiWindowFlags_NoTitleBar        | ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            | ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##main", nullptr, kMainFlags);
    ImGui::PopStyleVar(3);

    RenderHeaderBar(state);

    ImGui::SetCursorPos(ImVec2(0, kHeaderHeight));

    ImGui::BeginChild("##leftcol", ImVec2(leftW, contentH), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    RenderLeftColumn(state);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    RenderSplitter(state, contentH);
    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##rightcol", ImVec2(rightW, contentH), false);
    RenderRightContent(state);
    ImGui::EndChild();

    ImGui::End();
}
