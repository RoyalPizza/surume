#pragma once

#include <string>
#include <unordered_map>
#include <vector>

constexpr int COMMIT_SUMMARY_SIZE = 256;
constexpr int COMMIT_DESCRIPTION_SIZE = 16384;
constexpr int FILE_FILTER_SIZE = 256;

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
    std::string upstream; // e.g. "origin/main"; empty when the branch has none
    BranchLocation location = BranchLocation::LOCAL;
    bool is_current = false;
};

// The checked-out branch and how it compares with its upstream.
struct BranchStatus
{
    std::string name;     // empty on detached HEAD
    std::string upstream; // empty when the branch has no upstream
    int ahead = 0;        // commits on the branch that the upstream doesn't have
    int behind = 0;       // commits on the upstream that the branch doesn't have
};

struct FileStatus
{
    std::string path;
    std::string old_path; // non-empty only for renames and copies
    char index_status = ' ';    // porcelain X: staged state
    char worktree_status = ' '; // porcelain Y: working tree state
    bool is_conflicted = false; // unmerged: a merge, rebase or stash pop left conflicts
};

enum class DiffLineKind
{
    ADDED,
    REMOVED,
    HUNK,
    HEADER,  // "diff --git", "index", "---"/"+++", "Binary files"
    CONTEXT,
};

// One line of a diff, as a range into the diff text. Parsed once when the diff loads.
struct DiffLine
{
    int start = 0;
    int length = 0;
    DiffLineKind kind = DiffLineKind::CONTEXT;
};

struct CommitInfo
{
    std::string hash;      // short hash
    std::string full_hash;
    std::string author;
    std::string date;      // relative, e.g. "3 hours ago"
    std::string subject;   // first line of the message
};

// Everything shown for one commit in History. files use index_status for the change letter.
struct CommitDetails
{
    std::string full_hash;
    std::string author;
    std::string email;
    std::string date;    // absolute, "2026-10-17 14:30"
    std::string message; // full message
    std::vector<FileStatus> files;
};

struct AppSettings
{
    std::string git_path;
    std::string default_open_directory;
    bool is_auto_fetch_enabled = false;
    int auto_fetch_interval_minutes = 15;
};

// A loaded diff, split into lines for drawing.
struct DiffView
{
    std::string text;
    std::vector<DiffLine> lines;
    int max_columns = 0;             // widest line, for the horizontal scroll range
    bool is_large_confirmed = false; // user chose to show a diff over the size limit
};

// Everything Git reported for the active repository on the last refresh. Only applying a
// job result writes this; it is replaced wholesale, never edited in place.
struct RepoSnapshot
{
    BranchStatus head;
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
    LOAD_COMMIT,      // uses selected_commit_hash
    LOAD_COMMIT_DIFF, // uses selected_commit_hash and selected_commit_file
    COMMIT,           // uses the ticked files and commit text in AppState
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
    std::string restore_repo; // last active repository from settings, until it reopens or fails
    RepoSnapshot repo;

    // Next commit (written by the GUI; cleared on repository switch)
    std::unordered_map<std::string, bool> unticked_paths; // files left out; new files start ticked
    char commit_summary[COMMIT_SUMMARY_SIZE] = {};
    char commit_description[COMMIT_DESCRIPTION_SIZE] = {};

    // View
    NavMode nav_mode = NavMode::CHANGES;
    char file_filter[FILE_FILTER_SIZE] = {}; // case-insensitive path filter for the file list
    std::string selected_file_path; // empty when no file is selected
    DiffView diff;                  // diff of selected_file_path, once loaded

    // History view
    std::string selected_commit_hash; // full hash; empty when no commit is selected
    CommitDetails commit_details;     // of selected_commit_hash, once loaded
    std::string selected_commit_file; // path of the file shown from the commit
    DiffView commit_diff;             // that file's diff in the commit
    float left_column_width = 280.0f; // unscaled; persisted

    // Background work (refreshed from the jobs module every frame)
    bool is_busy = false;            // a job is queued or running
    bool is_mutation_running = false; // a job that changes the repository is queued or running
    std::string busy_label;          // what the running job is doing, for the GUI

    // Settings (persisted)
    AppSettings settings;

    // Git
    bool is_git_checked = false; // the startup check for git has finished
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
