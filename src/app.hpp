#pragma once
#include "model.hpp"

namespace App {
    void Init(AppState& state);
    void Tick(AppState& state);
    void Shutdown(AppState& state);

    // Re-reads branches and current branch from the active repo.
    void RefreshRepoState(AppState& state);
}
