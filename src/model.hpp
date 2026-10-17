#pragma once

#include <string>
#include <vector>

enum class NavMode
{
    CHANGES,
    HISTORY,
};

enum class BranchLocation
{
    LOCAL,
    REMOTE,
    LOCAL_AND_REMOTE,
};

struct BranchInfo
{
    std::string name;
    BranchLocation location = BranchLocation::LOCAL;
    bool is_current = false;
};

struct FileStatus
{
    std::string path;
    std::string old_path; // non-empty only for renames and copies
    char index_status = ' ';    // porcelain X: staged state
    char worktree_status = ' '; // porcelain Y: working tree state
};

struct CommitInfo
{
    std::string hash;    // short hash
    std::string author;
    std::string date;    // relative, e.g. "3 hours ago"
    std::string subject; // first line of the message
};

struct AppSettings
{
    std::string git_path;
    std::string default_open_directory;
    bool is_auto_fetch_enabled = false;
    int auto_fetch_interval_minutes = 15;
};

// Everything Git reported for the active repository on the last refresh. Only applying a
// job result writes this; it is replaced wholesale, never edited in place.
struct RepoSnapshot
{
    std::string current_branch; // empty on detached HEAD
    std::vector<BranchInfo> branches;
    std::vector<FileStatus> files;
    std::vector<CommitInfo> commits;
};

// What the GUI asks for. main.cpp turns requests into jobs, dialogs or shell actions after
// the frame is presented; the GUI never runs Git itself.
enum class RequestKind
{
    OPEN_REPO_DIALOG,
    SWITCH_REPO,      // text: repository path
    OPEN_IN_EXPLORER,
    OPEN_IN_TERMINAL,
    REFRESH,
    LOAD_DIFF,        // text: file path
    COMMIT,           // text: summary, text2: description
    CHECKOUT_BRANCH,  // text: branch name, flag: is a remote branch
    CREATE_BRANCH,    // text: branch name
    DELETE_BRANCH,    // text: branch name
    SAVE_SETTINGS,
    EXIT,
};

struct Request
{
    RequestKind kind = RequestKind::REFRESH;
    std::string text;
    std::string text2;
    bool flag = false;
};

struct AppState
{
    // Active repository (written by main.cpp when an open-repository job succeeds)
    std::string active_repo;
    std::string active_repo_name;
    int repo_generation = 0; // bumped on every switch; results for older generations are dropped
    std::vector<std::string> recent_repos;
    RepoSnapshot repo;

    // View
    NavMode nav_mode = NavMode::CHANGES;
    std::string selected_file_path; // empty when no file is selected
    std::string current_diff;       // diff of selected_file_path, once loaded
    float left_column_width = 280.0f; // unscaled; persisted

    // Background work (refreshed from the jobs module every frame)
    bool is_busy = false;            // a job is queued or running
    bool is_mutation_running = false; // a job that changes the repository is queued or running
    std::string busy_label;          // what the running job is doing, for the GUI
    bool has_commit_succeeded = false; // set when a commit lands; the GUI clears its fields

    // Settings (persisted)
    AppSettings settings;

    // Git
    bool is_git_found = false;
    std::string git_executable;

    // Requests from the GUI, handled in main.cpp after the frame is presented
    std::vector<Request> requests;

    // Modal open requests (set by the GUI, cleared once the popup is opened)
    bool is_new_branch_modal_requested = false;
    bool is_delete_branch_modal_requested = false;
    bool is_about_modal_requested = false;
    bool is_settings_modal_requested = false;

    // Last error shown to the user; empty when there is nothing to show
    std::string status_message;
};
