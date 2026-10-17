#pragma once

#include "model.hpp"

#include <string>
#include <vector>

struct GitResult
{
    int exit_code = -1;
    std::string output;       // stdout
    std::string error_output; // stderr
};

// Run git with args in repo_path (empty = current directory). Arguments are passed as a
// vector and quoted for the Windows command line; there is no shell. Text is UTF-8.
// Returns true if git ran and exited 0. result is filled either way.
bool git_run(const std::string& repo_path, const std::vector<std::string>& args,
    GitResult& result);

// Check that git can be run from PATH. Returns false if it can't; executable is set to the
// command used to run it.
bool git_find_executable(std::string& executable);

// Validate path as a repository root. Returns false if path isn't inside a work tree or is
// a subfolder of one. root is the normalized root with backslashes.
bool git_get_repo_root(const std::string& path, std::string& root);

// Read the current branch name. Returns false on detached HEAD or error.
bool git_get_current_branch(const std::string& repo_path, std::string& branch);

// List local branches plus remote branches that have no local counterpart.
// Returns false if the local branch query fails.
bool git_get_branches(const std::string& repo_path, std::vector<BranchInfo>& branches);

// Check out an existing branch. For a remote branch ("origin/x"), creates a local tracking
// branch. Returns false on failure; result holds git's error output.
bool git_checkout_branch(const std::string& repo_path, const std::string& name,
    bool is_remote, GitResult& result);

// Create a branch from HEAD and check it out. Returns false on failure.
bool git_create_branch(const std::string& repo_path, const std::string& name,
    GitResult& result);

// Safe-delete a local branch (fails if unmerged). Returns false on failure.
bool git_delete_branch(const std::string& repo_path, const std::string& name,
    GitResult& result);

// Commit staged changes. description may be empty. Returns false on failure.
bool git_commit(const std::string& repo_path, const std::string& summary,
    const std::string& description, GitResult& result);

// Read recent commits on the current branch, newest first. Returns false on error
// (including a repository with no commits yet).
bool git_get_log(const std::string& repo_path, std::vector<CommitInfo>& commits);

// Read index and working tree status for every changed file. Returns false on error.
bool git_get_status(const std::string& repo_path, std::vector<FileStatus>& files);

// Build a unified diff for one file: staged changes if any, otherwise working tree changes.
// Untracked files diff against an empty file. Returns false if git produced no diff.
bool git_get_diff(const std::string& repo_path, const FileStatus& file, std::string& diff);

// Read branch, branches, status and log into snapshot. Returns false if the status query
// fails (the repository is unusable); lists are left empty for any other query that fails.
bool git_load_snapshot(const std::string& repo_path, RepoSnapshot& snapshot);

// The message to show for a failed command: stderr, or stdout when stderr is empty (some
// failures, like "nothing to commit", are reported on stdout). Trimmed.
std::string git_error_text(const GitResult& result);
