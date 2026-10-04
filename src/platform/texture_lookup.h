// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace rw {
struct TexDictionary;
} // namespace rw

namespace coney::platform {

/// Makes librw find a material's texture by name the way the game's RenderWare does: with no current dictionary,
/// through every registered dictionary, newest first, the first texture of that name winning; with a current
/// dictionary, only in that one. A name found nowhere gives no texture: nothing is read from a file and no stand-in
/// is made (the game's texture-read callback returns none). Without it librw searches only the current dictionary and
/// otherwise tries to load an image file of that name.
///
/// That the newest dictionary is searched first is inferred from RenderWare (graphics.md, open question). Needs a
/// running RenderEngine; calling it again while the same engine runs does nothing harmful.
///
/// Research: docs/research/graphics.md#texture-lookup
void installGlobalTextureLookup();

/// A texture dictionary's place in the global lookup, for as long as this object lives: it is registered on
/// construction (as the newest) and taken out on destruction. Move-only.
class TextureLookupEntry {
  public:
    /// Registers `dictionary`, which must outlive this entry.
    explicit TextureLookupEntry(rw::TexDictionary* dictionary);
    TextureLookupEntry(TextureLookupEntry&& other) noexcept;
    TextureLookupEntry& operator=(TextureLookupEntry&& other) noexcept;
    TextureLookupEntry(const TextureLookupEntry&) = delete;
    TextureLookupEntry& operator=(const TextureLookupEntry&) = delete;
    ~TextureLookupEntry();

  private:
    // Takes the dictionary out of the lookup, if this entry still holds one.
    void release() noexcept;

    rw::TexDictionary* m_dictionary = nullptr; // not owned; null after a move
};

} // namespace coney::platform
