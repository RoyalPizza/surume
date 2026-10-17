# Surume Architecture

## Language and Platform

- C++20, MSVC, built by a plain `build.bat` (no build system).
- Dear ImGui with the Win32 + Direct3D 11 backends.
- Windows 10 and 11 only. No other platforms are planned, so Win32 code lives directly in `main.cpp` with no platform abstraction layer.
- Third-party code: Dear ImGui and tomlc17 (git submodules). Nothing else without a deliberate decision.

## Engineering Principles

- Follow `dev-standards/cpp/cpp.md`: C-style C++ with plain structs and free functions, module prefixes (`git_get_status`), snake_case, Allman braces, no lambdas or member functions, failures returned as `bool` with out parameters, `/W4 /WX`. Project overrides: `.hpp` headers, build output in `bin/` and `obj/`.
- **Performance is a feature.** Design against the targets in `DESIGN.md`. Nothing slow runs on the UI thread, nothing is drawn that isn't visible, and nothing is redrawn when nothing changed.
- Prefer code that is easy to step through in a debugger.
- Keep modules split by responsibility without a framework layer. Use Dear ImGui directly.
- Fixed, application-owned panels; no docking. Vanilla ImGui styling, with hardcoded colors only for semantic Git states.
- Prefer machine-readable Git output (`--porcelain`, `-z`, `--format`) over parsing human-oriented text.
- Store preferences under `%APPDATA%\Surume`, never inside repositories. Never store credentials; rely on Git's credential helpers.
- Ship a single executable. Embed small assets (fonts, icon) at build time.

## Modules

- `main.cpp`: entry point and everything Windows-specific: window, D3D11 and ImGui backend setup, the frame loop, folder dialog, Explorer/terminal launch, app-data path. Also app startup and shutdown, and handling of requests from the GUI after each frame.
- `gui`: all Dear ImGui drawing. Reads `AppState` and records requests; never runs Git. Entry point `gui_render`.
- `git`: runs Git (`git_run`) and parses its output into the types in `model.hpp`. Pure functions of a repository path; no knowledge of the GUI or jobs.
- `jobs` (planned): the worker thread and job queue. Turns requests into `git` calls off the UI thread and hands results back.
- `model`: header-only data types and `AppState`.
- `persistence`: `settings.toml` and `repositories.toml` via tomlc17.
- `log`: debug-only log file (`SURUME_DEBUG`). User-visible results go through `AppState::status_message`.

## Frame Loop

```
wait for input or a job result (MsgWaitForMultipleObjects)
pump window messages
apply finished job results to AppState
gui_render(state)        -- draws, records requests
present
handle requests          -- enqueue jobs, open dialogs
```

The loop blocks until there is something to do, so an idle Surume uses no CPU or GPU. The worker wakes the UI thread when a result is ready. Animations or progress indicators request continuous frames only while they are visible.

## Background Jobs

All Git work runs on **one worker thread** that executes jobs from a queue, one at a time. One thread is enough: Git serializes index changes with `index.lock` anyway, and a single queue keeps ordering obvious and debugging simple.

```
UI thread                          worker thread
---------                          -------------
gui records request
handle_requests: push Job  ----->  pop Job
show busy state                    run git commands
                                   push JobResult
                           <-----  wake UI thread
apply JobResult to AppState
```

Design:

- **Threads and locks are Win32 primitives** (`CreateThread`, `SRWLOCK`, `CONDITION_VARIABLE`). The standard allows neither `std::thread` nor `std::mutex`.
- **A job is plain data:** an `enum class JobKind` (`REFRESH`, `LOAD_DIFF`, `COMMIT`, `CHECKOUT`, `FETCH`, `PULL`, `PUSH`, `STASH`, ...), the repository path, and kind-specific inputs (paths, message, patch). The worker dispatches with a `switch` on the kind.
- **A result is plain data:** the job kind, a success flag, the parsed output (a status snapshot, a diff, a log), and Git's error text. The UI thread applies results; the worker never touches `AppState`.
- **Stale results are dropped.** Each job carries the repository path and a generation number. If the user switched repositories or a newer refresh was queued, older results are ignored.
- **Refreshes coalesce.** Queuing a refresh while one is waiting replaces it instead of adding another.
- **Mutations are exclusive.** While a mutating job (commit, checkout, pull, push, stash, discard) runs, the GUI disables other mutating actions and shows what is running. Read-only views stay usable.
- **Network jobs can be cancelled.** Cancelling terminates the running `git` process. Only local jobs keep a timeout; fetch, pull, and push run until done or cancelled.
- **Every mutation ends with a refresh** of status, branches, and log.

## Application State

`AppState` is split by who writes it:

- **Repository snapshot:** branch, branches, upstream and ahead/behind, changed files, commits. Written only by applying refresh results; replaced wholesale, never patched by the GUI.
- **Commit selection:** checkbox state per path, selected lines per partially ticked file, summary, description, amend flag. Written by the GUI; survives refreshes by matching on path.
- **View state:** active tab, selected file or commit, loaded diff, filter text, splitter width.
- **Requests and jobs:** pending requests from the GUI, the running job, and the busy flag.
- **Settings and recent repositories:** loaded at startup, saved when changed.

## Git Command Execution

Every command goes through `git_run`:

- Argument vector, quoted with the Windows command-line rules; no shell.
- `CreateProcessW` with UTF-8 to UTF-16 conversion; output is kept as UTF-8.
- `-c core.quotepath=false` on every command so paths stay readable UTF-8.
- Captured stdout, stderr, and exit code in a `GitResult`.
- Planned: timeout per job kind instead of the fixed 15 seconds; cancellation; redacting credentials from URLs in logs.

Repository validation uses `git rev-parse --show-toplevel`; the chosen folder must equal the returned top level after normalization. Users open repository roots only.

## Git Commands

Current:

- Status: `git status --porcelain -z -u`
- Branch: `git symbolic-ref --short HEAD`, `git branch --format=...`, `git branch -r --format=...`
- Log: `git log --format=...` with `%x1f` field separators
- Diff: `git diff`, `git diff --cached`, `git diff --no-index` for untracked files
- Commit and branches: `git commit -m`, `git checkout`, `git checkout -b`, `git branch -d`

Planned:

- `git status --porcelain=v2 -z --branch` to get status, branch, upstream, and ahead/behind in **one** process launch instead of several.
- Commit selection: `git reset -q` (index back to `HEAD`), `git add -A -- <paths>`, `git apply --cached` for partial files, then `git commit`.
- Discard: `git restore --source=HEAD --staged --worktree -- <paths>`, and deleting untracked files.
- Stash: `git stash push --include-untracked`, `git stash list`, `git stash show -p`, `git stash pop`, `git stash drop`.
- Amend: `git commit --amend`; `git log -1 --format=%B` to load the message.
- Sync: `git fetch`, `git pull`, `git push`, `git push -u <remote> <branch>`, `git remote`.
- History details: `git show --format=... --name-status` and `git show <commit> -- <path>`.

## Partial Commits

For a partially ticked file, Surume builds a patch from that file's diff against `HEAD`, keeping only the selected changes:

- Selected `+` lines stay; unselected `+` lines are dropped.
- Selected `-` lines stay; unselected `-` lines become context lines.
- Hunk headers are recounted to match.

The patch is applied to the freshly reset index with `git apply --cached`. The working tree is never touched.

## Rendering

- The file list, commit list, and diff draw only visible rows (`ImGuiListClipper`).
- Diffs are split into lines and classified once when loaded, not on every frame.
- Avoid allocations in the per-frame path: build labels into fixed buffers, not new strings.
- High-DPI aware, with a TTF font that covers non-ASCII file names.

## Error Handling

Errors are shown in plain language with Git's raw output behind a details toggle:

- "This branch has no upstream branch."
- "Pull could not complete because there are merge conflicts."
- "Authentication failed while contacting the remote."
- "Git is not installed or could not be found."

## Persistence

Two TOML files in `%APPDATA%\Surume`:

- `settings.toml`: `left_column_width`, `git_path`, `default_open_dir`, `auto_fetch_enabled`, `auto_fetch_interval_min`.
- `repositories.toml`: `repos` (recent list) and `active` (last active repository).

Planned: write with wide-character file APIs so non-ASCII user folders work, and save when settings change instead of only on a clean exit.

## Build Layout

- `build.bat` builds a debug executable; `build.bat release` builds an optimized one.
- The executable goes to `bin/surume.exe`; intermediate files go to `obj/`, which is emptied at the start of every build.
- Debug builds define `SURUME_DEBUG`, which enables `%APPDATA%\Surume\surume_debug.log` (overwritten on each launch).
- `src/surume.rc` embeds `src/surume.ico` (16 to 256 px) as icon resource 1: the exe icon and the window/taskbar icon. The logo source is `docs/logo/surume.svg`; `docs/logo/make_icon.py` regenerates `surume.png` and `surume.ico` from it (needs Pillow and Edge). Rejected concepts are kept in `docs/logo/concepts/`.

## Testing

`build.bat test` builds `bin\surume_tests.exe` from `src\tests\` plus the non-GUI modules (`git`, `persistence`, `log`) and runs it. It returns non-zero if any check fails.

- The tests create throwaway repositories under `%TEMP%\surume tests ë日\` (non-ASCII and spaces on purpose) and delete the folder afterwards.
- Covered: repository root validation, command-line quoting and UTF-8 round trips through commit messages, status parsing (renames, non-ASCII and quoted names), diffs of untracked files, branch and log parsing, branch create/checkout/delete, and settings persistence in a non-ASCII folder.
- Add a test with every Git feature: commit selection, partial-commit patches, discard, stash, amend.
- Manual QA against repositories with: a clean tree, untracked files, deleted and renamed files, merge conflicts, detached `HEAD`, no remote, ahead/behind state, and a very large change set.
