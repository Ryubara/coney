// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A mesh-view hook for the tests that read the frame description (docs/engine/gpu-renderer.md#scene): it
// hides the kinds it is given, as the debug tools' MeshViews does for the World page's parts, and draws every other
// model as the game does. It sits here, not in the debug tools, so the draw-stream tests, which build in every
// configuration, can use it.

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <vector>

#include "platform/mesh_view_hook.h"
#include "platform/perf_probes.h"

namespace coney::test {

/// Hides `hidden` kinds; `passes` is what hasPasses() answers (a wireframe or a mesh view is on).
class HideHook final : public coney::platform::MeshViewHook {
  public:
    explicit HideHook(std::initializer_list<coney::platform::DrawKind> hidden = {}, bool passes = false)
        : m_hidden(hidden), m_passes(passes) {}

    bool beginFrame(const coney::platform::PlayLevelMode& /*mode*/) override { return true; }
    [[nodiscard]] bool hasPasses() const override { return m_passes; }
    [[nodiscard]] bool hides(coney::platform::DrawKind kind) const override {
        return std::ranges::find(m_hidden, kind) != m_hidden.end();
    }
    void draw(rw::Atomic* /*atomic*/, coney::platform::DrawKind kind, bool /*layer*/,
              const std::function<void()>& shaded) override {
        if (!hides(kind)) {
            shaded();
        }
    }

  private:
    std::vector<coney::platform::DrawKind> m_hidden;
    bool m_passes;
};

} // namespace coney::test
