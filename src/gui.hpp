#pragma once

#include "model.hpp"

// Pick the Git status and diff colors for a dark or light ImGui style.
void gui_set_dark_mode(bool is_dark_mode);

// Draw the whole interface for one frame: menu bar, header, left column, diff view, modals.
// Must be called between ImGui::NewFrame and ImGui::Render. Requests that need the window
// or block (dialogs, opening repositories, loading diffs) are left in state for main.cpp.
void gui_render(AppState& state);
