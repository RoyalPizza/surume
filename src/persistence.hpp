#pragma once

#include "model.hpp"

#include <string>

// Load settings.toml and repositories.toml from dir (the app-data folder) into state.
// Missing or invalid files leave the defaults in place; that is the normal first run.
void persistence_load(AppState& state, const std::string& dir);

// Write settings.toml and repositories.toml to dir. Returns false if either file
// couldn't be written.
bool persistence_save(const AppState& state, const std::string& dir);
