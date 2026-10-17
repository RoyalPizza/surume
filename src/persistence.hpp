#pragma once
#include "model.hpp"

namespace Persistence {
    void Load(AppState& state);
    void Save(const AppState& state);
}
