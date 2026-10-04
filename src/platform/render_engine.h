// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>

#include "core/error.h"

namespace coney::platform {

/// librw's engine, started and stopped in the order librw requires (init, open, start; then stop, close, term).
/// librw runs on its NULL platform for now: no drawing yet, but the engine, plugins and file loaders are live.
///
/// librw keeps its engine in global state, so only one RenderEngine can be running at a time; a second start()
/// while one runs fails at librw's init step.
class RenderEngine {
  public:
    /// Brings librw up. On failure, returns ErrorCode::PlatformFailure naming the librw step that refused, after
    /// undoing the steps that had already succeeded, so librw is back where it started.
    [[nodiscard]] static std::expected<RenderEngine, Error> start();

    RenderEngine(RenderEngine&& other) noexcept;
    RenderEngine& operator=(RenderEngine&& other) noexcept;
    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;
    ~RenderEngine();

  private:
    RenderEngine() = default;

    /// Runs librw's stop, close and term, if this object still owns the running engine.
    void shutDown() noexcept;

    bool m_running = false; // false after a move, so only one object shuts librw down
};

} // namespace coney::platform
