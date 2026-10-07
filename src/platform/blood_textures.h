// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <expected>
#include <vector>

#include "core/error.h"
#include "fileio/wad.h"
#include "graphics/human_blood.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Texture;
} // namespace rw

namespace coney::platform {

/// The three shared blood textures of the humans' second pass, `charblood_d1` to `charblood_d3`, loaded with the
/// dictionaries of `global.pak` (the resource manager's start-up pack, docs/research/boot.md) and converted for
/// drawing. Owns the dictionaries: it must outlive every CharacterMesh given one of its textures, and be destroyed
/// before the RenderEngine stops. Move-only.
///
/// **Coney choice**: the original keeps the three in resource-manager slots `+0x74`-`+0x7c`; which of the pack's
/// resources fill them is not on the pages, so they are found by the texture names seen at runtime.
///
/// Research: docs/research/graphics.md#human-draw
class BloodTextures {
  public:
    /// Loads them from `wad`. Fails with ErrorCode::NotFound when the pack or one of the textures is missing, and as
    /// the dictionaries' load and conversion do. With the NULL backend nothing is converted and the textures are still
    /// found by name.
    [[nodiscard]] static std::expected<BloodTextures, Error> load(const RenderEngine& engine, const io::Wad& wad);

    /// The texture for `which`; never null once loaded.
    [[nodiscard]] rw::Texture* texture(graphics::BloodTexture which) const {
        return m_textures.at(static_cast<std::size_t>(which));
    }

  private:
    BloodTextures() = default;

    std::vector<TextureDictionary> m_dictionaries; // own the textures
    std::array<rw::Texture*, 3> m_textures{};      // by graphics::BloodTexture
};

} // namespace coney::platform
