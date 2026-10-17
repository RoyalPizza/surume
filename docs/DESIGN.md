# Surume Design

## Summary

Surume is a native Windows Git client built with C++ and Dear ImGui. Its reason to exist is **performance**: it should start instantly, use little memory, and never freeze, in a space where the popular clients are heavy Electron apps. It follows GitHub Desktop's proven layout and workflow, then improves individual workflows where real use shows a better way.

The application should feel immediate and practical rather than decorative: a compact, tool-like interface with predictable controls and no waiting on the UI.

## Performance Targets

These are the measurable form of the product's identity. Treat a regression against them as a bug.

- **Startup:** window visible and interactive in under 200 ms; repository state fills in as background jobs finish.
- **Never blocking:** no Git command runs on the UI thread. Every click gets visual feedback within one frame.
- **Idle cost:** close to zero CPU and GPU when nothing changes; the app redraws on input and job results, not continuously.
- **Memory:** under 50 MB for a typical repository.
- **Big repos and files:** a working tree with 10,000 changed files and a 100,000-line diff both scroll smoothly (only visible rows are drawn).
- **Footprint:** one executable with no installer, runtime or bundled browser.

## Current Status

As of 2026-10-17. Task IDs refer to `TASKS.md`.

| Area | Status |
| --- | --- |
| Window, high-DPI, system fonts, menu row, fixed layout with splitter | Done |
| Open repository, repo-root validation, recent list, restore last repo | Done |
| Remove a repository from the recent list, health warnings | Not started (F9) |
| Missing-Git error state | Done |
| All Git work on a background worker; redraw only on change | Done |
| Refresh on window focus and F5 | Done |
| Changed files with checkboxes, status badges, conflicted state, filter | Done |
| Partial commits (line and hunk checkboxes) | Not started (F15) |
| Commit selected files, amend, discard, stash | Done |
| Diff against HEAD with clipping; binary and large-diff placeholders | Done |
| Branch picker with markers; create, switch (stash prompt), delete | Done |
| Ahead/behind counts | Done |
| Sync button, auto-fetch | Not started (F2, F4) |
| History list, commit details, per-file commit diffs | Done |
| Settings that take effect (Git path, default folder) | Not started (F11) |
| Keyboard shortcuts, context menus | Partial (F13) |

## Product Goals

- Be the fastest Git client to open, use, and leave running (see Performance Targets).
- Keep the main commit workflow visible at all times.
- Make common Git workflows understandable without hiding Git's model, except where hiding it makes the common case simpler (the commit checkboxes).
- Provide clear repository, branch, and remote state.
- Prefer explicit, recoverable actions over surprising automation. Destructive actions confirm first.
- Work the same with any Git remote. No accounts, sign-in, or host-specific APIs.
- Stay native, small, and easy to build from source.

## Non-Goals

- Replacing a full IDE or a merge tool. Conflicts are edited in the user's editor.
- Implementing every Git command.
- GitHub (or any host) integration in the first version.
- Cross-platform support.
- Project management, issues, or CI dashboards.

## Target Users

- Developers who want a fast visual Git client that stays out of the way.
- People coming from GitHub Desktop who want the same flow without the weight.
- Experienced users who want quick commit and sync workflows without opening a terminal.

## Core Workflows

### Open a Repository

The current repository is shown as a button in the header bar. Clicking it opens a popup listing recent repositories; switching is a single click. An "Open..." entry at the bottom opens a folder dialog. There is no clone in the first version: Surume opens existing local repositories only.

Only repository root folders are accepted. Validation uses `git rev-parse --show-toplevel`, not a `.git` check. A folder outside any work tree, or a subfolder of one, is rejected with a clear error and not added to the recent list.

An opened repository becomes active immediately and moves to the top of the recent list. The recent list persists between launches. On startup, Surume reopens the last active repository; if it is gone or invalid, the app shows a warning and falls back to the empty state.

Surume requires an installed `git` executable. If Git cannot be found, the app says so clearly and disables every action that needs it.

Users can:

- Switch to a recent repository from the header popup.
- Open a local repository with the folder dialog.
- Remove a repository from the recent list without touching it on disk.
- See a warning when a repository's `.git` is missing, inaccessible, or corrupted.

### Review and Commit Changes

The Changes tab lists every changed file with a **checkbox** and a status badge (added, modified, deleted, renamed, untracked, conflicted). The checkboxes decide what goes into the next commit. This is GitHub Desktop's model: the index is managed by Surume, not shown.

Checkbox rules:

- Ticking and unticking is app state only. It is instant and runs no Git command.
- New changes appear **ticked** by default; the common case is committing everything.
- A header row shows the file count and a tick-all checkbox (ticked, unticked, or mixed).
- Ticks are remembered per path across refreshes, so a background refresh never loses the user's selection.
- The commit button reads `Commit N files to <branch>` and is disabled when nothing is ticked or the summary is empty.

On commit, Surume makes the index match the ticks, then commits:

1. Reset the index to `HEAD`.
2. Stage the ticked files (and the selected lines of partially ticked files).
3. Run `git commit` with the summary and description.

Because Surume rebuilds the index at commit time, any staging done from the command line is replaced by the checkbox selection. Files that were already staged when the repository was loaded appear ticked, so nothing staged outside Surume is silently dropped from view.

**Partial commits.** Selecting a file shows its diff on the right with a checkbox in the gutter of every added or removed line, and one on every hunk header. Unticking lines removes them from the commit; the file's checkbox then shows the mixed state. At commit time, Surume builds a patch from the selected lines and applies it to the index with `git apply --cached`.

**Discard changes.** Right-clicking a file (or a multi-selection) offers "Discard changes...", which restores the file from `HEAD` (deleting it if untracked) after a confirmation. "Discard all changes..." in the Repository menu does the same for every file.

**Amend.** An "Amend last commit" checkbox in the commit footer loads the last commit's message into the summary and description and commits with `--amend`. If the last commit is already on the upstream, the footer warns that amending rewrites published history.

**Stash.** "Stash all changes" in the Branch menu stashes every change, including untracked files. When a stash exists for the current branch, a "Stashed changes" row appears at the bottom of the Changes list; selecting it shows its diff with "Restore" and "Discard" buttons. Switching branches with uncommitted changes offers to stash them first.

The diff is a unified text diff in the right pane, rendered as scrollable monospaced text with coloring for additions, deletions, hunk headers, and context. Binary files and diffs over a size limit show a placeholder instead. Syntax highlighting, word-level diffs, side-by-side view, and image diffs come later.

The file filter at the top of the list searches paths only, case-insensitively, and shows an empty-result message when nothing matches.

### Branching

The current branch is shown in the header bar; clicking it opens the branch picker.

- Lists every branch with a marker for local-only, remote-only, and local with an upstream.
- Switching to a remote-only branch creates a local tracking branch.
- Creates a new branch from `HEAD`.
- Deletes a local branch when Git considers it safe (`branch -d`).
- Shows ahead/behind counts for the current branch when it has an upstream.

### Sync

The header has one contextual sync button instead of separate fetch, pull, and push buttons:

- `Fetch <remote>` when the branch is even with its upstream.
- `Pull <remote>` when behind, and also when both ahead and behind (the pull must happen before a normal push).
- `Push <remote>` when ahead.
- `Publish branch` when the branch has no upstream and a remote is available.
- Disabled, with a clear reason, when there are no remotes, `HEAD` is detached, conflicts block the action, or Git reports an authentication or upstream problem.

Remote selection:

- If the branch has an upstream, use its remote.
- Otherwise use `origin` if it exists.
- If there is no `origin` and several remotes exist, ask which one to publish to; never guess.
- With no remotes, sync is disabled with a clear no-remote state.

Network operations show progress, can be cancelled, and have no fixed timeout.

**Conflicts (minimum).** Full conflict handling (mark resolved, continue, abort) comes later. In the first version, when a pull or stash restore leaves conflicts, conflicted files show a distinct badge and the app explains that the merge must be finished in an editor or the terminal.

### Auto-Fetch

When enabled in settings, Surume fetches in the background on a configurable interval and updates the ahead/behind counts. The comparison is against the current branch's upstream.

### History

The History tab lists commits on the current branch: short hash, subject, author, and relative date. Selecting a commit shows its full message, author, date, and changed files in the right pane; selecting a changed file shows that file's diff for the commit.

## User Interface

### Main Window Layout

The layout follows GitHub Desktop:

- **Menu row:** File, Edit, View, Repository, Branch, Help.
- **Header bar:** repository picker, branch picker, sync button.
- **Left column:** Changes and History tabs.
  - Changes: filter, file list with checkboxes and tick-all, commit footer (summary, description, Amend checkbox, commit button).
  - History: commit list.
- **Right pane:** the selected file's diff (with line checkboxes), the selected commit's details, the stash view, or an empty-state message.

The commit footer stays visible in the Changes tab because committing is the primary loop.

### Empty State

With no repository open, the right pane shows one primary action, `Open repository...`. It is not a landing page or onboarding flow.

### Visual Style

- Vanilla Dear ImGui dark style. No custom theming yet.
- Dense, readable desktop-tool layout with fixed, resizable panels (no docking).
- Clear disabled states, with a tooltip saying why an action is unavailable.
- Color only for Git status and risk, never decoration.

### Interaction Principles

- Every Git operation runs in the background. While one runs, the UI shows what is happening, and actions that would conflict with it are disabled.
- Keyboard shortcuts for frequent actions (commit, refresh, switch tab, sync).
- Context menus for files and branches.
- Confirmation before destructive actions: discard, delete branch, amend a published commit.
- Errors in plain language, with Git's raw output behind a details toggle.
- The repository refreshes when the window regains focus and on F5.

## Settings

- Git executable path (empty means use `PATH`).
- Default folder for the open dialog.
- Auto-fetch enabled and interval.
- External editor path.
- Confirmation preferences for destructive actions.

Settings save as soon as they change.

## Later

Deliberately out of the first version, in rough priority order:

- Conflict handling: mark resolved, continue or abort a merge or rebase.
- Clone by URL.
- Branch graph in History.
- GitHub (or other host) integration: sign-in, browse repositories, pull requests.
- Syntax highlighting, side-by-side and word-level diffs.
- Themes.

## First Version Scope

The first version ships as one release, when all of this works:

- Everything already marked Done in Current Status.
- Background Git jobs; no Git call on the UI thread (F12).
- Redraw only on change (I4), list and diff clipping (U5).
- Refresh on window focus and the F5 key (task F5).
- Missing-Git error state (U4).
- Checkbox commit selection (F1) and partial commits (F15).
- Discard (F16), stash (F17), amend (F18).
- Conflicted-file badge and explanation (G4).
- File path filter (F7); binary and large-file placeholders (F10).
- Branch markers (F8), ahead/behind (F3), sync button (F2), auto-fetch (F4).
- History commit details (F6).
- Remove from recent list and health warnings (F9).
- Settings that take effect (F11), saved on change (I5).
- Keyboard shortcuts and context menus (F13).
- Tests for Git output parsing (F14).
