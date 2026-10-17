# Surume Design

## Summary

Surume is a native C++ desktop Git client inspired by GitHub Desktop, built with Dear ImGui. The goal is to provide a fast, focused, visual workflow for day-to-day Git tasks: choosing repositories, reviewing changes, staging files, committing, branching, fetching, pulling, pushing, and resolving common sync states.

The application should feel immediate and practical rather than decorative. Dear ImGui gives us a compact, tool-like interface with predictable controls, keyboard-friendly workflows, and minimal UI latency.

## Current Status

This document is the spec inherited from Gitaluna and is due for a redesign (see `TASKS.md`). The code implements part of it:

| Area | Status |
| --- | --- |
| Win32 + D3D11 window, vanilla dark ImGui style, app menu row | Done |
| Fixed layout: header bar, left column with Changes/History tabs, resizable splitter, right content area | Done |
| Open repository (folder dialog, repo-root validation), recent list, restore last repo on startup | Done |
| Remove a repository from the recent list, repository health warnings | Not started |
| Missing-Git detection | Partial: detected at startup, but no error is shown and repo actions stay enabled |
| Changed file list with status badges (`?`, `A`, `M`, `D`, `R`) | Partial: one flat list, no staged/unstaged grouping, no conflicted/ignored states |
| File path filter | Not started |
| Unified text diff with line coloring | Done |
| Binary and large-file placeholders | Not started |
| Stage / unstage (single or all) | Not started |
| Commit summary + description, commit staged changes | Done |
| Branch picker: list, switch (remote-only creates a tracking branch), create | Done |
| Branch state indicators (local / remote / tracking) | Not started: all three render the same |
| Delete local branch | Done, from the Branch menu (not the picker) |
| Ahead/behind counts | Not started |
| Sync button (fetch / pull / push / publish) | Not started: button is a placeholder labelled "Fetch origin" |
| Auto-fetch | Not started: settings are saved but unused |
| History: commit list | Done |
| History: commit details and changed files | Not started |
| Settings dialog | Partial: saves Git path, default open dir and auto-fetch, but none of them affect behavior yet |
| Async Git operations | Not started: every Git call blocks the UI thread |
| Keyboard shortcuts, context menus | Not started |
| File > Exit | Not working (menu item does nothing) |

## Product Goals

- Make common Git workflows understandable without hiding Git's model.
- Keep the main commit workflow visible at all times.
- Provide clear repository, branch, and remote state.
- Make file diffs and staged changes easy to inspect.
- Support GitHub-adjacent workflows where useful, while keeping the core app usable with any Git remote.
- Prefer explicit, recoverable actions over surprising automation.
- Stay native, lightweight, and easy to build from source.

## Non-Goals

- Replacing a full IDE.
- Implementing every Git command in the first version.
- Building a GitHub-only client.
- Hiding all command-line Git concepts.
- Providing complex project management, issue tracking, or CI dashboards in the first version.

## Target Users

- Developers who want a fast visual Git client.
- Beginners who understand commits and branches but benefit from guided UI.
- Experienced users who want quick staging, committing, and sync workflows without opening a terminal.

## Core Workflows

### Open or Add Repository

The current repository is shown as a button in the top bar. Clicking it opens a compact popup listing recent repositories. Switching is a single click from that list. An "Open..." entry at the bottom of the popup triggers a folder dialog to add a new repository. There is no separate full-screen add-repository flow in the first version.

The first version only accepts valid Git repository root folders. Validation uses Git itself rather than checking only for a `.git` directory. If the selected folder is not inside a Git work tree, or is a subfolder inside a larger repository, Surume rejects it and shows a clear error instead of adding it to the recent repository list.

When a repository is successfully opened, it immediately becomes the active repository and is added to the recent repository list.

The recent repository list persists between app launches.

On startup, Surume reopens the last active repository when possible. If that repository no longer exists or is no longer valid, the app shows a small warning, falls back to the no-repository empty state, and leaves the recent list available from the repository picker.

The first version uses real persistence under the user's platform app-data folder. General app settings and repository history are stored in separate files so preferences and recents can evolve independently.

The first version requires an installed `git` executable. If Git cannot be found, Surume shows a clear error and disables repository-opening actions that require Git.

Users can:

- Switch to a recent repository from the top bar popup.
- Open an existing local repository via folder dialog.
- Add a repository to the recent list.
- Remove a repository from the app list without deleting it from disk.
- See repository health warnings when `.git` is missing, inaccessible, or corrupted.

### Review Changes

The app shows:

- Changed files grouped by working tree and staged state.
- File status icons for added, modified, deleted, renamed, conflicted, ignored, and untracked files.
- A diff viewer for text files.
- A binary-file placeholder for files that cannot be diffed.
- A simple file path search filter for changed files.

The changes filter searches file names and paths only. It is case-insensitive, applies to both staged and unstaged groups, and shows an empty result message when no changed files match. Status filters, file type filters, and advanced query syntax are deferred.

The first version uses a unified text diff preview in the right content area. It should be generated from Git diff output and rendered as scrollable monospaced text with basic line coloring for additions, deletions, hunk headers, and unchanged context. Syntax highlighting, inline word-level diffing, side-by-side diffing, image diffs, and advanced code-editor behavior are deferred.

### Stage and Commit

Users can:

- Stage or unstage individual files.
- Stage or unstage all files.
- Enter a commit summary and optional description.
- Commit staged changes.
- Amend the previous commit in a later version.

The first version keeps the commit footer simple: summary, description, and commit button. Co-authors, sign-off, commit options menus, and other commit metadata/actions are deferred until they are deliberately designed.

### Branching

The current branch is shown in the top bar. Clicking it opens a branch picker.

Branch picker behavior:

- Lists all known branches with a visual indicator for each state: local-only, remote-only, and local with a remote tracking branch.
- Switching to a remote-only branch automatically creates a local tracking branch.
- Allows creating a new branch from the current HEAD.
- Allows deleting a local branch when safe.

Users can:

- View the current branch at a glance from the top bar.
- Switch to any local or remote branch.
- Create a branch from the current HEAD.
- Delete local branches when safe.
- See ahead and behind counts when a tracking branch exists.

### Sync

The header uses one contextual GitHub Desktop-style sync button instead of separate fetch, pull, and push buttons.

Users can:

- Fetch from the current branch's upstream remote.
- Pull when the current branch is behind its upstream.
- Push when the current branch is ahead of its upstream.
- See sync-blocking states clearly, such as conflicts, detached HEAD, no upstream branch, or authentication failure.

Sync button behavior:

- Shows `Fetch <remote>` when the branch is even with its upstream or the app needs updated remote state.
- Shows `Pull <remote>` when the branch is behind.
- Shows `Push <remote>` when the branch is ahead.
- Shows `Pull <remote>` when the branch is both ahead and behind, because pulling/rebasing/merging must be resolved before a normal push.
- Shows `Publish branch` when the branch has no upstream and a publish target is available.
- Is disabled with a clear status when the repository has no remotes, is detached, has conflicts that block the action, or Git reports an authentication/upstream problem.

Remote selection rules:

- If the current branch has an upstream, use that upstream's remote.
- If there is no upstream and `origin` exists, use `origin` for fetch and publish language.
- If there is no upstream and multiple remotes exist without `origin`, do not guess silently; require an explicit publish/remote choice before pushing.
- If there are no remotes, disable sync actions and show a clear no-remote state.

### Auto-Fetch

When enabled in settings, the app runs a background fetch on a configurable interval and updates the ahead/behind count in the top bar. The comparison is against the current branch's upstream, not a hardcoded branch name. The interval and enabled state are user-configurable.

### History

Users can:

- View a commit list for the current branch.
- Select a commit and inspect metadata.
- View changed files for a selected commit.

## User Interface

### Main Window Layout

The first version should closely match GitHub Desktop's layout as the baseline. Surume can diverge later, but v1 should start from a familiar, proven Git client shape.

The main window uses these regions:

- App menu row: File, Edit, View, Repository, Branch, Help.
- Header bar: current repository picker, current branch picker, and single contextual sync button.
- Left column: Changes and History tabs.
- Changes tab content: filter row, changed file list, staged/unstaged grouping, and status counts.
- Left column footer: commit summary, commit description, and commit button.
- Right content area: selected file diff, selected history details, or an empty-state panel when there is nothing selected.

Repository and branch navigation live in the header bar, not in a persistent repository sidebar.

The commit panel should remain available in the changes view because committing is the primary loop.

### App Menu

The first version includes a GitHub Desktop-style menu row:

- File
- Edit
- View
- Repository
- Branch
- Help

The first implementation may include disabled or placeholder menu items where the underlying feature is not implemented yet. The menu should establish the final command organization without requiring every command to work immediately.

### Navigation Modes

Initial navigation follows GitHub Desktop's primary tabs:

- Changes
- History

Branch selection and branch creation live behind the current-branch picker in the header bar. Settings can be reached from the app menu or a small header/menu action rather than occupying a main navigation mode.

Pull requests, issues, branches-as-a-full-page, and GitHub account views can arrive later.

### Empty State

When no repository is open, the main content area shows a simple empty state with one primary action:

- `Open repository...`

The empty state should not become a landing page or onboarding flow. It exists only to get the user into a local repository quickly.

### Visual Style

- Use vanilla Dear ImGui styling for v1. Do not build custom theming yet.
- Dense, readable desktop-tool layout.
- Stable panel sizes with resizable splitters.
- Fixed application-owned panels instead of dockable windows.
- Clear disabled states for unavailable Git actions.
- Icons for common actions where available.
- Color used sparingly for status and risk, not decoration.
- No marketing-style landing screen inside the app.

### Interaction Principles

- Keyboard shortcuts for frequent actions.
- Context menus for file and branch operations.
- Confirmation dialogs for destructive operations.
- Inline status messages for Git command results.
- Long operations run asynchronously and never freeze the UI.

## Settings

Initial settings:

- Git executable path.
- Default clone/open directory.
- Auto-fetch enabled and interval.
- External editor path.
- Confirmation preferences for destructive actions.

Later settings:

- Theme selection.
- GitHub account integration.
- SSH/HTTPS remote preferences.
- Diff options.
- Commit signing configuration visibility.

## GitHub Integration

GitHub-specific features should be layered on top of the generic Git client rather than baked into the core repository model.

Potential later features:

- Sign in with GitHub.
- Clone from GitHub.
- View pull requests for the active repository.
- Create pull request after pushing a branch.
- Open repository, branch, commit, or pull request in browser.

The first version can ship without GitHub API integration if local Git workflows are solid.

## First Version Scope

The first version should be a usable local Git client for the core GitHub Desktop workflow, not a staged shell with placeholder behavior.

First version includes:

- Plain `build.bat` build (no build system).
- Win32 + Direct3D 11 Dear ImGui app.
- GitHub Desktop-style fixed resizable panel layout.
- GitHub Desktop-style app menu row.
- Vanilla Dear ImGui styling.
- Installed Git detection with clear missing-Git error state.
- Recent repository list.
- Persisted recent repository list.
- Separate app-data files for settings and repository history.
- Open repository dialog.
- No-repository empty state with an `Open repository...` primary action.
- Valid Git repository root validation.
- Restore last active repository on startup, or show a warning and fall back to the empty state.
- Current repository and current branch display.
- Real working tree status.
- Changed file list with staged and unstaged grouping.
- Case-insensitive file path search for changed files.
- Text diff viewer.
- Binary and large-file placeholders.
- Stage and unstage individual files.
- Stage and unstage all files.
- Commit summary and description fields.
- Commit validation.
- Commit staged changes.
- Refresh repository state after mutating operations.
- Branch list.
- Branch switching.
- Branch creation.
- Local branch deletion when safe.
- Commit history list.
- Commit detail view.
- Fetch.
- Pull.
- Push.
- Single contextual sync button.
- Ahead/behind display.
- Clear error states for no upstream branch, detached HEAD, conflicts, authentication failure, and missing Git.

## Open Questions

- Should clone support be part of version 1, or should version 1 focus on existing local repositories?
- Should GitHub API integration be planned for version 1 or deferred until the local client feels complete?
