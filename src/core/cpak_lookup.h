// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// The lookup order of an install laid out in Coney Packs: which layer (an enabled mod, a texture pack or the game's own
// archives) serves an asset, by its `game:/` name and kind. The rules are docs/guides/cpak-format.md#lookup; this is
// their one implementation, built from records the caller has already read (the catalogue, the enabled mods' entries,
// the texture packs' keys), so it opens no file. The install layout has no counterpart in the original.
namespace coney::cpak {

/// What an entry is, as the catalogue's `kind` field names it (docs/guides/cpak-format.md#catalogue).
enum class Kind : std::uint8_t {
    Script,
    Text,
    Data,
    World,
    Collision,
    Objects,
    Model,
    Animation,
    Character,
    Scene,
    Texture,
    Sound,
    Music,
    Bank,
    Movie,
};

/// The name the catalogue gives `kind` ("script", "texture", ...).
[[nodiscard]] std::string_view kindName(Kind kind);

/// The kind the catalogue calls `name`, or nullopt for a name it does not know.
[[nodiscard]] std::optional<Kind> parseKind(std::string_view name);

/// Whether `kind` is a simulation kind (it changes what happens in the game: scripts, worlds, collision, animation
/// timing) rather than a presentation kind (what is seen or heard). The split is the page's kinds table
/// (docs/guides/cpak-format.md#kinds), after docs/guides/asset-and-script-architecture.md#asset-layers.
[[nodiscard]] bool isSimulation(Kind kind);

/// The disc an entry's bytes came from.
enum class Source : std::uint8_t {
    Ps2,  ///< The PS2 disc: always installed, the only gameplay source and the test mode's only source.
    Xbox, ///< The Xbox disc: presentation only, under `game/xbox/`.
};

/// One row of the catalogue, the fields the lookup needs.
struct CatalogueRow {
    std::string name; ///< The entry's name, checked by checkName().
    Kind kind = Kind::Data;
    Source source = Source::Ps2;
    std::string archive; ///< The archive's path from `game/` (`textures/hud.cpak`), or a movie's file.
};

/// One entry an enabled mod replaces or adds.
struct ModEntry {
    std::string name; ///< The `game:/` name it replaces or adds, checked by checkName().
    Kind kind = Kind::Data;
    std::string archive; ///< Where the mod holds it, from the mod's own root.
};

/// An enabled mod: the entries its patch archives hold.
struct Mod {
    std::string id; ///< Its name, unique among the enabled mods.
    std::vector<ModEntry> entries;
};

/// A texture pack: the original textures it replaces, by the key texture packs use
/// (docs/guides/enhancements.md#packs), opaque here.
struct Pack {
    std::string id; ///< Its name, unique among the packs.
    std::vector<std::string> textureKeys;
};

/// Whether the lookup serves a play session or test mode.
enum class Mode : std::uint8_t {
    Play, ///< Mods, packs and the Xbox entries, by the order on the page.
    Test, ///< The PS2 entries only (`--headless`, `--input-script`, the disc tests): no mod, pack or Xbox entry.
};

/// Which layer an asset comes from.
enum class Layer : std::uint8_t {
    Mod,  ///< An enabled mod's patch archive.
    Game, ///< The game's own archives under `game/`.
};

/// Where resolve() found an asset.
struct Location {
    Layer layer = Layer::Game;
    std::string mod;             ///< For Layer::Mod, the mod's id; empty otherwise.
    Source source = Source::Ps2; ///< For Layer::Game, the disc of the row picked.
    std::string archive;         ///< The archive (or movie file), from the mod's root or from `game/`.
};

/// The lookup order over one install's catalogue, its enabled mods and its texture packs.
class Lookup {
  public:
    /// Builds the lookup. `mods` are the enabled mods in their load order, `packs` the texture packs in theirs; in
    /// Mode::Test both are ignored. Fails with ErrorCode::Invalid for an ill-formed name (checkName()), a name with two
    /// rows of the same source, an Xbox row of a simulation kind, an empty or repeated mod or pack id, a mod naming an
    /// entry twice, or a mod entry whose kind is not the kind of the catalogue's entry of that name.
    [[nodiscard]] static std::expected<Lookup, Error> build(std::vector<CatalogueRow> catalogue, std::vector<Mod> mods,
                                                            std::vector<Pack> packs, Mode mode);

    /// Where the asset called `name` of `kind` is read from: the last enabled mod in the load order that holds it,
    /// else the game's Xbox row for a presentation kind when there is one, else its PS2 row. Fails with
    /// ErrorCode::NotFound when no layer holds it, and ErrorCode::Invalid when the entry of that name is of another
    /// kind.
    [[nodiscard]] std::expected<Location, Error> resolve(std::string_view name, Kind kind) const;

    /// The texture pack that replaces the texture `textureKey` of a dictionary resolve() found at `dictionary`: the
    /// last pack in the list that holds the key, or nullopt. A dictionary a mod replaced takes nothing from a pack,
    /// since mods come before packs; nor does any dictionary in Mode::Test.
    [[nodiscard]] std::optional<std::string> textureFromPack(const Location& dictionary,
                                                             std::string_view textureKey) const;

    /// Whether an enabled mod replaces or adds an entry of a simulation kind, which marks a co-op session as modded.
    [[nodiscard]] bool simulationModded() const { return m_simulationModded; }

  private:
    Lookup() = default;

    std::vector<CatalogueRow> m_catalogue; ///< Sorted by name, then source.
    std::vector<Mod> m_mods;               ///< In load order; each mod's entries sorted by name.
    std::vector<Pack> m_packs;             ///< In list order; each pack's keys sorted.
    Mode m_mode = Mode::Play;
    bool m_simulationModded = false;
};

} // namespace coney::cpak
