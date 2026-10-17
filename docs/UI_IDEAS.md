# UI Ideas

Ideas for making the UI easier to look at, from a review of screenshots on 2026-10-17. None of these are decided. The goal is a calmer version of the current layout, not a modern redesign: no rounded cards, gradients or large padding.

## What makes it tiring now

The app uses stock ImGui Dark or Light, following the Windows app mode (`apply_style`, `src/main.cpp`). The only custom colors are the Git status and diff palettes in `src/gui.cpp`. The notes below were written against dark mode.

- **Too much bright blue.** Tabs, selected rows, checkboxes, dropdowns, Fetch and the empty Summary box all use the same saturated blue. Nothing stands out, and the empty commit box looks selected.
- **Near-black background with bright white text.** Harsh contrast, and every area (sidebar, diff, commit box) is the same black, so the regions blur together.
- **No breathing room.** Diff text starts against the splitter. History rows and commit details sit flush against their edges.
- **Harsh diff colors.** Added lines are bright green text on a bright green bar. The `diff --git`, `index`, `---` and `+++` lines are noise at the top of every diff.
- **Small readability problems.** The hash and author on a selected history row are grey on blue. Dropdowns use the letter `v` as an arrow. The Changes/History tabs look like buttons.

## Ideas, roughly by payoff

1. **Muted classic grey theme.** Replace `StyleColorsDark` with a custom palette in the spirit of older Visual Studio dark or Sublime:
   - A few dark greys instead of near-black: one for the sidebar, one for the diff, one for inputs.
   - Soft off-white text instead of pure white.
   - One desaturated accent, used only for selection and the main button (Commit). Everything else grey.
   - Grey fills for inputs, checkboxes and dropdowns, not blue.
2. **Dark title bar.** Done: the app now follows the Windows light/dark app mode, including the title bar, and switches live. Still possible on Windows 11: `DWMWA_CAPTION_COLOR` to set the title bar to the app's exact background color so it blends in.
3. **Calmer diff colors.** Dark tinted backgrounds for added and removed lines, with normal-colored text on top instead of green on green. Hide the four header lines, since the file is already selected on the left.
4. **Padding and separation.** A few pixels of inner padding in each pane. A slightly different shade for the sidebar so the split shows without a heavy bar. A thin splitter that highlights on hover.
5. **Small fixes.**
   - Readable hash, author and date on the selected history row.
   - A real arrow on the dropdowns instead of `v`.
   - Changes/History styled as tabs (underline or flat background), not buttons.
   - A shorter commit details metadata line: drop the full email and full hash, or move them to a tooltip.
6. **Maybe later.** Line numbers in a diff gutter. Useful, but adds more to the screen.

Suggested start: 1 and 3 together. They are mostly color constants and would change the feel the most. Both need a dark and a light version now.
