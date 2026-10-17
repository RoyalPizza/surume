#pragma once

#include "model.hpp"

#include <string>
#include <vector>

// Git work that runs on the worker thread. Mutating kinds end with a fresh snapshot.
enum class JobKind
{
    FIND_GIT,        // check that git runs
    OPEN_REPO,       // text: path to validate and load; text2: message if it isn't a repo
    REFRESH,
    LOAD_DIFF,       // file: the file to diff
    LOAD_COMMIT,     // text: commit hash
    LOAD_COMMIT_DIFF, // text: commit hash, file: the file in that commit
    COMMIT,          // paths: files to commit, text: summary, text2: description
    CHECKOUT_BRANCH, // text: branch name, flag: is a remote branch
    CREATE_BRANCH,   // text: branch name
    DELETE_BRANCH,   // text: branch name
    DISCARD,         // files: the changes to throw away
};

struct Job
{
    JobKind kind = JobKind::REFRESH;
    int generation = 0;    // AppState::repo_generation when queued
    std::string repo_path; // empty for FIND_GIT
    std::string text;
    std::string text2;
    bool flag = false;
    FileStatus file;
    std::vector<std::string> paths;
    std::vector<FileStatus> files;
};

struct JobResult
{
    JobKind kind = JobKind::REFRESH;
    int generation = 0;
    std::string repo_path; // for OPEN_REPO: the validated root
    bool is_success = false;
    std::string error;     // user-facing; empty on success
    bool has_snapshot = false;
    RepoSnapshot snapshot;
    std::string diff;
    std::string diff_path; // file the diff belongs to
    std::string commit_hash; // for LOAD_COMMIT and LOAD_COMMIT_DIFF
    CommitDetails commit_details;
    std::string git_executable; // for FIND_GIT
};

// Start the worker thread. Returns false if the thread or its event couldn't be created.
bool jobs_start();

// Ask the worker to finish its current job and exit, and wait for it. Queued jobs are dropped.
void jobs_stop();

// Queue a job. A REFRESH for a repository that already has one waiting is dropped.
void jobs_push(const Job& job);

// Take the oldest finished result. Returns false when there is none.
bool jobs_pop_result(JobResult& result);

// Event the worker signals when a result is ready, for MsgWaitForMultipleObjects. A Win32
// HANDLE, typed void* to keep <windows.h> out of this header.
void* jobs_wake_event();

// Report whether jobs are queued or running, whether any of them changes the repository, and
// a label for the running one. Returns false when the worker is idle.
bool jobs_get_status(bool& is_mutation_pending, std::string& running_label);
