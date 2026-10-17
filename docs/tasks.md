# Tasks

Surume is the Gitaluna codebase moved into its own repo. The plan is to redesign it rather than keep building the old design.

## Next

- [ ] **Redesign the app.** Revisit `design.md` and `architecture.md` before writing more code; treat the current code as a starting point, not the plan.
- [x] **Rename Gitaluna to Surume** everywhere: `CMakeLists.txt`, `src/` (window title, log names, platform code) and the docs.
- [ ] **Make a logo.** An orange kraken (Surume is the kraken from One Piece). Needs an app icon (`.ico`) as well.
- [x] **Remove CMake.** Replace it with a plain `build.bat`, like the other C++ repos (plc-toolbox, plc-enet-tool), and drop the CMake ignores from `.gitignore`.
