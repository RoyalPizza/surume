#pragma once
#include <string>
#include <vector>

enum class NavMode { Changes, History };

enum class BranchLocation { Local, Remote, LocalAndRemote };

struct BranchInfo {
    std::string    name;
    BranchLocation location  = BranchLocation::Local;
    bool           isCurrent = false;
};

struct FileStatus {
    std::string path;
    std::string oldPath; // non-empty only for renames
    char        x = ' '; // index (staged) status
    char        y = ' '; // worktree status
};

struct CommitInfo {
    std::string hash;    // short hash
    std::string author;
    std::string date;    // relative (e.g. "3 hours ago")
    std::string subject; // first line of message
};

struct AppSettings {
    std::string gitPath;
    std::string defaultOpenDir;
    bool        autoFetchEnabled     = false;
    int         autoFetchIntervalMin = 15;
};

struct AppState {
    // Repository
    std::string              activeRepo;
    std::string              activeRepoName;
    std::vector<std::string> recentRepos;

    // Branch
    std::string             currentBranch;
    std::vector<BranchInfo> branches;

    // Changes
    std::vector<FileStatus> changedFiles;
    int         selectedFileIdx = -1; // index into changedFiles
    bool        requestDiff     = false;
    std::string currentDiff;

    // History
    std::vector<CommitInfo> commits;

    // Navigation
    NavMode navMode = NavMode::Changes;

    // UI layout (persisted)
    float leftColumnWidth = 280.0f;

    // Settings (persisted)
    AppSettings settings;

    // Git
    bool        gitFound    = false;
    std::string gitExePath;

    // Deferred platform actions (resolved in main loop after Present)
    bool        requestOpenRepo       = false;
    std::string requestSwitchRepoPath;

    // Modal visibility flags (set in UI, cleared after OpenPopup)
    bool showNewBranchModal    = false;
    bool showDeleteBranchModal = false;
    bool showAboutModal        = false;
    bool showSettingsModal     = false;

    // Status / error display
    std::string statusMessage;
};
