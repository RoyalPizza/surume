#include "App.h"
#include "Git.h"
#include "Persistence.h"
#include "Log.h"

namespace App {

void Init(AppState& state) {
    LOG_INFO("App::Init begin");

    // Detect git
    state.gitExePath = Git::FindExecutable();
    state.gitFound   = !state.gitExePath.empty();
    if (!state.gitFound)
        LOG_WARN("Git not found — repository actions will be disabled");

    // Load persistence
    Persistence::Load(state);

    // Validate and restore the last active repo
    if (!state.activeRepo.empty()) {
        if (!state.gitFound) {
            LOG_WARN("App::Init: skipping repo restore — git not found");
            state.activeRepo.clear();
        } else {
            LOG_INFO("App::Init: restoring last active repo '%s'", state.activeRepo.c_str());
            std::string root = Git::GetRepoRoot(state.activeRepo);
            if (root.empty()) {
                LOG_WARN("App::Init: last repo no longer valid — clearing");
                state.statusMessage = "Last repository is no longer available.";
                state.activeRepo.clear();
                state.activeRepoName.clear();
            } else {
                state.activeRepo = root;
                size_t slash = root.find_last_of("\\/");
                state.activeRepoName = slash != std::string::npos ? root.substr(slash + 1) : root;
                RefreshRepoState(state);
            }
        }
    }

    LOG_INFO("App::Init complete — gitFound=%d activeRepo='%s'",
        (int)state.gitFound, state.activeRepo.c_str());
}

void Tick(AppState& state) {
    // Per-frame background work: auto-fetch, job dispatch, etc.
}

void Shutdown(AppState& state) {
    LOG_INFO("App::Shutdown begin");
    Persistence::Save(state);
    LOG_INFO("App::Shutdown complete");
}

void RefreshRepoState(AppState& state) {
    if (state.activeRepo.empty()) return;
    LOG_INFO("App::RefreshRepoState repo='%s'", state.activeRepo.c_str());
    state.currentBranch   = Git::GetCurrentBranch(state.activeRepo);
    state.branches        = Git::GetBranches(state.activeRepo);
    state.changedFiles    = Git::GetStatus(state.activeRepo);
    state.commits         = Git::GetLog(state.activeRepo);
    state.selectedFileIdx = -1;
    state.currentDiff.clear();
    LOG_INFO("App::RefreshRepoState done — branch='%s' branches=%d commits=%d",
        state.currentBranch.c_str(), (int)state.branches.size(), (int)state.commits.size());
}

} // namespace App
