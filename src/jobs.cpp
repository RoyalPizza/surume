#include "jobs.hpp"

#include "git.hpp"
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

// Single worker; the queue and results are shared with the UI thread under one lock
static HANDLE worker_thread = nullptr;
static HANDLE wake_event = nullptr;
static SRWLOCK lock = SRWLOCK_INIT;
static CONDITION_VARIABLE queue_changed = CONDITION_VARIABLE_INIT;
static std::vector<Job> queue;
static std::vector<JobResult> results;
static bool is_running_job = false;
static Job running_job;
static bool is_stop_requested = false;

static bool is_mutation(JobKind kind)
{
    switch (kind)
    {
        case JobKind::COMMIT:
        case JobKind::CHECKOUT_BRANCH:
        case JobKind::CREATE_BRANCH:
        case JobKind::DELETE_BRANCH:
        case JobKind::DISCARD:
        case JobKind::STASH:
        case JobKind::STASH_AND_CHECKOUT:
        case JobKind::STASH_POP:
        case JobKind::STASH_DROP:
            return true;
        case JobKind::FIND_GIT:
        case JobKind::OPEN_REPO:
        case JobKind::REFRESH:
        case JobKind::LOAD_DIFF:
        case JobKind::LOAD_COMMIT:
        case JobKind::LOAD_COMMIT_DIFF:
        case JobKind::LOAD_LAST_COMMIT:
        case JobKind::LOAD_STASH_DIFF:
            return false;
    }
    return false;
}

static const char* job_label(JobKind kind)
{
    switch (kind)
    {
        case JobKind::FIND_GIT:
            return "Looking for Git";
        case JobKind::OPEN_REPO:
            return "Opening repository";
        case JobKind::REFRESH:
            return "Refreshing";
        case JobKind::LOAD_DIFF:
        case JobKind::LOAD_COMMIT_DIFF:
            return "Loading diff";
        case JobKind::LOAD_COMMIT:
        case JobKind::LOAD_LAST_COMMIT:
            return "Loading commit";
        case JobKind::COMMIT:
            return "Committing";
        case JobKind::CHECKOUT_BRANCH:
            return "Switching branch";
        case JobKind::CREATE_BRANCH:
            return "Creating branch";
        case JobKind::DELETE_BRANCH:
            return "Deleting branch";
        case JobKind::DISCARD:
            return "Discarding changes";
        case JobKind::STASH:
        case JobKind::STASH_AND_CHECKOUT:
            return "Stashing";
        case JobKind::STASH_POP:
            return "Restoring stash";
        case JobKind::STASH_DROP:
            return "Discarding stash";
        case JobKind::LOAD_STASH_DIFF:
            return "Loading stash";
    }
    return "Working";
}

// Run a mutation's command, then reload the snapshot whether or not it succeeded.
static void finish_mutation(const Job& job, bool is_success, const GitResult& git_result,
    JobResult& result)
{
    result.is_success = is_success;
    if (!is_success)
    {
        result.error = git_error_text(git_result);
    }
    result.has_snapshot = git_load_snapshot(job.repo_path, result.snapshot);
}

static void run_open_repo(const Job& job, JobResult& result)
{
    std::string root;
    if (!git_get_repo_root(job.text, root))
    {
        result.error = job.text2;
        return;
    }

    result.repo_path = root;
    result.has_snapshot = git_load_snapshot(root, result.snapshot);
    result.is_success = result.has_snapshot;
    if (!result.is_success)
    {
        result.error = "Could not read the repository status: " + root;
    }
}

static void run_job(const Job& job, JobResult& result)
{
    result.kind = job.kind;
    result.generation = job.generation;
    result.repo_path = job.repo_path;

    GitResult git_result;
    switch (job.kind)
    {
        case JobKind::FIND_GIT:
            result.is_success = git_find_executable(result.git_executable);
            if (!result.is_success)
            {
                result.error = "Git is not installed or could not be found on PATH.";
            }
            break;

        case JobKind::OPEN_REPO:
            run_open_repo(job, result);
            break;

        case JobKind::REFRESH:
            result.has_snapshot = git_load_snapshot(job.repo_path, result.snapshot);
            result.is_success = result.has_snapshot;
            if (!result.is_success)
            {
                result.error = "Could not read the repository status.";
            }
            break;

        case JobKind::LOAD_DIFF:
            result.diff_path = job.file.path;
            result.is_success = git_get_diff(job.repo_path, job.file, result.diff);
            break;

        case JobKind::LOAD_COMMIT:
            result.commit_hash = job.text;
            result.is_success = git_get_commit_details(job.repo_path, job.text,
                result.commit_details);
            break;

        case JobKind::LOAD_COMMIT_DIFF:
            result.commit_hash = job.text;
            result.diff_path = job.file.path;
            result.is_success = git_get_commit_file_diff(job.repo_path, job.text, job.file,
                result.diff);
            break;

        case JobKind::LOAD_LAST_COMMIT:
            result.is_success = git_get_last_commit(job.repo_path, result.message,
                result.is_pushed);
            break;

        case JobKind::COMMIT:
        {
            bool is_success = git_commit_paths(job.repo_path, job.paths, job.text, job.text2,
                job.flag, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::CHECKOUT_BRANCH:
        {
            bool is_success = git_checkout_branch(job.repo_path, job.text, job.flag, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::CREATE_BRANCH:
        {
            bool is_success = git_create_branch(job.repo_path, job.text, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::DELETE_BRANCH:
        {
            bool is_success = git_delete_branch(job.repo_path, job.text, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::DISCARD:
        {
            bool is_success = git_discard(job.repo_path, job.files, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::STASH:
        {
            bool is_success = git_stash_push(job.repo_path, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::STASH_AND_CHECKOUT:
        {
            bool is_success = git_stash_push(job.repo_path, git_result) &&
                git_checkout_branch(job.repo_path, job.text, job.flag, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::STASH_POP:
        {
            bool is_success = git_stash_pop(job.repo_path, job.text, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::STASH_DROP:
        {
            bool is_success = git_stash_drop(job.repo_path, job.text, git_result);
            finish_mutation(job, is_success, git_result, result);
            break;
        }

        case JobKind::LOAD_STASH_DIFF:
            result.diff_path = job.text;
            result.is_success = git_get_stash_diff(job.repo_path, job.text, result.diff);
            break;
    }
}

static DWORD WINAPI worker_main(LPVOID)
{
    while (true)
    {
        Job job;
        AcquireSRWLockExclusive(&lock);
        while (queue.empty() && !is_stop_requested)
        {
            SleepConditionVariableSRW(&queue_changed, &lock, INFINITE, 0);
        }
        if (is_stop_requested)
        {
            ReleaseSRWLockExclusive(&lock);
            return 0;
        }
        job = queue.front();
        queue.erase(queue.begin());
        running_job = job;
        is_running_job = true;
        ReleaseSRWLockExclusive(&lock);

        LOG_INFO("job start: %s", job_label(job.kind));
        JobResult result;
        run_job(job, result);
        LOG_INFO("job done: %s ok=%d", job_label(job.kind), (int)result.is_success);

        AcquireSRWLockExclusive(&lock);
        results.push_back(result);
        is_running_job = false;
        ReleaseSRWLockExclusive(&lock);
        SetEvent(wake_event);
    }
}

bool jobs_start()
{
    wake_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (wake_event == nullptr)
    {
        LOG_ERROR("jobs_start: CreateEvent failed: %u", (unsigned)GetLastError());
        return false;
    }

    worker_thread = CreateThread(nullptr, 0, worker_main, nullptr, 0, nullptr);
    if (worker_thread == nullptr)
    {
        LOG_ERROR("jobs_start: CreateThread failed: %u", (unsigned)GetLastError());
        CloseHandle(wake_event);
        wake_event = nullptr;
        return false;
    }
    return true;
}

void jobs_stop()
{
    if (worker_thread == nullptr)
    {
        return;
    }

    AcquireSRWLockExclusive(&lock);
    is_stop_requested = true;
    queue.clear();
    ReleaseSRWLockExclusive(&lock);
    WakeConditionVariable(&queue_changed);

    WaitForSingleObject(worker_thread, INFINITE);
    CloseHandle(worker_thread);
    CloseHandle(wake_event);
    worker_thread = nullptr;
    wake_event = nullptr;
}

void jobs_push(const Job& job)
{
    AcquireSRWLockExclusive(&lock);
    bool is_duplicate = false;
    if (job.kind == JobKind::REFRESH)
    {
        for (const Job& queued : queue)
        {
            if (queued.kind == JobKind::REFRESH && queued.repo_path == job.repo_path)
            {
                is_duplicate = true;
            }
        }
    }
    if (!is_duplicate)
    {
        queue.push_back(job);
    }
    ReleaseSRWLockExclusive(&lock);
    WakeConditionVariable(&queue_changed);
}

bool jobs_pop_result(JobResult& result)
{
    AcquireSRWLockExclusive(&lock);
    bool has_result = !results.empty();
    if (has_result)
    {
        result = results.front();
        results.erase(results.begin());
    }
    ReleaseSRWLockExclusive(&lock);
    return has_result;
}

void* jobs_wake_event()
{
    return wake_event;
}

bool jobs_get_status(bool& is_mutation_pending, std::string& running_label)
{
    AcquireSRWLockShared(&lock);
    bool is_busy = is_running_job || !queue.empty();
    is_mutation_pending = is_running_job && is_mutation(running_job.kind);
    for (const Job& queued : queue)
    {
        is_mutation_pending = is_mutation_pending || is_mutation(queued.kind);
    }
    running_label = is_running_job ? job_label(running_job.kind) : "";
    ReleaseSRWLockShared(&lock);
    return is_busy;
}
