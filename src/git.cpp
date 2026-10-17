#include "git.hpp"

#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

constexpr DWORD RUN_TIMEOUT_MILLISECONDS = 15000;
constexpr DWORD POLL_INTERVAL_MILLISECONDS = 10;
constexpr DWORD PIPE_READ_SIZE = 4096;
constexpr int MAX_LOG_COMMITS = 100;
constexpr char LOG_FIELD_SEPARATOR = '\x1f';
constexpr size_t STATUS_PATH_OFFSET = 3; // "XY path"

// Convert UTF-8 to a null-terminated UTF-16 buffer for Win32 W functions.
static std::vector<wchar_t> to_wide(const std::string& text)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0)
    {
        return std::vector<wchar_t>(1, L'\0');
    }

    std::vector<wchar_t> wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), len);
    return wide;
}

// Append one argument using the Windows command-line quoting rules, so git's argument
// parser gives back exactly the original text. Backslashes are only special before a quote.
static void append_quoted_argument(std::string& command_line, const std::string& argument)
{
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos)
    {
        command_line += argument;
        return;
    }

    command_line += '"';
    size_t backslash_count = 0;
    for (char c : argument)
    {
        if (c == '\\')
        {
            ++backslash_count;
            continue;
        }

        if (c == '"')
        {
            command_line.append(backslash_count * 2 + 1, '\\');
        }
        else
        {
            command_line.append(backslash_count, '\\');
        }
        command_line += c;
        backslash_count = 0;
    }
    command_line.append(backslash_count * 2, '\\');
    command_line += '"';
}

static void drain_pipe(HANDLE pipe, std::string& destination)
{
    char buf[PIPE_READ_SIZE];
    DWORD available = 0;
    while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
    {
        DWORD to_read = available < PIPE_READ_SIZE ? available : PIPE_READ_SIZE;
        DWORD read_count = 0;
        if (!ReadFile(pipe, buf, to_read, &read_count, nullptr) || read_count == 0)
        {
            break;
        }
        destination.append(buf, read_count);
    }
}

static std::string trim_end(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
    {
        text.pop_back();
    }
    return text;
}

static std::string to_backslashes(std::string path)
{
    for (char& c : path)
    {
        if (c == '/')
        {
            c = '\\';
        }
    }
    return path;
}

// Split text on separator, keeping empty pieces and stripping a trailing '\r' from each.
static std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> pieces;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(separator, start);
        if (end == std::string::npos)
        {
            end = text.size();
        }

        std::string piece = text.substr(start, end - start);
        if (!piece.empty() && piece.back() == '\r')
        {
            piece.pop_back();
        }
        pieces.push_back(piece);
        start = end + 1;
    }
    return pieces;
}

// -------------------------------------------------------------------------

bool git_run(const std::string& repo_path, const std::vector<std::string>& args,
    GitResult& result)
{
    result = {};

    // quotepath=false keeps non-ASCII paths as UTF-8 instead of octal escapes
    std::string command_line = "git -c core.quotepath=false";
    for (const std::string& arg : args)
    {
        command_line += ' ';
        append_quoted_argument(command_line, arg);
    }
    LOG_DEBUG("git_run cwd='%s' cmd='%s'", repo_path.c_str(), command_line.c_str());

    SECURITY_ATTRIBUTES security = { sizeof(security), nullptr, TRUE };
    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    HANDLE error_read = nullptr;
    HANDLE error_write = nullptr;
    if (!CreatePipe(&out_read, &out_write, &security, 0))
    {
        LOG_ERROR("git_run: CreatePipe failed: %u", (unsigned)GetLastError());
        result.error_output = "CreatePipe failed";
        return false;
    }
    if (!CreatePipe(&error_read, &error_write, &security, 0))
    {
        LOG_ERROR("git_run: CreatePipe failed: %u", (unsigned)GetLastError());
        CloseHandle(out_read);
        CloseHandle(out_write);
        result.error_output = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(error_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup_info = {};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = out_write;
    startup_info.hStdError = error_write;

    std::vector<wchar_t> wide_command_line = to_wide(command_line);
    std::vector<wchar_t> wide_directory = to_wide(repo_path);
    const wchar_t* directory = repo_path.empty() ? nullptr : wide_directory.data();

    PROCESS_INFORMATION process_info = {};
    bool is_created = CreateProcessW(nullptr, wide_command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, directory, &startup_info, &process_info) != 0;

    CloseHandle(out_write);
    CloseHandle(error_write);

    if (!is_created)
    {
        LOG_ERROR("git_run: CreateProcess failed: %u (is git in PATH?)", (unsigned)GetLastError());
        CloseHandle(out_read);
        CloseHandle(error_read);
        result.error_output = "CreateProcess failed";
        return false;
    }

    DWORD start_tick = GetTickCount();
    while (true)
    {
        drain_pipe(out_read, result.output);
        drain_pipe(error_read, result.error_output);
        DWORD wait = WaitForSingleObject(process_info.hProcess, POLL_INTERVAL_MILLISECONDS);
        if (wait == WAIT_OBJECT_0)
        {
            break;
        }
        if (GetTickCount() - start_tick > RUN_TIMEOUT_MILLISECONDS)
        {
            LOG_WARN("git_run timed out: cmd='%s'", command_line.c_str());
            TerminateProcess(process_info.hProcess, 1);
            break;
        }
    }
    drain_pipe(out_read, result.output);
    drain_pipe(error_read, result.error_output);

    DWORD exit_code = 1;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    result.exit_code = (int)exit_code;

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
    CloseHandle(out_read);
    CloseHandle(error_read);

    LOG_DEBUG("git_run exit=%d out='%s' err='%s'", result.exit_code,
        trim_end(result.output).c_str(), trim_end(result.error_output).c_str());
    return result.exit_code == 0;
}

bool git_find_executable(std::string& executable)
{
    GitResult result;
    if (!git_run("", { "--version" }, result))
    {
        LOG_WARN("git_find_executable: git not found in PATH");
        return false;
    }

    LOG_INFO("git_find_executable: %s", trim_end(result.output).c_str());
    executable = "git";
    return true;
}

bool git_get_repo_root(const std::string& path, std::string& root)
{
    GitResult result;
    if (!git_run(path, { "rev-parse", "--show-toplevel" }, result))
    {
        LOG_WARN("git_get_repo_root: '%s' is not in a work tree", path.c_str());
        return false;
    }

    std::string top_level = to_backslashes(trim_end(result.output));
    std::string normalized_path = to_backslashes(path);
    while (!top_level.empty() && top_level.back() == '\\')
    {
        top_level.pop_back();
    }
    while (!normalized_path.empty() && normalized_path.back() == '\\')
    {
        normalized_path.pop_back();
    }

    if (_stricmp(top_level.c_str(), normalized_path.c_str()) != 0)
    {
        LOG_WARN("git_get_repo_root: '%s' is a subfolder of '%s'",
            normalized_path.c_str(), top_level.c_str());
        return false;
    }

    root = top_level;
    return true;
}

bool git_get_current_branch(const std::string& repo_path, std::string& branch)
{
    // symbolic-ref works on a repository with no commits, where rev-parse fails
    GitResult result;
    if (!git_run(repo_path, { "symbolic-ref", "--short", "HEAD" }, result))
    {
        LOG_WARN("git_get_current_branch: detached HEAD or error (exit=%d)", result.exit_code);
        return false;
    }

    branch = trim_end(result.output);
    return true;
}

// Search only the first count entries (the local branches).
static bool has_branch_named(const std::vector<BranchInfo>& branches, size_t count,
    const std::string& name)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (_stricmp(branches[i].name.c_str(), name.c_str()) == 0)
        {
            return true;
        }
    }
    return false;
}

bool git_get_branches(const std::string& repo_path, std::vector<BranchInfo>& branches)
{
    branches.clear();

    GitResult local;
    if (!git_run(repo_path,
        { "branch", "--format=%(HEAD)|%(refname:short)|%(upstream:short)" }, local))
    {
        LOG_WARN("git_get_branches: local branch query failed");
        return false;
    }

    for (const std::string& line : split(local.output, '\n'))
    {
        size_t first_bar = line.find('|');
        if (first_bar == std::string::npos)
        {
            continue;
        }
        size_t second_bar = line.find('|', first_bar + 1);
        if (second_bar == std::string::npos)
        {
            continue;
        }

        BranchInfo branch;
        branch.is_current = line[0] == '*';
        branch.name = line.substr(first_bar + 1, second_bar - first_bar - 1);
        bool has_upstream = second_bar + 1 < line.size();
        branch.location = has_upstream ? BranchLocation::LOCAL_AND_REMOTE : BranchLocation::LOCAL;
        branches.push_back(branch);
    }
    size_t local_count = branches.size();

    // Remote branches with no local counterpart are listed as remote-only
    GitResult remote;
    if (!git_run(repo_path, { "branch", "-r", "--format=%(refname:short)" }, remote))
    {
        return true;
    }

    for (const std::string& line : split(remote.output, '\n'))
    {
        size_t slash = line.find('/');
        if (slash == std::string::npos)
        {
            continue;
        }

        std::string local_name = line.substr(slash + 1);
        if (local_name == "HEAD" || has_branch_named(branches, local_count, local_name))
        {
            continue;
        }

        BranchInfo branch;
        branch.name = line;
        branch.location = BranchLocation::REMOTE;
        branches.push_back(branch);
    }
    return true;
}

bool git_checkout_branch(const std::string& repo_path, const std::string& name,
    bool is_remote, GitResult& result)
{
    if (!is_remote)
    {
        return git_run(repo_path, { "checkout", name }, result);
    }

    // "origin/feature/x" becomes local "feature/x" tracking the remote branch
    size_t slash = name.find('/');
    std::string local_name = slash != std::string::npos ? name.substr(slash + 1) : name;
    return git_run(repo_path, { "checkout", "-b", local_name, "--track", name }, result);
}

bool git_create_branch(const std::string& repo_path, const std::string& name,
    GitResult& result)
{
    return git_run(repo_path, { "checkout", "-b", name }, result);
}

bool git_delete_branch(const std::string& repo_path, const std::string& name,
    GitResult& result)
{
    return git_run(repo_path, { "branch", "-d", name }, result);
}

bool git_commit(const std::string& repo_path, const std::string& summary,
    const std::string& description, GitResult& result)
{
    std::vector<std::string> args = { "commit", "-m", summary };
    if (!description.empty())
    {
        args.push_back("-m");
        args.push_back(description);
    }
    return git_run(repo_path, args, result);
}

bool git_get_log(const std::string& repo_path, std::vector<CommitInfo>& commits)
{
    commits.clear();

    char max_count[32];
    snprintf(max_count, sizeof(max_count), "--max-count=%d", MAX_LOG_COMMITS);

    GitResult result;
    if (!git_run(repo_path, { "log", max_count, "--format=%h%x1f%an%x1f%ar%x1f%s" }, result))
    {
        LOG_WARN("git_get_log failed (exit=%d): %s", result.exit_code,
            trim_end(result.error_output).c_str());
        return false;
    }

    for (const std::string& line : split(result.output, '\n'))
    {
        std::vector<std::string> fields = split(line, LOG_FIELD_SEPARATOR);
        if (fields.size() < 4)
        {
            continue;
        }

        CommitInfo commit;
        commit.hash = fields[0];
        commit.author = fields[1];
        commit.date = fields[2];
        commit.subject = fields[3];
        commits.push_back(commit);
    }
    return true;
}

bool git_get_status(const std::string& repo_path, std::vector<FileStatus>& files)
{
    files.clear();

    // -z: NUL-separated, paths never quoted. A rename is "XY new\0old\0".
    GitResult result;
    if (!git_run(repo_path, { "status", "--porcelain", "-z", "-u" }, result))
    {
        LOG_WARN("git_get_status failed (exit=%d): %s", result.exit_code,
            trim_end(result.error_output).c_str());
        return false;
    }

    std::vector<std::string> entries = split(result.output, '\0');
    size_t i = 0;
    while (i < entries.size())
    {
        const std::string& entry = entries[i];
        ++i;
        if (entry.size() <= STATUS_PATH_OFFSET)
        {
            continue;
        }

        FileStatus file;
        file.index_status = entry[0];
        file.worktree_status = entry[1];
        file.path = entry.substr(STATUS_PATH_OFFSET);

        bool is_renamed_or_copied = file.index_status == 'R' || file.index_status == 'C' ||
            file.worktree_status == 'R' || file.worktree_status == 'C';
        if (is_renamed_or_copied && i < entries.size())
        {
            file.old_path = entries[i];
            ++i;
        }
        files.push_back(file);
    }
    return true;
}

bool git_get_diff(const std::string& repo_path, const FileStatus& file, std::string& diff)
{
    GitResult result;

    if (file.index_status == '?' && file.worktree_status == '?')
    {
        // --no-index exits 1 whenever the files differ, which is always here
        git_run(repo_path, { "diff", "--no-index", "--", "/dev/null", file.path }, result);
        if (!result.output.empty())
        {
            diff = result.output;
        }
        else if (!result.error_output.empty())
        {
            diff = result.error_output;
        }
        else
        {
            diff = "(empty file)";
        }
        return true;
    }

    if (file.index_status != ' ')
    {
        if (git_run(repo_path, { "diff", "--cached", "--", file.path }, result) &&
            !result.output.empty())
        {
            diff = result.output;
            return true;
        }
    }

    if (file.worktree_status != ' ')
    {
        if (git_run(repo_path, { "diff", "--", file.path }, result) && !result.output.empty())
        {
            diff = result.output;
            return true;
        }
    }

    LOG_WARN("git_get_diff produced no output for '%s' (exit=%d err='%s')",
        file.path.c_str(), result.exit_code, trim_end(result.error_output).c_str());
    return false;
}

void git_refresh_repo_state(AppState& state)
{
    if (state.active_repo.empty())
    {
        return;
    }

    if (!git_get_current_branch(state.active_repo, state.current_branch))
    {
        state.current_branch.clear();
    }
    git_get_branches(state.active_repo, state.branches);
    git_get_status(state.active_repo, state.changed_files);
    git_get_log(state.active_repo, state.commits);
    state.selected_file_index = -1;
    state.current_diff.clear();

    LOG_INFO("git_refresh_repo_state: branch='%s' branches=%d files=%d commits=%d",
        state.current_branch.c_str(), (int)state.branches.size(),
        (int)state.changed_files.size(), (int)state.commits.size());
}
