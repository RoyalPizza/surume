# Gitaluna Architecture

## Language and UI

- C++20 or newer.
- Dear ImGui for immediate-mode UI.
- Windows v1 uses a native Win32 window with Dear ImGui's Win32 + Direct3D 11 backend, matching the Pixeluna-style startup and rendering stack.
- Other platform backends are deferred.
- CMake as the build system.

## Engineering Principles

- Use simple C-style C++: `.cpp` and `.h` files, plain structs, free functions, explicit state, minimal inheritance, and minimal template cleverness.
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

The first version requires an installed `git` executable. Gitaluna should detect Git at startup or before the first Git-backed action, show a clear missing-Git error, and avoid repository actions that cannot run without Git.

## Major Modules

- `App`: application startup, main loop, shutdown.
- `Ui`: Dear ImGui screens, panels, dialogs, style, and layout.
- `Git`: Git command execution, parsing, repository state, status, diff, branch, commit, and sync operations.
- `Model`: application state, repositories, selected file, selected branch, selected commit, settings.
- `Jobs`: background task queue for Git operations.
- `Platform`: filesystem dialogs, process launching, paths, clipboard, OS integration. Platform-specific implementations per OS (e.g., `platform_win32.cpp`).
- `Persistence`: recent repositories, window layout, user preferences. Serialization via TOML using tomlc17.
- `Logging`: diagnostic logs and user-visible operation results.

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

Persist locally under the user's platform app-data folder using TOML (tomlc17). The app-data folder name is `Gitaluna`; on Windows this resolves to `%APPDATA%\Gitaluna`.

Repository history and general settings are stored in separate files:

- `settings.toml`
- `repositories.toml`

The settings file stores:

- Window size and panel layout.
- UI preferences.
- Git executable path.
- Default open directory.
- Other non-secret app settings.

The repository history file stores:

- Recent repositories.
- Last active repository.

Settings files are stored under app data, not inside repositories.

Do not store credentials directly. Rely on Git credential helpers, OS keychains, or future dedicated auth integration.

## Build Layout

- Binaries output to `bin/`.
- Intermediate files output to `obj/`.
- Two CMake targets: `gitaluna` (release) and `gitaluna_debug` (debug).

## Testing Strategy

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

- Should the first diff viewer be custom-rendered ImGui text or a dedicated syntax-highlighted component?
- What is the minimum supported OS set?
