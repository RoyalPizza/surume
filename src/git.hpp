#pragma once
#include "model.hpp"
#include <string>
#include <vector>

struct GitResult {
    int         exitCode = -1;
    std::string out;
    std::string err;
    bool Ok() const { return exitCode == 0; }
};

namespace Git {
    // Run a git command in repoPath with the given args (no shell expansion).
    GitResult Run(const std::string& repoPath, std::vector<std::string> args);

    // Check if git is available. Returns "git" if found in PATH, empty otherwise.
    std::string FindGitExecutable();

    // Validate path as a repo root. Returns canonical root (backslashes) or empty.
    std::string GetRepoRoot(const std::string& path);

    // Returns current branch name, or empty on detached HEAD or error.
    std::string GetCurrentBranch(const std::string& repoPath);

    // Returns all local and remote branches.
    std::vector<BranchInfo> GetBranches(const std::string& repoPath);

    // Checkout an existing branch. Pass isRemote=true to auto-create a local tracking branch.
    GitResult CheckoutBranch(const std::string& repoPath, const std::string& name, bool isRemote);

    // Create a new branch from HEAD and check it out.
    GitResult CreateBranch(const std::string& repoPath, const std::string& name);

    // Delete a local branch (safe delete — fails if unmerged).
    GitResult DeleteBranch(const std::string& repoPath, const std::string& name);

    // Commit staged changes.
    GitResult Commit(const std::string& repoPath, const std::string& summary, const std::string& description);

    // Return up to maxCount commits on the current branch.
    std::vector<CommitInfo> GetLog(const std::string& repoPath, int maxCount = 100);

    // Return working tree and index status for all changed files.
    std::vector<FileStatus> GetStatus(const std::string& repoPath);

    // Return unified diff for a single file.
    std::string GetDiff(const std::string& repoPath, const FileStatus& file);

    // Re-read branch, branches, status, and log for state.activeRepo. Clears the file selection.
    void RefreshRepoState(AppState& state);
}
