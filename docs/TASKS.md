# Tasks

Surume is the Gitaluna codebase moved into its own repo and redesigned (see `DESIGN.md` and `ARCHITECTURE.md`). The first version ships as one release when every task under **Features: v1** is done, plus the bugs.

Each task has a permanent ID: a group letter and a number, like `G3`. New tasks take the next number in their group. IDs are never reused or renumbered when tasks move or finish.

| Letter | Group | Next |
| --- | --- | --- |
| `N` | Next / big-ticket work | N3 |
| `G` | Git bugs | G7 |
| `U` | GUI bugs | U9 |
| `A` | App bugs | A5 |
| `F` | Features | F25 |
| `I` | Improvements | I9 |
| `C` | Chores (repo, build, structure) | C6 |

## Next

Nothing queued. Pick from the v1 features and bugs below.

## Bugs

### Git (`git.cpp`)

- [x] G1 **8.3 short paths and `subst` drives are rejected.** `git_get_repo_root` compares the chosen path to Git's long-form top-level path as text, so a path like `C:\Users\ROYALP~1\repo` fails. The folder picker returns long paths, so this only affects paths typed or restored from elsewhere. Normalize with `GetLongPathNameW` or `GetFinalPathNameByHandleW` first.
- [x] G2 **Diffs show only the staged part** of a file that has both staged and unstaged changes.
- [x] G3 **Commit failure shows an empty reason.** With nothing staged, Git writes "nothing to commit" to stdout, not stderr.
- [x] G4 **Conflicts look like modifications.** Unmerged files (`UU`, `AA`, ...) get the yellow `M` badge. v1 minimum: a distinct conflicted badge and a message saying the merge must be finished in an editor or terminal.
- [x] G5 **`checkout <name>` is ambiguous** if a branch and a file share a name. Use `git switch`.

### GUI (`gui.cpp`)

- [x] U1 **View > Changes / History does nothing.** It sets `state.nav_mode`, but the tab bar never reads it.
- [x] U2 **File > Exit does nothing.**
- [x] U3 **Commit button is enabled when nothing is staged.**
- [x] U4 **Missing Git isn't handled.** `is_git_found` is never checked by the GUI: no error is shown and repo actions stay enabled.
- [ ] U5 **Large diffs are slow.** `render_diff` draws every line every frame; use `ImGuiListClipper`.
- [x] U6 **Commit text carries across repo switches**, and the 1024-byte description buffer truncates silently.
- [x] U7 **Long relative dates** ("2 years, 3 months ago") overlap the hash in History.

### App (`main.cpp`, `persistence.cpp`)

- [x] A1 **Settings aren't saved for non-ASCII Windows usernames.** `fopen_s` and `toml_parse_file_ex` take ANSI paths. Use wide-char file APIs.
- [x] A2 **Likely spins the CPU while minimized.** `Present(1, 0)` returns immediately when occluded; check for `DXGI_STATUS_OCCLUDED` and sleep, like the ImGui example does.
- [x] A3 **`window_shutdown` destroys an already-destroyed window** (harmless, but wrong).
- [x] A4 **`imgui.ini` is written to the working directory.** The app owns its layout, so set `io.IniFilename = nullptr` and drop the `.gitignore` entry.

## Features: v1

- [x] F1 **Checkbox commit selection.** A checkbox per file plus tick-all; new changes ticked by default; ticks kept per path across refreshes; already-staged files load ticked. Commit resets the index, stages the ticked files, then commits. See DESIGN.md, Review and Commit Changes.
- [ ] F2 Sync button: fetch, pull, push, publish branch.
- [x] F3 Ahead/behind counts. Switch status to `git status --porcelain=v2 -z --branch` to get status, upstream and ahead/behind in one process launch.
- [ ] F4 Auto-fetch (settings exist but are unused).
- [x] F5 Repo refresh when the window regains focus, and on F5. The file list is stale until the app runs a Git action.
- [ ] F6 History: commit details and changed files.
- [ ] F7 Changed-file path filter.
- [x] F8 Branch state markers (local / remote / tracking all render the same).
- [ ] F9 Remove a repository from the recent list; repository health warnings.
- [x] F10 Binary and large-file diff placeholders.
- [ ] F11 Settings that take effect: Git executable path, default open directory.
- [x] F12 **Background jobs.** One Win32 worker thread with a job queue, per ARCHITECTURE.md: plain-data jobs and results, stale results dropped by generation, coalesced refreshes, exclusive mutations, cancellable network jobs with no fixed timeout. Includes splitting `AppState` by writer (snapshot, selection, view, requests, settings) and moving the GUI's direct Git calls into jobs (absorbs I6).
- [ ] F13 Keyboard shortcuts and context menus.
- [x] F14 Tests: Git output parsing against a throwaway repo.
- [ ] F15 **Partial commits.** Line and hunk checkboxes in the diff gutter; the file checkbox shows a mixed state. Commit builds a patch from the selected lines and applies it with `git apply --cached`.
- [ ] F16 **Discard changes.** Context menu on a file or selection, and "Discard all changes..." in the Repository menu. Confirms first; untracked files are deleted.
- [ ] F17 **Stash.** "Stash all changes" (including untracked) in the Branch menu; a "Stashed changes" row in the Changes list with Restore and Discard; offer to stash when switching branches with changes.
- [ ] F18 **Amend last commit.** Checkbox in the commit footer that loads the last message and commits with `--amend`; warns when the commit is already pushed.

## Features: later

- [ ] F19 Conflict handling: mark files resolved, continue or abort a merge or rebase.
- [ ] F20 Clone by URL.
- [ ] F21 Branch graph in History.
- [ ] F22 GitHub (or other host) integration: sign-in, browse repositories, pull requests.
- [ ] F23 Syntax highlighting, side-by-side and word-level diffs.
- [ ] F24 Themes.

## Improvements

- [x] I1 High-DPI support (`ImGui_ImplWin32_EnableDpiAwareness` or a manifest); the window is blurry on scaled displays.
- [x] I2 Load a TTF font with wider glyph coverage so non-ASCII file names don't render as `?`.
- [x] I3 Add `/utf-8` to `build.bat`; `gui.cpp` contains UTF-8 characters.
- [x] I4 Only redraw when something changes instead of rendering at 60 fps while idle.
- [x] I5 Save settings when they change, not only on a clean exit.
- [x] I6 Stop the GUI calling Git directly; route actions through request flags handled in `main.cpp`, like Open Repository. Part of F12.
- [x] I7 Clean up `.gitignore`: drop the leftover CMake, Visual Studio project, compiler output and `*.log` entries.
- [x] I8 Add a `compile_flags.txt` so clangd in VS Code finds the ImGui and tomlc17 headers.

## Done

- [x] N1 **Redesign the app.** Performance as the product identity with measurable targets; GitHub Desktop layout with checkbox commit selection (staged at commit time); v1 adds partial commits, discard, stash, amend and History details; no clone or GitHub integration; one Win32 worker thread for Git jobs; ships as one release. See `DESIGN.md` and `ARCHITECTURE.md`.
- [x] N2 **Make a logo.** Flat orange kraken mascot (`docs/logo/surume.svg`), built into `src/surume.ico` (16 to 256 px) and embedded in the exe via `src/surume.rc`; also the window and taskbar icon. The minimal-mark and pixel-art concepts are kept in `docs/logo/concepts/`.
- [x] C1 **Rename Gitaluna to Surume** everywhere: `src/` (window title, log names, platform code) and the docs.
- [x] C2 **Remove CMake.** Replaced with a plain `build.bat`; exe to `bin/`, intermediates to `obj/`.
- [x] C3 **Tidy the layout.** Docs in `docs/`, lowercase snake_case source files, `.hpp` headers, debug log in `%APPDATA%\Surume`.
- [x] C4 **Merge modules.** `app` and `platform_win32` folded into `main.cpp`; `ui` renamed to `gui`.
- [x] C5 **Convert `src/` to `dev-standards/cpp/cpp.md`.** snake_case with module prefixes, no namespaces, Allman braces, named constants, no lambdas, iostreams or member functions, `bool` plus out-parameter error handling, `/W4 /WX`.
- [x] G6 **Fix `git.cpp` command and parsing bugs** (with the conversion): `CreateProcessW` with UTF-8 to UTF-16 conversion, Windows command-line quoting rules (backslashes, quotes, empty arguments), `git status --porcelain -z` (renames as new/old, no quoted paths), and `-c core.quotepath=false` on every command. Verified against a throwaway repo with a non-ASCII path, a non-ASCII file name with spaces and quotes, a rename, and a commit message with backslashes, quotes and emoji.
- [x] U8 **Fix branch picker iterating a list it replaces.** Checking out a branch refreshed `state.branches` while the picker was looping over it.
