# Tasks

Surume is the Gitaluna codebase moved into its own repo. The plan is to redesign it rather than keep building the old design.

## Next

- [ ] **Convert `src/` to `dev-standards/cpp/cpp.md`.** Before any other work. snake_case functions/variables/members with module prefixes (`git_get_status`), no namespaces, Allman braces with braces on every `if`/`for`, `CONSTANT_CASE` constants and enum members, no lambdas, no iostreams (`std::istringstream` in `git.cpp`), `auto` only where the standard allows, header comments on every function. Build with `/W4 /WX`. Keep the project overrides: `.hpp` headers, `bin/` + `obj/` output. Fix the four `git.cpp` bugs below (`CreateProcessW`, quoting, renames, quoted paths) in the same pass, since `git_run` and status parsing get rewritten anyway.
- [ ] **Redesign the app.** Revisit `DESIGN.md` and `ARCHITECTURE.md` before writing more code; treat the current code as a starting point, not the plan.
- [ ] **Make a logo.** An orange kraken (Surume is the kraken from One Piece). Needs an app icon (`.ico`) as well, embedded via a `.rc` resource file.

## Bugs

### Git (`git.cpp`)

- [ ] **Non-ASCII text is garbled.** `Git::Run` uses `CreateProcessA`, so UTF-8 commit messages and non-ASCII repo paths are read in the ANSI code page. Convert to UTF-16 and use `CreateProcessW`.
- [ ] **Argument quoting is wrong.** `QuoteArg` doesn't double backslashes before a `"` or at the end of a quoted argument, and drops empty arguments. A commit summary ending in `\` or containing `\"` merges or splits arguments.
- [ ] **Renames are parsed backwards.** Porcelain v1 prints `old -> new`; `GetStatus` stores the old path as `path`. The diff shows the wrong file and the tooltip says "Renamed from" the new name.
- [ ] **Quoted paths are never unescaped.** Paths with spaces, quotes or non-ASCII characters come back C-quoted; only the outer quotes are stripped. Quoted renames break entirely. Use `git status --porcelain -z` (fixes the rename bug too) and `-c core.quotepath=false`.
- [ ] **Diffs show only the staged part** of a file that has both staged and unstaged changes.
- [ ] **Commit failure shows an empty reason.** With nothing staged, Git writes "nothing to commit" to stdout, not stderr.
- [ ] **Conflicts look like modifications.** Unmerged files (`UU`, `AA`, ...) get the yellow `M` badge.
- [ ] **`checkout <name>` is ambiguous** if a branch and a file share a name. Use `git switch`.

### GUI (`gui.cpp`)

- [ ] **View > Changes / History does nothing.** It sets `state.navMode`, but the tab bar never reads it.
- [ ] **File > Exit does nothing.**
- [ ] **Commit button is enabled when nothing is staged.**
- [ ] **Missing Git isn't handled.** `gitFound` is never checked: no error is shown and repo actions stay enabled.
- [ ] **Large diffs are slow.** `RenderDiff` draws every line every frame; use `ImGuiListClipper`.
- [ ] **Commit text carries across repo switches**, and the 1024-byte description buffer truncates silently.
- [ ] **Long relative dates** ("2 years, 3 months ago") overlap the hash in History.

### App (`main.cpp`, `persistence.cpp`)

- [ ] **Settings aren't saved for non-ASCII Windows usernames.** `fopen_s` and `toml_parse_file_ex` take ANSI paths. Use wide-char file APIs.
- [ ] **Likely spins the CPU while minimized.** `Present(1, 0)` returns immediately when occluded; check for `DXGI_STATUS_OCCLUDED` and sleep, like the ImGui example does.
- [ ] **`ShutdownWindow` destroys an already-destroyed window** (harmless, but wrong).
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
- [ ] Add `/utf-8` to `build.bat`; sources contain UTF-8 characters.
- [ ] Only redraw when something changes instead of rendering at 60 fps while idle.
- [ ] Save settings when they change, not only on a clean exit.
- [ ] Stop the GUI calling Git directly; route actions through request flags handled in `main.cpp`, like Open Repository.
- [ ] Clean up `.gitignore`: drop the leftover CMake, Visual Studio project, compiler output and `*.log` entries.

## Done

- [x] **Rename Gitaluna to Surume** everywhere: `src/` (window title, log names, platform code) and the docs.
- [x] **Remove CMake.** Replaced with a plain `build.bat`; exe to `bin/`, intermediates to `obj/`.
- [x] **Tidy the layout.** Docs in `docs/`, lowercase snake_case source files, `.hpp` headers, debug log in `%APPDATA%\Surume`.
- [x] **Merge modules.** `app` and `platform_win32` folded into `main.cpp`; `ui` renamed to `gui`.
