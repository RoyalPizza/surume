#pragma once

#include "model.hpp"

// Draw the whole interface for one frame: menu bar, header, left column, diff view, modals.
// Must be called between ImGui::NewFrame and ImGui::Render. Requests that need the window
// or block (dialogs, opening repositories, loading diffs) are left in state for main.cpp.
void gui_render(AppState& state);
