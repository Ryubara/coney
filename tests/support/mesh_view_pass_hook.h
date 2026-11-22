// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A mesh-view hook for the tests of the passes a renderer that records its draws asks for (docs/engine/gpu-renderer.md
// #scene): it gives every recorded model the passes it is told to, and notes what it was asked, as the debug tools'
// MeshViews does for the Debug draw page. It sits here, not in the debug tools, so the tests that build in every
// configuration can use it.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "platform/mesh_view_hook.h"

namespace coney::test {

/// What the hook was asked for one recorded model: its kind, mesh, and each material's texture name and alpha.
struct PassQuestion {
    coney::platform::DrawKind kind = coney::platform::DrawKind::Other;
    coney::graphics::MeshId mesh;
    std::vector<std::string> textures;
    std::vector<std::uint8_t> alphas;
};

/// Gives every model `passes`, hides `hidden` kinds and notes each question passesFor() is asked.
class PassHook final : public coney::platform::MeshViewHook {
  public:
    explicit PassHook(coney::platform::ViewPasses passes = {},
                      std::initializer_list<coney::platform::DrawKind> hidden = {})
        : passes(std::move(passes)), m_hidden(hidden) {}

    bool beginFrame(const coney::platform::PlayLevelMode& /*mode*/) override { return true; }
    [[nodiscard]] bool hides(coney::platform::DrawKind kind) const override {
        return std::ranges::find(m_hidden, kind) != m_hidden.end();
    }
    void draw(rw::Atomic* /*atomic*/, coney::platform::DrawKind /*kind*/, bool /*layer*/,
              const std::function<void()>& shaded) override {
        shaded();
    }
    [[nodiscard]] coney::platform::ViewPasses
    passesFor(coney::platform::DrawKind kind, coney::graphics::MeshId mesh,
              std::span<const coney::platform::ViewMaterial> materials) override {
        PassQuestion question{.kind = kind, .mesh = mesh, .textures = {}, .alphas = {}};
        for (const coney::platform::ViewMaterial& material : materials) {
            question.textures.emplace_back(material.texture);
            question.alphas.push_back(material.alpha);
        }
        asked.push_back(std::move(question));
        return passes;
    }

    coney::platform::ViewPasses passes; ///< What every model is given.
    std::vector<PassQuestion> asked;    ///< Every question, in order.

  private:
    std::vector<coney::platform::DrawKind> m_hidden;
};

} // namespace coney::test
