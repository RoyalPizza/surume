# Surume

A native Windows Git client in the spirit of GitHub Desktop, written in C++ with [Dear ImGui](https://github.com/ocornut/imgui). Named after the giant kraken from One Piece.

## Status

Early and mid-redesign. The current code is the old Gitaluna codebase: it can open repositories, show changes and diffs, commit, switch/create/delete branches, and list history. Staging, sync (fetch/pull/push), and background Git operations are not done yet. See [docs/DESIGN.md](docs/DESIGN.md#current-status) for the full breakdown and [docs/TASKS.md](docs/TASKS.md) for what's next.

## Requirements

- Windows 10 or 11
- Visual Studio 2022 or Build Tools with the **Desktop development with C++** workload
- Git on `PATH` (Surume runs the `git` executable)

## Building

Clone with submodules:

```
git clone --recurse-submodules https://github.com/RoyalPizza/surume.git
```

Or, in an existing clone:

```
git submodule update --init
```

Then build:

```
build.bat           # debug
build.bat release   # optimized
```

`build.bat` finds MSVC on its own, so it runs from any terminal. The executable is written to `bin\surume.exe`.

Debug builds write a log to `%APPDATA%\Surume\surume_debug.log`. Settings and recent repositories are stored in the same folder.

## Layout

```
src/          application code
docs/         design, architecture, and task docs
third-party/  Dear ImGui and tomlc17 (git submodules)
bin/          build output: executable only
obj/          build output: intermediate files
build.bat     build script
```

## Docs

- [DESIGN.md](docs/DESIGN.md): product spec and current status
- [ARCHITECTURE.md](docs/ARCHITECTURE.md): technical design
- [TASKS.md](docs/TASKS.md): task list
