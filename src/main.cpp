#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>

#include "Log.h"
#include "Platform.h"
#include "App.h"
#include "Git.h"
#include "Ui.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    Log::Init();
    LOG_INFO("=== Gitaluna starting ===");

    PlatformContext platform = {};
    AppState        state    = {};

    LOG_INFO("Calling Platform::Init (1280x800)");
    if (!Platform::Init(platform, "Gitaluna", 1280, 800)) {
        LOG_ERROR("Platform::Init failed — aborting");
        Log::Shutdown();
        return 1;
    }
    LOG_INFO("Platform::Init succeeded");

    LOG_INFO("Calling App::Init");
    App::Init(state);
    LOG_INFO("App::Init complete — gitFound=%d", (int)state.gitFound);

    LOG_INFO("Entering main loop");
    int frameCount = 0;

    while (!platform.wantsQuit) {
        Platform::PumpMessages(platform);
        if (platform.wantsQuit) {
            LOG_INFO("WM_QUIT received — exiting loop");
            break;
        }

        Platform::HandleResize(platform);
        Platform::NewFrame();

        App::Tick(state);
        Ui::Render(state, platform);

        Platform::Present(platform);

        ++frameCount;
        if (frameCount % 600 == 0)
            LOG_DEBUG("Heartbeat — frame %d, activeRepo='%s', branch='%s'",
                frameCount, state.activeRepo.c_str(), state.currentBranch.c_str());

        if (state.requestOpenRepo) {
            state.requestOpenRepo = false;
            LOG_INFO("Open repository dialog requested");
            std::string path = Platform::OpenFolderDialog(platform.hwnd, "Open Repository");
            if (path.empty()) {
                LOG_INFO("Open repository dialog cancelled or failed");
            } else {
                LOG_INFO("User selected path: '%s'", path.c_str());
                std::string root = Git::GetRepoRoot(path);
                if (root.empty()) {
                    LOG_WARN("Path is not a valid git repo root — ignoring");
                    state.statusMessage = "Not a valid Git repository root.";
                } else {
                    state.activeRepo = root;
                    size_t slash = root.find_last_of("\\/");
                    state.activeRepoName = (slash != std::string::npos) ? root.substr(slash + 1) : root;
                    state.recentRepos.erase(
                        std::remove(state.recentRepos.begin(), state.recentRepos.end(), root),
                        state.recentRepos.end());
                    state.recentRepos.insert(state.recentRepos.begin(), root);
                    state.statusMessage.clear();
                    App::RefreshRepoState(state);
                    LOG_INFO("Repo opened — name='%s' branch='%s'",
                        state.activeRepoName.c_str(), state.currentBranch.c_str());
                }
            }
        }

        if (state.requestDiff) {
            state.requestDiff = false;
            if (state.selectedFileIdx >= 0 && state.selectedFileIdx < (int)state.changedFiles.size()) {
                const FileStatus& f = state.changedFiles[state.selectedFileIdx];
                LOG_INFO("Fetching diff for '%s'", f.path.c_str());
                state.currentDiff = Git::GetDiff(state.activeRepo, f);
                LOG_INFO("Diff fetched — %d bytes", (int)state.currentDiff.size());
            }
        }

        if (!state.requestSwitchRepoPath.empty()) {
            std::string path = std::move(state.requestSwitchRepoPath);
            state.requestSwitchRepoPath.clear();
            LOG_INFO("Switch repo requested: '%s'", path.c_str());
            std::string root = Git::GetRepoRoot(path);
            if (root.empty()) {
                LOG_WARN("requestSwitchRepoPath: not a valid git repo — ignoring");
                state.statusMessage = "Repository no longer available: " + path;
            } else {
                state.activeRepo = root;
                size_t slash = root.find_last_of("\\/");
                state.activeRepoName = (slash != std::string::npos) ? root.substr(slash + 1) : root;
                state.recentRepos.erase(
                    std::remove(state.recentRepos.begin(), state.recentRepos.end(), root),
                    state.recentRepos.end());
                state.recentRepos.insert(state.recentRepos.begin(), root);
                state.statusMessage.clear();
                App::RefreshRepoState(state);
                LOG_INFO("Repo switched — name='%s' branch='%s'",
                    state.activeRepoName.c_str(), state.currentBranch.c_str());
            }
        }
    }

    LOG_INFO("Main loop exited after %d frames", frameCount);

    LOG_INFO("Calling App::Shutdown");
    App::Shutdown(state);

    LOG_INFO("Calling Platform::Shutdown");
    Platform::Shutdown(platform);

    LOG_INFO("=== Gitaluna exiting cleanly ===");
    Log::Shutdown();
    return 0;
}
