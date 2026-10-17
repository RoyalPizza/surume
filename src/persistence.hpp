#pragma once
#include "model.hpp"
#include <string>

namespace Persistence {
    // dir is the app-data folder holding settings.toml and repositories.toml.
    void Load(AppState& state, const std::string& dir);
    void Save(const AppState& state, const std::string& dir);
}
