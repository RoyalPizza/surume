#pragma once
#include "Model.h"

namespace Persistence {
    void Load(AppState& state);
    void Save(const AppState& state);
}
