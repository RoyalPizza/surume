# Tasks

Surume is the Gitaluna codebase moved into its own repo. The plan is to redesign it rather than keep building the old design.

## Next

- [ ] **Redesign the app.** Revisit `DESIGN.md` and `ARCHITECTURE.md` before writing more code; treat the current code as a starting point, not the plan.
- [ ] **Make a logo.** An orange kraken (Surume is the kraken from One Piece). Needs an app icon (`.ico`) as well, embedded via a `.rc` resource file.

## Bugs

### Git (`git.cpp`)

- [ ] **8.3 short paths and `subst` drives are rejected.** `git_get_repo_root` compares the chosen path to Git's long-form top-level path as text, so a path like `C:\Users\ROYALP~1\repo` fails. The folder picker returns long paths, so this only affects paths typed or restored from elsewhere. Normalize with `GetLongPathNameW` or `GetFinalPathNameByHandleW` first.
- [ ] **Diffs show only the staged part** of a file that has both staged and unstaged changes.
- [ ] **Commit failure shows an empty reason.** With nothing staged, Git writes "nothing to commit" to stdout, not stderr.
- [ ] **Conflicts look like modifications.** Unmerged files (`UU`, `AA`, ...) get the yellow `M` badge.
- [ ] **`checkout <name>` is ambiguous** if a branch and a file share a name. Use `git switch`.

### GUI (`gui.cpp`)

- [ ] **View > Changes / History does nothing.** It sets `state.nav_mode`, but the tab bar never reads it.
- [ ] **File > Exit does nothing.**
- [ ] **Commit button is enabled when nothing is staged.**
- [ ] **Missing Git isn't handled.** `is_git_found` is never checked by the GUI: no error is shown and repo actions stay enabled.
- [ ] **Large diffs are slow.** `render_diff` draws every line every frame; use `ImGuiListClipper`.
- [ ] **Commit text carries across repo switches**, and the 1024-byte description buffer truncates silently.
- [ ] **Long relative dates** ("2 years, 3 months ago") overlap the hash in History.

### App (`main.cpp`, `persistence.cpp`)

- [ ] **Settings aren't saved for non-ASCII Windows usernames.** `fopen_s` and `toml_parse_file_ex` take ANSI paths. Use wide-char file APIs.
- [ ] **Likely spins the CPU while minimized.** `Present(1, 0)` returns immediately when occluded; check for `DXGI_STATUS_OCCLUDED` and sleep, like the ImGui example does.
- [ ] **`window_shutdown` destroys an already-destroyed window** (harmless, but wrong).
- [ ] **`imgui.ini` is written to the working directory.** The app owns its layout, so set `io.IniFilename = nullptr` and drop the `.gitignore` entry.

## Not Implemented

- [ ] Stage / unstage files (single and all).
- [ ] Sync button: fetch, pull, push, publish branch.
- [ ] Ahead/behind counts.
- [ ] Auto-fetch (settings exist but are unused).
- [ ] Repo refresh when the window regains focus, and on F5. The file list is stale until the app runs a Git action.
- [ ] History: commit details and changed files.
- [ ] Changed-file path filter.
- [ ] Branch state markers (local / remote / tracking all render the same).
- [ ] Remove a repository from the recent list; repository health warnings.
- [ ] Binary and large-file diff placeholders.
- [ ] Settings that take effect: Git executable path, default open directory.
- [ ] Background jobs so Git calls don't block the UI (and the hardcoded 15s timeout doesn't kill fetch/push).
- [ ] Keyboard shortcuts and context menus.
- [ ] Tests: Git output parsing against a throwaway repo.

## Improvements

- [ ] High-DPI support (`ImGui_ImplWin32_EnableDpiAwareness` or a manifest); the window is blurry on scaled displays.
- [ ] Load a TTF font with wider glyph coverage so non-ASCII file names don't render as `?`.
- [ ] Add `/utf-8` to `build.bat`; `gui.cpp` contains UTF-8 characters.
- [ ] Only redraw when something changes instead of rendering at 60 fps while idle.
- [ ] Save settings when they change, not only on a clean exit.
- [ ] Stop the GUI calling Git directly; route actions through request flags handled in `main.cpp`, like Open Repository.
- [ ] Clean up `.gitignore`: drop the leftover CMake, Visual Studio project, compiler output and `*.log` entries.
- [ ] Add a `compile_flags.txt` so clangd in VS Code finds the ImGui and tomlc17 headers.

## Done

- [x] **Rename Gitaluna to Surume** everywhere: `src/` (window title, log names, platform code) and the docs.
- [x] **Remove CMake.** Replaced with a plain `build.bat`; exe to `bin/`, intermediates to `obj/`.
- [x] **Tidy the layout.** Docs in `docs/`, lowercase snake_case source files, `.hpp` headers, debug log in `%APPDATA%\Surume`.
- [x] **Merge modules.** `app` and `platform_win32` folded into `main.cpp`; `ui` renamed to `gui`.
- [x] **Convert `src/` to `dev-standards/cpp/cpp.md`.** snake_case with module prefixes, no namespaces, Allman braces, named constants, no lambdas, iostreams or member functions, `bool` plus out-parameter error handling, `/W4 /WX`.
- [x] **Fix `git.cpp` command and parsing bugs** (with the conversion): `CreateProcessW` with UTF-8 to UTF-16 conversion, Windows command-line quoting rules (backslashes, quotes, empty arguments), `git status --porcelain -z` (renames as new/old, no quoted paths), and `-c core.quotepath=false` on every command. Verified against a throwaway repo with a non-ASCII path, a non-ASCII file name with spaces and quotes, a rename, and a commit message with backslashes, quotes and emoji.
- [x] **Fix branch picker iterating a list it replaces.** Checking out a branch refreshed `state.branches` while the picker was looping over it.
