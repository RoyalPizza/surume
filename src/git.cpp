#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "git.hpp"
#include "log.hpp"
#include <sstream>

static std::string QuoteArg(const std::string& arg) {
    if (arg.find_first_of(" \t\n\"") == std::string::npos)
        return arg;
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"') out += "\\\"";
        else          out += c;
    }
    out += '"';
    return out;
}

static void DrainPipe(HANDLE pipe, std::string& dest) {
    char buf[4096];
    DWORD avail = 0;
    while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
        DWORD toRead  = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
        DWORD nRead   = 0;
        if (!ReadFile(pipe, buf, toRead, &nRead, nullptr) || nRead == 0) break;
        dest.append(buf, nRead);
    }
}

static std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

static std::string NormSep(std::string s) {
    for (char& c : s) if (c == '/') c = '\\';
    return s;
}

// -------------------------------------------------------------------------

GitResult Git::Run(const std::string& repoPath, std::vector<std::string> args) {
    std::string cmdLine = "git";
    for (const auto& arg : args) {
        cmdLine += ' ';
        cmdLine += QuoteArg(arg);
    }
    LOG_DEBUG("Git::Run cwd='%s' cmd='%s'", repoPath.c_str(), cmdLine.c_str());

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hOutR = nullptr, hOutW = nullptr;
    HANDLE hErrR = nullptr, hErrW = nullptr;

    if (!CreatePipe(&hOutR, &hOutW, &sa, 0) || !CreatePipe(&hErrR, &hErrW, &sa, 0)) {
        LOG_ERROR("Git::Run CreatePipe failed: %u", (unsigned)GetLastError());
        return { -1, "", "CreatePipe failed" };
    }
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESTDHANDLES;
    si.hStdOutput   = hOutW;
    si.hStdError    = hErrW;

    PROCESS_INFORMATION pi = {};
    const char* cwd = repoPath.empty() ? nullptr : repoPath.c_str();

    bool created = !!CreateProcessA(
        nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, cwd, &si, &pi);

    CloseHandle(hOutW);
    CloseHandle(hErrW);

    if (!created) {
        LOG_ERROR("Git::Run CreateProcess failed: %u (is git in PATH?)", (unsigned)GetLastError());
        CloseHandle(hOutR); CloseHandle(hErrR);
        return { -1, "", "CreateProcess failed" };
    }

    GitResult result;
    DWORD startTick = GetTickCount();

    while (true) {
        DrainPipe(hOutR, result.out);
        DrainPipe(hErrR, result.err);
        if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) break;
        if (GetTickCount() - startTick > 15000) {
            LOG_WARN("Git::Run timed out — cmd='%s'", cmdLine.c_str());
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    DrainPipe(hOutR, result.out);
    DrainPipe(hErrR, result.err);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = (int)exitCode;

    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    CloseHandle(hOutR);       CloseHandle(hErrR);

    LOG_DEBUG("Git::Run exit=%d out='%s' err='%s'",
        result.exitCode, Trim(result.out).c_str(), Trim(result.err).c_str());
    return result;
}

std::string Git::FindExecutable() {
    LOG_INFO("Git::FindExecutable");
    GitResult r = Run("", { "--version" });
    if (r.Ok()) {
        LOG_INFO("Git found in PATH: %s", Trim(r.out).c_str());
        return "git";
    }
    LOG_WARN("Git not found in PATH");
    return "";
}

std::string Git::GetRepoRoot(const std::string& path) {
    LOG_INFO("Git::GetRepoRoot path='%s'", path.c_str());
    GitResult r = Run(path, { "rev-parse", "--show-toplevel" });
    if (!r.Ok()) {
        LOG_WARN("Git::GetRepoRoot failed (exit=%d)", r.exitCode);
        return "";
    }

    std::string root     = NormSep(Trim(r.out));
    std::string normPath = NormSep(path);
    while (!root.empty()     && root.back()     == '\\') root.pop_back();
    while (!normPath.empty() && normPath.back() == '\\') normPath.pop_back();

    if (_stricmp(root.c_str(), normPath.c_str()) != 0) {
        LOG_WARN("Git::GetRepoRoot: '%s' is a subfolder of '%s' — rejecting", normPath.c_str(), root.c_str());
        return "";
    }
    LOG_INFO("Git::GetRepoRoot validated root='%s'", root.c_str());
    return root;
}

std::string Git::GetCurrentBranch(const std::string& repoPath) {
    // rev-parse fails on repos with zero commits — fall back to symbolic-ref
    GitResult r = Run(repoPath, { "symbolic-ref", "--short", "HEAD" });
    if (r.Ok()) {
        std::string branch = Trim(r.out);
        LOG_INFO("Git::GetCurrentBranch='%s' (symbolic-ref)", branch.c_str());
        return branch;
    }
    // Detached HEAD: rev-parse returns "HEAD", symbolic-ref fails
    LOG_WARN("Git::GetCurrentBranch: detached HEAD or error (exit=%d)", r.exitCode);
    return "";
}

std::vector<BranchInfo> Git::GetBranches(const std::string& repoPath) {
    LOG_INFO("Git::GetBranches repoPath='%s'", repoPath.c_str());
    std::vector<BranchInfo> result;

    // Local branches: %(HEAD)|%(refname:short)|%(upstream:short)
    GitResult local = Run(repoPath, { "branch", "--format=%(HEAD)|%(refname:short)|%(upstream:short)" });
    if (!local.Ok()) {
        LOG_WARN("Git::GetBranches local branch query failed");
        return result;
    }

    std::vector<std::string> localNames;
    std::istringstream ls(local.out);
    std::string line;
    while (std::getline(ls, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        bool isCurrent = !line.empty() && line[0] == '*';
        size_t p1 = line.find('|');
        if (p1 == std::string::npos) continue;
        size_t p2 = line.find('|', p1 + 1);
        std::string name     = line.substr(p1 + 1, p2 != std::string::npos ? p2 - p1 - 1 : std::string::npos);
        std::string upstream = p2 != std::string::npos ? line.substr(p2 + 1) : "";

        BranchInfo info;
        info.name      = name;
        info.isCurrent = isCurrent;
        info.location  = upstream.empty() ? BranchLocation::Local : BranchLocation::LocalAndRemote;
        result.push_back(info);
        localNames.push_back(name);
    }

    // Remote branches — add those with no local counterpart as Remote-only
    GitResult remote = Run(repoPath, { "branch", "-r", "--format=%(refname:short)" });
    if (remote.Ok()) {
        std::istringstream rs(remote.out);
        while (std::getline(rs, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.empty()) continue;

            size_t slash = line.find('/');
            if (slash == std::string::npos) continue;
            std::string localEquiv = line.substr(slash + 1);
            if (localEquiv == "HEAD") continue;

            bool hasLocal = false;
            for (const auto& n : localNames)
                if (_stricmp(n.c_str(), localEquiv.c_str()) == 0) { hasLocal = true; break; }
            if (hasLocal) continue;

            BranchInfo info;
            info.name     = line;
            info.location = BranchLocation::Remote;
            result.push_back(info);
        }
    }

    LOG_INFO("Git::GetBranches found %d branches", (int)result.size());
    return result;
}

GitResult Git::CheckoutBranch(const std::string& repoPath, const std::string& name, bool isRemote) {
    LOG_INFO("Git::CheckoutBranch name='%s' isRemote=%d", name.c_str(), (int)isRemote);
    if (!isRemote)
        return Run(repoPath, { "checkout", name });

    // Remote branch like "origin/main" — create local tracking branch
    size_t slash      = name.find('/');
    std::string local = slash != std::string::npos ? name.substr(slash + 1) : name;
    return Run(repoPath, { "checkout", "-b", local, "--track", name });
}

GitResult Git::CreateBranch(const std::string& repoPath, const std::string& name) {
    LOG_INFO("Git::CreateBranch name='%s'", name.c_str());
    return Run(repoPath, { "checkout", "-b", name });
}

GitResult Git::DeleteBranch(const std::string& repoPath, const std::string& name) {
    LOG_INFO("Git::DeleteBranch name='%s'", name.c_str());
    return Run(repoPath, { "branch", "-d", name });
}

GitResult Git::Commit(const std::string& repoPath, const std::string& summary, const std::string& description) {
    LOG_INFO("Git::Commit summary='%s'", summary.c_str());
    std::vector<std::string> args = { "commit", "-m", summary };
    if (!description.empty()) {
        args.push_back("-m");
        args.push_back(description);
    }
    return Run(repoPath, args);
}

std::string Git::GetDiff(const std::string& repoPath, const FileStatus& file) {
    LOG_INFO("Git::GetDiff path='%s' x='%c' y='%c'", file.path.c_str(), file.x, file.y);

    GitResult r;

    if (file.x == '?' && file.y == '?') {
        // Untracked: diff against /dev/null to show the whole file as added
        r = Run(repoPath, { "diff", "--no-index", "--", "/dev/null", file.path });
        // --no-index exits 1 when files differ (always), treat as ok
        if (!r.out.empty()) return r.out;
        return r.err.empty() ? "(empty file)" : r.err;
    }

    if (file.x != ' ') {
        // Has staged changes — show what's in the index vs HEAD (or empty tree for new repos)
        r = Run(repoPath, { "diff", "--cached", "--", file.path });
        if (r.Ok() && !r.out.empty()) return r.out;
    }

    if (file.y != ' ') {
        // Has unstaged working-tree changes
        r = Run(repoPath, { "diff", "--", file.path });
        if (r.Ok() && !r.out.empty()) return r.out;
    }

    if (!r.out.empty()) return r.out;
    LOG_WARN("Git::GetDiff produced no output (exit=%d err='%s')", r.exitCode, r.err.c_str());
    return "";
}

std::vector<FileStatus> Git::GetStatus(const std::string& repoPath) {
    LOG_INFO("Git::GetStatus repoPath='%s'", repoPath.c_str());

    GitResult r = Run(repoPath, { "status", "--porcelain", "-u" });
    std::vector<FileStatus> result;
    if (!r.Ok()) {
        LOG_WARN("Git::GetStatus failed (exit=%d): %s", r.exitCode, Trim(r.err).c_str());
        return result;
    }

    std::istringstream ss(r.out);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.size() < 4) continue; // need "XY path"

        FileStatus f;
        f.x = line[0];
        f.y = line[1];
        // line[2] is always a space
        std::string rest = line.substr(3);

        // Strip surrounding quotes (git quotes paths with special chars)
        if (!rest.empty() && rest.front() == '"' && rest.back() == '"')
            rest = rest.substr(1, rest.size() - 2);

        // Rename: "new -> old"
        size_t arrow = rest.find(" -> ");
        if (arrow != std::string::npos) {
            f.path    = rest.substr(0, arrow);
            f.oldPath = rest.substr(arrow + 4);
        } else {
            f.path = rest;
        }

        result.push_back(std::move(f));
    }

    LOG_INFO("Git::GetStatus found %d changed files", (int)result.size());
    return result;
}

std::vector<CommitInfo> Git::GetLog(const std::string& repoPath, int maxCount) {
    LOG_INFO("Git::GetLog repoPath='%s' max=%d", repoPath.c_str(), maxCount);

    // Use ASCII unit separator (0x1F) as field delimiter — safe in any commit message.
    char maxStr[32];
    std::snprintf(maxStr, sizeof(maxStr), "%d", maxCount);
    GitResult r = Run(repoPath, {
        "log",
        "--max-count=" + std::string(maxStr),
        "--format=%h\x1f%an\x1f%ar\x1f%s"
    });

    std::vector<CommitInfo> result;
    if (!r.Ok()) {
        LOG_WARN("Git::GetLog failed (exit=%d): %s", r.exitCode, Trim(r.err).c_str());
        return result;
    }

    std::istringstream ss(r.out);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        auto field = [&](size_t& pos) -> std::string {
            size_t end = line.find('\x1f', pos);
            std::string f = line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? line.size() : end + 1;
            return f;
        };

        size_t pos = 0;
        CommitInfo c;
        c.hash    = field(pos);
        c.author  = field(pos);
        c.date    = field(pos);
        c.subject = field(pos);
        result.push_back(std::move(c));
    }

    LOG_INFO("Git::GetLog returned %d commits", (int)result.size());
    return result;
}
