// Tests for git.cpp and persistence.cpp against throwaway repositories. Run: build.bat test
#include "git.hpp"
#include "model.hpp"
#include "persistence.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

constexpr int WIDE_PATH_SIZE = MAX_PATH * 2;

// Non-ASCII and spaces on purpose: every path the tests use goes through this folder
constexpr const char* TEST_FOLDER_NAME = "surume tests \xC3\xAB\xE6\x97\xA5"; // "ë日"

static int failure_count = 0;
static int check_count = 0;

static void check(bool is_ok, const char* description)
{
    ++check_count;
    printf("%s  %s\n", is_ok ? "PASS" : "FAIL", description);
    if (!is_ok)
    {
        ++failure_count;
    }
}

static void check_equal(const std::string& actual, const std::string& expected,
    const char* description)
{
    check(actual == expected, description);
    if (actual != expected)
    {
        printf("      got:      [%s]\n      expected: [%s]\n", actual.c_str(), expected.c_str());
    }
}

static std::vector<wchar_t> to_wide(const std::string& text)
{
    std::vector<wchar_t> wide(WIDE_PATH_SIZE, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), WIDE_PATH_SIZE);
    return wide;
}

static std::string to_utf8(const wchar_t* text)
{
    char utf8[WIDE_PATH_SIZE * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

static void write_file(const std::string& path, const char* text)
{
    FILE* file = nullptr;
    if (_wfopen_s(&file, to_wide(path).data(), L"wb") != 0 || file == nullptr)
    {
        printf("      could not write %s\n", path.c_str());
        return;
    }
    fputs(text, file);
    fclose(file);
}

static void make_directory(const std::string& path)
{
    CreateDirectoryW(to_wide(path).data(), nullptr);
}

// Delete a folder tree. SHFileOperation needs a double-null-terminated path.
static void delete_tree(const std::string& path)
{
    std::vector<wchar_t> from = to_wide(path);
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation = {};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.data();
    operation.fFlags = FOF_NO_UI;
    SHFileOperationW(&operation);
}

// Long-form temp folder; GetTempPath can return 8.3 names like ROYALP~1.
static std::string temp_root()
{
    wchar_t short_path[MAX_PATH] = {};
    wchar_t long_path[WIDE_PATH_SIZE] = {};
    GetTempPathW(MAX_PATH, short_path);
    if (GetLongPathNameW(short_path, long_path, WIDE_PATH_SIZE) == 0)
    {
        return to_utf8(short_path);
    }
    return to_utf8(long_path);
}

static void run(const std::string& repo, const std::vector<std::string>& args)
{
    GitResult result;
    if (!git_run(repo, args, result))
    {
        printf("      setup command failed: git %s: %s\n", args[0].c_str(),
            result.error_output.c_str());
    }
}

static std::string create_repo(const std::string& root, const char* name)
{
    std::string repo = root + "\\" + name;
    make_directory(repo);
    run(repo, { "init", "-q", "-b", "main" });
    run(repo, { "config", "user.name", "Test" });
    run(repo, { "config", "user.email", "test@example.com" });
    run(repo, { "config", "commit.gpgsign", "false" });
    return repo;
}

static const FileStatus* find_file(const std::vector<FileStatus>& files, const std::string& path)
{
    for (const FileStatus& file : files)
    {
        if (file.path == path)
        {
            return &file;
        }
    }
    return nullptr;
}

// -------------------------------------------------------------------------

static void test_repo_root(const std::string& root)
{
    std::string repo = create_repo(root, "root check");
    std::string found_root;
    check(git_get_repo_root(repo, found_root), "repo root accepted for non-ASCII path, spaces");
    check_equal(found_root, repo, "repo root is returned with backslashes");

    std::string sub = repo + "\\sub";
    make_directory(sub);
    check(!git_get_repo_root(sub, found_root), "subfolder of a repo is rejected");
    check(!git_get_repo_root(root, found_root), "folder outside any repo is rejected");

    // 8.3 short names exist only if the volume generates them
    wchar_t short_path[WIDE_PATH_SIZE] = {};
    GetShortPathNameW(to_wide(repo).data(), short_path, WIDE_PATH_SIZE);
    std::string short_repo = to_utf8(short_path);
    if (short_repo.empty() || short_repo == repo)
    {
        printf("SKIP  8.3 short path (volume has no short names)\n");
        return;
    }
    check(git_get_repo_root(short_repo, found_root), "repo opened by its 8.3 short path");
}

static void test_commit_message(const std::string& root)
{
    std::string repo = create_repo(root, "commit message");
    write_file(repo + "\\a.txt", "one\n");
    run(repo, { "add", "." });

    // Summary ends in a backslash; description has \" and quotes, non-ASCII and an emoji
    std::string summary = "caf\xC3\xA9 \xE2\x80\x94 fix C:\\dir\\";
    std::string description = "say \\\"hi\\\" and \"bye\" \xF0\x9F\x90\x99";
    GitResult result;
    check(git_commit(repo, summary, description, result), "commit with tricky message succeeds");

    git_run(repo, { "log", "-1", "--format=%B" }, result);
    check_equal(result.output, summary + "\n\n" + description + "\n\n",
        "commit message round-trips exactly");

    check(git_run(repo, { "commit", "--allow-empty", "--allow-empty-message", "-q", "-m", "" },
        result), "empty argument is passed through, not dropped");
}

static void test_status(const std::string& root)
{
    std::string repo = create_repo(root, "status");
    write_file(repo + "\\a.txt", "one\n");
    write_file(repo + "\\keep.txt", "keep\n");
    run(repo, { "add", "." });
    run(repo, { "commit", "-q", "-m", "init" });

    run(repo, { "mv", "a.txt", "b.txt" });
    write_file(repo + "\\keep.txt", "changed\n");
    std::string odd_name = "my file \xC3\xA9 'q'.txt";
    write_file(repo + "\\" + odd_name, "x\n");

    std::vector<FileStatus> files;
    check(git_get_status(repo, files), "status succeeds");
    check(files.size() == 3, "status lists 3 entries");

    const FileStatus* renamed = find_file(files, "b.txt");
    check(renamed != nullptr && renamed->index_status == 'R' && renamed->old_path == "a.txt",
        "rename parsed as path=new, old_path=old");

    const FileStatus* modified = find_file(files, "keep.txt");
    check(modified != nullptr && modified->worktree_status == 'M', "modified file parsed");

    const FileStatus* untracked = find_file(files, odd_name);
    check(untracked != nullptr && untracked->index_status == '?',
        "non-ASCII name with spaces and quotes comes back unescaped");

    std::string diff;
    check(untracked != nullptr && git_get_diff(repo, *untracked, diff) &&
        diff.find("+x") != std::string::npos, "diff works for the untracked odd name");
}

static void test_branches_and_log(const std::string& root)
{
    std::string repo = create_repo(root, "branches");
    write_file(repo + "\\a.txt", "one\n");
    run(repo, { "add", "." });
    run(repo, { "commit", "-q", "-m", "first" });
    write_file(repo + "\\a.txt", "two\n");
    run(repo, { "commit", "-q", "-am", "second | with bar" });

    GitResult result;
    check(git_create_branch(repo, "feature/x", result), "create branch");

    std::string branch;
    check(git_get_current_branch(repo, branch), "current branch read");
    check_equal(branch, "feature/x", "new branch is checked out");

    std::vector<BranchInfo> branches;
    check(git_get_branches(repo, branches), "branch list succeeds");
    bool has_main = false;
    bool is_feature_current = false;
    for (const BranchInfo& info : branches)
    {
        has_main = has_main || info.name == "main";
        is_feature_current = is_feature_current || (info.name == "feature/x" && info.is_current);
    }
    check(branches.size() == 2 && has_main && is_feature_current,
        "branch list has main and current feature/x");

    std::vector<CommitInfo> commits;
    check(git_get_log(repo, commits), "log succeeds");
    check(commits.size() == 2, "log has 2 commits");
    check(!commits.empty() && commits[0].subject == "second | with bar",
        "log subject with a bar parsed, newest first");

    check(git_checkout_branch(repo, "main", false, result), "checkout main");
    check(git_delete_branch(repo, "feature/x", result), "delete merged branch");

    // A branch named like a tracked file must switch branches, not restore the file
    check(git_create_branch(repo, "a.txt", result), "create branch named like a file");
    check(git_checkout_branch(repo, "main", false, result) &&
        git_checkout_branch(repo, "a.txt", false, result) &&
        git_get_current_branch(repo, branch) && branch == "a.txt",
        "switch to a branch named like a file");
}

static void commit_file(const std::string& repo, const char* text, const char* message)
{
    write_file(repo + "\\a.txt", text);
    run(repo, { "add", "a.txt" });
    run(repo, { "commit", "-q", "-m", message });
}

static void test_ahead_behind(const std::string& root)
{
    std::string remote = root + "\\remote.git";
    make_directory(remote);
    run(remote, { "init", "-q", "--bare", "-b", "main" });

    std::string repo = create_repo(root, "ahead behind");
    commit_file(repo, "1\n", "one");
    commit_file(repo, "2\n", "two");
    run(repo, { "remote", "add", "origin", remote });
    run(repo, { "push", "-q", "-u", "origin", "main" });

    BranchStatus branch;
    std::vector<FileStatus> files;
    check(git_get_status(repo, branch, files), "status with branch info succeeds");
    check(branch.name == "main" && branch.upstream == "origin/main", "branch and upstream read");
    check(branch.ahead == 0 && branch.behind == 0, "even with upstream after push");

    // Local drops "two" and adds "three": one ahead, one behind
    run(repo, { "reset", "-q", "--hard", "HEAD~1" });
    commit_file(repo, "3\n", "three");
    check(git_get_status(repo, branch, files) && branch.ahead == 1 && branch.behind == 1,
        "1 ahead and 1 behind after diverging");

    run(repo, { "switch", "-q", "--detach" });
    check(git_get_status(repo, branch, files) && branch.name.empty(), "detached HEAD has no name");
}

static void test_conflict(const std::string& root)
{
    std::string repo = create_repo(root, "conflict");
    commit_file(repo, "base\n", "base");
    run(repo, { "switch", "-q", "-c", "other" });
    commit_file(repo, "other\n", "other");
    run(repo, { "switch", "-q", "main" });
    commit_file(repo, "main\n", "main");

    GitResult result;
    check(!git_run(repo, { "merge", "other" }, result), "merge stops on the conflict");

    std::vector<FileStatus> files;
    check(git_get_status(repo, files), "status during a conflict succeeds");
    const FileStatus* conflicted = find_file(files, "a.txt");
    check(conflicted != nullptr && conflicted->is_conflicted, "conflicted file is marked");
}

static void test_commit_paths(const std::string& root)
{
    std::string repo = create_repo(root, "commit paths");
    GitResult result;

    // First commit in a repository with no HEAD yet, with only one of two new files
    write_file(repo + "\\a.txt", "a\n");
    write_file(repo + "\\b.txt", "b\n");
    check(git_commit_paths(repo, { "a.txt" }, "first", "", result), "first commit with no HEAD");
    std::vector<FileStatus> files;
    git_get_status(repo, files);
    check(files.size() == 1 && files[0].path == "b.txt" && files[0].index_status == '?',
        "unticked new file stays untracked");

    run(repo, { "add", "b.txt" });
    run(repo, { "commit", "-q", "-m", "add b" });
    write_file(repo + "\\c.txt", "c\n");
    run(repo, { "add", "c.txt" });
    run(repo, { "commit", "-q", "-m", "add c" });

    // Modify a, delete b, rename c to d; stage a from the "command line"
    write_file(repo + "\\a.txt", "a2\n");
    run(repo, { "add", "a.txt" });
    run(repo, { "rm", "-q", "b.txt" });
    run(repo, { "mv", "c.txt", "d.txt" });

    // Tick the deletion and the rename (both sides) but not a.txt
    check(git_commit_paths(repo, { "b.txt", "d.txt", "c.txt" }, "delete and rename", "", result),
        "commit a deletion and a rename");
    git_get_status(repo, files);
    const FileStatus* modified = find_file(files, "a.txt");
    check(files.size() == 1 && modified != nullptr && modified->index_status == ' ' &&
        modified->worktree_status == 'M', "outside staging replaced: a.txt left unstaged");

    git_run(repo, { "show", "--name-status", "--format=", "HEAD" }, result);
    check(result.output.find("D\tb.txt") != std::string::npos &&
        result.output.find("R100\tc.txt\td.txt") != std::string::npos,
        "commit has the deletion and the rename");

    // Nothing ticked: the commit fails with git's reason on stdout
    check(!git_commit_paths(repo, {}, "empty", "", result) &&
        git_error_text(result).find("no changes added") != std::string::npos,
        "empty selection fails with git's reason (from stdout)");
}

static void test_diff(const std::string& root)
{
    std::string repo = create_repo(root, "diff");
    write_file(repo + "\\a.txt", "one\ntwo\n");
    write_file(repo + "\\bin.dat", "x");
    run(repo, { "add", "." });
    run(repo, { "commit", "-q", "-m", "init" });

    // One change staged, another only in the working tree
    write_file(repo + "\\a.txt", "ONE\ntwo\n");
    run(repo, { "add", "a.txt" });
    write_file(repo + "\\a.txt", "ONE\nTWO\n");

    FileStatus file;
    file.path = "a.txt";
    file.index_status = 'M';
    file.worktree_status = 'M';
    std::string diff;
    check(git_get_diff(repo, file, diff) && diff.find("+ONE") != std::string::npos &&
        diff.find("+TWO") != std::string::npos, "diff shows staged and unstaged changes together");

    FILE* binary = nullptr;
    _wfopen_s(&binary, to_wide(repo + "\\bin.dat").data(), L"wb");
    const char bytes[] = { 0, 1, 2, 0, 3 };
    fwrite(bytes, 1, sizeof(bytes), binary);
    fclose(binary);
    FileStatus binary_file;
    binary_file.path = "bin.dat";
    binary_file.worktree_status = 'M';
    check(git_get_diff(repo, binary_file, diff) && diff.find("Binary files") != std::string::npos,
        "binary change is reported as binary");

    std::vector<DiffLine> lines;
    int max_columns = 0;
    std::string sample = "diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 +1 @@\n-old\n+\tnew\n ctx";
    git_parse_diff_lines(sample, lines, max_columns);
    check(lines.size() == 7 && lines[0].kind == DiffLineKind::HEADER &&
        lines[1].kind == DiffLineKind::HEADER && lines[2].kind == DiffLineKind::HEADER &&
        lines[3].kind == DiffLineKind::HUNK && lines[4].kind == DiffLineKind::REMOVED &&
        lines[5].kind == DiffLineKind::ADDED && lines[6].kind == DiffLineKind::CONTEXT,
        "diff lines classified");
    check(max_columns == 18 && sample.substr(lines[5].start, lines[5].length) == "+\tnew",
        "diff line ranges and widest line (tab counts as 4)");
}

static void test_commit_details(const std::string& root)
{
    std::string repo = create_repo(root, "details");
    write_file(repo + "\\old.txt", "keep\n");
    run(repo, { "add", "." });
    run(repo, { "commit", "-q", "-m", "init" });

    run(repo, { "mv", "old.txt", "new.txt" });
    write_file(repo + "\\added.txt", "hello\n");
    run(repo, { "add", "." });
    run(repo, { "commit", "-q", "-m", "Subject line", "-m", "Body line one.\nBody line two." });

    std::vector<CommitInfo> commits;
    check(git_get_log(repo, commits) && !commits.empty() && commits[0].full_hash.size() == 40,
        "log includes full hashes");

    CommitDetails details;
    check(!commits.empty() && git_get_commit_details(repo, commits[0].full_hash, details),
        "commit details load");
    check_equal(details.message, "Subject line\n\nBody line one.\nBody line two.",
        "full message read");
    check(details.author == "Test" && details.email == "test@example.com" &&
        details.date.size() == 16, "author, email and absolute date read");

    const FileStatus* renamed = find_file(details.files, "new.txt");
    const FileStatus* added = find_file(details.files, "added.txt");
    check(details.files.size() == 2 && renamed != nullptr && renamed->index_status == 'R' &&
        renamed->old_path == "old.txt" && added != nullptr && added->index_status == 'A',
        "changed files include the rename and the addition");

    std::string diff;
    check(added != nullptr && git_get_commit_file_diff(repo, details.full_hash, *added, diff) &&
        diff.find("+hello") != std::string::npos, "file diff for a commit");
}

static bool file_exists(const std::string& path)
{
    return GetFileAttributesW(to_wide(path).data()) != INVALID_FILE_ATTRIBUTES;
}

static void test_discard(const std::string& root)
{
    std::string repo = create_repo(root, "discard");
    write_file(repo + "\\mod.txt", "original\n");
    write_file(repo + "\\gone.txt", "keep me\n");
    write_file(repo + "\\old.txt", "rename me\n");
    run(repo, { "add", "." });
    run(repo, { "commit", "-q", "-m", "init" });

    write_file(repo + "\\mod.txt", "changed\n");
    run(repo, { "add", "mod.txt" });
    write_file(repo + "\\mod.txt", "changed again\n");
    run(repo, { "rm", "-q", "gone.txt" });
    write_file(repo + "\\added.txt", "new\n");
    run(repo, { "add", "added.txt" });
    write_file(repo + "\\untracked.txt", "junk\n");
    run(repo, { "mv", "old.txt", "new.txt" });

    std::vector<FileStatus> files;
    git_get_status(repo, files);
    GitResult result;
    check(git_discard(repo, files, result), "discard every kind of change");

    git_get_status(repo, files);
    check(files.empty(), "working tree is clean after discarding everything");
    check(!file_exists(repo + "\\added.txt") && !file_exists(repo + "\\untracked.txt") &&
        !file_exists(repo + "\\new.txt"), "new, untracked and renamed-to files are gone");
    check(file_exists(repo + "\\gone.txt") && file_exists(repo + "\\old.txt"),
        "deleted and renamed-from files are back");

    // Repository with no commits: discarding a staged new file removes it
    std::string empty_repo = create_repo(root, "discard empty");
    write_file(empty_repo + "\\first.txt", "x\n");
    run(empty_repo, { "add", "first.txt" });
    git_get_status(empty_repo, files);
    check(git_discard(empty_repo, files, result) && !file_exists(empty_repo + "\\first.txt"),
        "discard in a repository with no commits");
}

static void test_persistence(const std::string& root)
{
    std::string dir = root + "\\app data \xC3\xBC\\"; // "ü"
    make_directory(dir.substr(0, dir.size() - 1));

    AppState saved;
    saved.active_repo = "C:\\repos\\caf\xC3\xA9 \"quoted\"";
    saved.recent_repos = { saved.active_repo, "D:\\other" };
    saved.left_column_width = 333.0f;
    saved.settings.git_path = "C:\\Program Files\\Git\\bin\\git.exe";
    saved.settings.is_auto_fetch_enabled = true;
    saved.settings.auto_fetch_interval_minutes = 7;
    check(persistence_save(saved, dir), "settings save to a non-ASCII folder");

    AppState loaded;
    persistence_load(loaded, dir);
    check_equal(loaded.restore_repo, saved.active_repo, "active repo round-trips");

    // A repository that couldn't be reopened yet (e.g. git missing) stays remembered
    AppState unopened;
    unopened.restore_repo = "C:\\repos\\later";
    check(persistence_save(unopened, dir), "save with only a restore repo");
    AppState reloaded;
    persistence_load(reloaded, dir);
    check_equal(reloaded.restore_repo, unopened.restore_repo, "unopened repo stays remembered");
    check(loaded.recent_repos == saved.recent_repos, "recent repos round-trip");
    check(loaded.left_column_width == 333.0f, "column width round-trips");
    check_equal(loaded.settings.git_path, saved.settings.git_path, "git path round-trips");
    check(loaded.settings.is_auto_fetch_enabled &&
        loaded.settings.auto_fetch_interval_minutes == 7, "auto-fetch settings round-trip");
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    std::string git;
    if (!git_find_executable(git))
    {
        printf("git not found on PATH\n");
        return 1;
    }

    std::string root = temp_root() + TEST_FOLDER_NAME;
    delete_tree(root);
    make_directory(root);

    test_repo_root(root);
    test_commit_message(root);
    test_status(root);
    test_branches_and_log(root);
    test_ahead_behind(root);
    test_conflict(root);
    test_commit_paths(root);
    test_diff(root);
    test_commit_details(root);
    test_discard(root);
    test_persistence(root);

    delete_tree(root);
    printf("\n%d checks, %d failed\n", check_count, failure_count);
    return failure_count == 0 ? 0 : 1;
}
