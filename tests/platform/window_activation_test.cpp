// SPDX-License-Identifier: GPL-3.0-or-later
// Windows opened by runs nobody is playing must not take the keyboard focus (docs/guides/research-workflow.md): the SDL
// hints RenderEngine::start sets before librw shows its window. SDL reads hints without a display, so this runs on CI.
#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include "platform/render_engine.h"

TEST_CASE("a window that must not activate is shown and raised without taking the focus", "[window]") {
    coney::platform::setWindowActivation(false);
    CHECK_FALSE(SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true));
    CHECK_FALSE(SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, true));

    // A later run in the same process that a person plays gets SDL's usual behaviour back.
    coney::platform::setWindowActivation(true);
    CHECK(SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, false));
    CHECK(SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, false));
}
