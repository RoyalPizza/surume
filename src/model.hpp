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

struct AppState
{
    // Repository
    std::string active_repo;
    std::string active_repo_name;
    std::vector<std::string> recent_repos;

    // Branch
    std::string current_branch;
    std::vector<BranchInfo> branches;

    // Changes
    std::vector<FileStatus> changed_files;
    int selected_file_index = -1; // index into changed_files
    std::string current_diff;

    // History
    std::vector<CommitInfo> commits;

    // Navigation
    NavMode nav_mode = NavMode::CHANGES;

    // Layout (persisted)
    float left_column_width = 280.0f;

    // Settings (persisted)
    AppSettings settings;

    // Git
    bool is_git_found = false;
    std::string git_executable;

    // Requests from the GUI, handled in main.cpp after the frame is presented
    bool is_open_repo_requested = false;
    bool is_exit_requested = false;
    bool is_save_requested = false; // settings or recent repos changed
    bool is_open_in_explorer_requested = false;
    bool is_open_in_terminal_requested = false;
    bool is_diff_requested = false;
    std::string switch_repo_path; // non-empty requests a repository switch

    // Modal open requests (set by the GUI, cleared once the popup is opened)
    bool is_new_branch_modal_requested = false;
    bool is_delete_branch_modal_requested = false;
    bool is_about_modal_requested = false;
    bool is_settings_modal_requested = false;

    // Last error shown to the user; empty when there is nothing to show
    std::string status_message;
};
