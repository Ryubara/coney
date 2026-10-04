// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/render_engine.h"

#include <string>
#include <utility>

#include <rw.h>

namespace coney::platform {

namespace {

std::unexpected<Error> librwFailure(const char* step) {
    return std::unexpected(Error{ErrorCode::PlatformFailure, std::string("librw ") + step + " failed"});
}

} // namespace

std::expected<RenderEngine, Error> RenderEngine::start() {
    // The default memory functions (malloc and free) are fine until Coney has an allocator of its own.
    if (!rw::Engine::init()) {
        return librwFailure("Engine::init");
    }
    // On the NULL platform librw declares EngineOpenParams but never defines it (only the D3D9, GL3 and PS2 builds
    // do), and the NULL device ignores the pointer it is given, so there is nothing to fill in: pass null. When the
    // renderer switches to GL3, this becomes the real parameters (the SDL window and the fullscreen flag).
    if (!rw::Engine::open(nullptr)) {
        rw::Engine::term();
        return librwFailure("Engine::open");
    }
    if (!rw::Engine::start()) {
        rw::Engine::close();
        rw::Engine::term();
        return librwFailure("Engine::start");
    }
    RenderEngine engine;
    engine.m_running = true;
    return engine;
}

RenderEngine::RenderEngine(RenderEngine&& other) noexcept : m_running(std::exchange(other.m_running, false)) {}

RenderEngine& RenderEngine::operator=(RenderEngine&& other) noexcept {
    if (this != &other) {
        shutDown();
        m_running = std::exchange(other.m_running, false);
    }
    return *this;
}

RenderEngine::~RenderEngine() { shutDown(); }

void RenderEngine::shutDown() noexcept {
    if (!m_running) {
        return;
    }
    m_running = false;
    // The reverse of start(): librw refuses each step unless the one before it has run.
    rw::Engine::stop();
    rw::Engine::close();
    rw::Engine::term();
}

} // namespace coney::platform
