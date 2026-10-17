# Surume Architecture

## Language and UI

- C++20 or newer.
- Dear ImGui for immediate-mode UI.
- Windows v1 uses a native Win32 window with Dear ImGui's Win32 + Direct3D 11 backend.
- Windows only. There are no plans for other platforms at the moment, so Win32 code lives directly in `main.cpp` with no platform abstraction layer.
- No build system: a plain `build.bat` calls MSVC `cl` directly.

## Engineering Principles

- Use simple C-style C++: `.cpp` and `.hpp` files, plain structs, free functions, explicit state, minimal inheritance, and minimal template cleverness.
- Prefer code that is easy to step through in a debugger.
- Keep modules split by responsibility without building a deep framework layer.
- Keep Dear ImGui panel code close to the state and operations it edits.
- Use a central application state struct for selected repository, branch, file, commit, active jobs, settings, and transient UI state.
- Use Dear ImGui directly instead of wrapping it in a custom UI framework too early.
- Use fixed, application-owned resizable panels instead of dockable windows for v1.
- Save splitter sizes and panel preferences as app settings.
- Use vanilla Dear ImGui styling for v1. Do not add custom theming yet.
- Use ImGui style colors and widgets where practical, with hardcoded colors reserved for semantic Git states such as added, deleted, modified, and conflicted.
- Use icon buttons with tooltips for repeated toolbar-style actions such as refresh, fetch, pull, push, stage, unstage, branch, and settings.
- Prefer conservative dependencies and avoid adding a library for behavior that can be implemented simply.
- Prefer shipping as a single executable when practical.
- Embed small required assets, such as icon fonts, at build time when practical.
- Store app preferences under the user's platform app-data folder rather than inside repositories.
- Keep comments short and useful, focused on intent, constraints, or non-obvious behavior.
- Avoid expensive work in the per-frame UI path; cache repository state and run Git work in background jobs.
- Use machine-readable external command output whenever possible instead of parsing human-oriented text.

## Git Backend

There are two viable backend paths:

1. Shell out to the installed `git` executable.
2. Use `libgit2` for direct repository access.

The recommended first implementation is shelling out to `git`.

Reasons:

- Matches command-line Git behavior closely.
- Avoids early dependency complexity.
- Handles credential helpers and host-specific auth flows through the user's existing Git setup.
- Lets the app mature its UI and workflow model before committing to a deeper Git abstraction.

The Git layer should still be isolated behind a service boundary so that selected operations can move to `libgit2` later if useful.

The first version requires an installed `git` executable. Surume should detect Git at startup or before the first Git-backed action, show a clear missing-Git error, and avoid repository actions that cannot run without Git.

## Major Modules

- `main.cpp`: entry point and everything Windows-specific. Win32 window, D3D11 and ImGui backend setup, frame present, the folder dialog, Open in Explorer/Terminal, and the app-data path. Also app startup (Git detection, loading persistence, restoring the last repository), the main loop, shutdown, and deferred requests from the UI (open repository, open in Explorer/Terminal, repository switch, diff load), handled after each frame is presented.
- `Gui`: Dear ImGui screens, panels, dialogs, and layout.
- `Git`: Git command execution and parsing for status, diff, branches, commits, and log, plus `RefreshRepoState` to reload all of it into `AppState`. Sync operations are not implemented yet.
- `Model`: header-only. The central `AppState` struct and the plain data types it holds.
- `Jobs`: background task queue for Git operations. Not implemented yet.
- `Persistence`: recent repositories, panel layout, user preferences. Serialization via TOML using tomlc17.
- `Log`: diagnostic log file, compiled in only for debug builds (`SURUME_DEBUG`). User-visible operation results go through `AppState::statusMessage`.

## Git Command Execution

Git operations should run through one command runner with:

- Working directory.
- Argument vector, not shell string interpolation.
- Timeout support.
- Captured stdout, stderr, and exit code.
- Cancellation where practical.
- Structured result objects.
- Redaction for credentials or sensitive URLs in logs.

UI code should not directly construct process calls.

Repository validation should use Git rather than filesystem-only checks. The first version should validate opened folders with:

- `git -C <folder> rev-parse --show-toplevel`

The selected folder must match the returned top-level path after path normalization. If Git reports a different top-level path, the selected folder is a subfolder and should be rejected. This keeps v1 strict: users open repository roots only.

The first version should implement the full core GitHub Desktop-style local workflow: current branch, working tree status, changed file list, diff viewing, staging, committing, branch switching/creation, history, fetch, pull, push, and ahead/behind state.

## Application State

The app keeps a central state object containing:

- Open repositories.
- Active repository.
- Active branch.
- Active upstream remote and branch when one exists.
- Repository status snapshot.
- Selected navigation mode.
- Selected file.
- Selected commit.
- In-flight jobs.
- Last operation messages.
- User preferences.

Repository state should be refreshed after mutating operations such as stage, unstage, commit, checkout, pull, and push.

## Background Jobs

All potentially slow operations run outside the UI frame:

- Repository status scan.
- Diff generation.
- Fetch, pull, and push.
- Commit history loading.
- Branch listing.

The UI displays progress and stale-state indicators while jobs are active.

Current code: there is no job system yet. Every Git call runs synchronously, on the UI thread, with a hardcoded 15 second timeout. Some calls happen from UI code mid-frame (checkout, branch create/delete, commit), and others are deferred to the main loop.

## Data Parsing

Prefer machine-readable Git output where available:

- `git status --porcelain=v2 --branch`
- `git diff --numstat`
- `git diff --name-status`
- `git log --format=...`
- `git branch --format=...`
- `git rev-parse --abbrev-ref --symbolic-full-name @{u}`
- `git remote`

Avoid parsing human-oriented output unless no structured form exists.

Current code uses `git status --porcelain -u` (porcelain v1, no `--branch`), `git branch --format=...`, `git branch -r --format=...`, `git log --format=...`, `git symbolic-ref --short HEAD`, and `git diff` / `git diff --cached` / `git diff --no-index`. Upstream, remote, and numstat queries are not used yet.

Sync behavior should be based on the current branch's upstream remote when available. If no upstream exists, `origin` is the preferred default for fetch/publish language. If no upstream exists and multiple remotes exist without `origin`, the app should require an explicit remote choice before publishing instead of guessing.

## Error Handling

Errors should be shown in plain language with expandable technical details.

Examples:

- "This branch has no upstream branch."
- "Pull could not complete because there are merge conflicts."
- "Authentication failed while contacting the remote."
- "Git is not installed or could not be found."

The raw Git stderr can be shown behind a details disclosure for diagnosis.

## Persistence

Persist locally under the user's platform app-data folder using TOML (tomlc17). The app-data folder name is `Surume`; on Windows this resolves to `%APPDATA%\Surume`.

Repository history and general settings are stored in separate files:

- `settings.toml`
- `repositories.toml`

The settings file stores:

- Left column width (`left_column_width`).
- Git executable path (`git_path`).
- Default open directory (`default_open_dir`).
- Auto-fetch enabled and interval (`auto_fetch_enabled`, `auto_fetch_interval_min`).

Window size and other UI preferences are not stored yet. The Git path, default open directory, and auto-fetch settings are saved but not yet used: Git always runs as `git` from `PATH`, and the folder dialog ignores the default directory.

The repository history file stores:

- Recent repositories (`repos`).
- Last active repository (`active`).

Both files are read once at startup and written only on clean shutdown.

Settings files are stored under app data, not inside repositories.

Do not store credentials directly. Rely on Git credential helpers, OS keychains, or future dedicated auth integration.

## Build Layout

- `build.bat` builds a debug executable; `build.bat release` builds an optimized one.
- The executable goes to `bin/surume.exe` and intermediate files to `obj/`.
- Debug builds define `SURUME_DEBUG`, which enables the log file `%APPDATA%\Surume\surume_debug.log` (overwritten on each launch).

## Testing Strategy

No tests exist yet. This section is the plan.

### Unit Tests

- Git porcelain parsers.
- Branch and status models.
- Command result handling.
- Settings serialization.

### Integration Tests

Use temporary repositories to verify:

- Status detection.
- Stage and unstage.
- Commit creation.
- Branch creation and checkout.
- History loading.
- Diff retrieval.

### Manual QA

Exercise repositories with:

- Clean working tree.
- Untracked files.
- Staged and unstaged changes.
- Deleted and renamed files.
- Merge conflicts.
- Detached HEAD.
- No remote.
- Ahead and behind tracking branch state.

## Open Questions

- Should the diff viewer stay custom-rendered ImGui text (the current implementation) or move to a dedicated syntax-highlighted component?
- What is the minimum supported OS set?
