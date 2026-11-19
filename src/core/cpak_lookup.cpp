// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_lookup.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>

#include "core/cpak_index.h"

namespace coney::cpak {

namespace {

// Every kind with its catalogue name, in the enum's order.
constexpr std::array<std::pair<Kind, std::string_view>, 15> kKindNames{{
    {Kind::Script, "script"},
    {Kind::Text, "text"},
    {Kind::Data, "data"},
    {Kind::World, "world"},
    {Kind::Collision, "collision"},
    {Kind::Objects, "objects"},
    {Kind::Model, "model"},
    {Kind::Animation, "animation"},
    {Kind::Character, "character"},
    {Kind::Scene, "scene"},
    {Kind::Texture, "texture"},
    {Kind::Sound, "sound"},
    {Kind::Music, "music"},
    {Kind::Bank, "bank"},
    {Kind::Movie, "movie"},
}};

// The error for a name checkName() refused, saying where the name was met.
std::unexpected<Error> badName(std::string_view where, const Error& error) {
    return fail(ErrorCode::Invalid, std::format("cpak lookup: {}: {}", where, error.message));
}

// The catalogue's rows of `name`, sorted by name then source: at most one PS2 and one Xbox row.
std::span<const CatalogueRow> rowsOf(const std::vector<CatalogueRow>& catalogue, std::string_view name) {
    const auto [first, last] = std::ranges::equal_range(catalogue, name, std::ranges::less{}, &CatalogueRow::name);
    return {first, last};
}

// The entry of `mod` called `name`, or nullptr; the entries are sorted by name.
const ModEntry* entryOf(const Mod& mod, std::string_view name) {
    const auto found = std::ranges::lower_bound(mod.entries, name, std::ranges::less{}, &ModEntry::name);
    return found != mod.entries.end() && found->name == name ? &*found : nullptr;
}

// Checks that `ids` (the mods' or packs' ids, in their order) are none empty and none repeated.
std::expected<void, Error> checkIds(std::string_view what, std::vector<std::string_view> ids) {
    if (std::ranges::find(ids, std::string_view{}) != ids.end()) {
        return fail(ErrorCode::Invalid, std::format("cpak lookup: a {} has no id", what));
    }
    std::ranges::sort(ids);
    if (const auto repeated = std::ranges::adjacent_find(ids); repeated != ids.end()) {
        return fail(ErrorCode::Invalid, std::format("cpak lookup: two {}s are called {}", what, *repeated));
    }
    return {};
}

// Checks and sorts the catalogue: names well formed, one row per name and source, Xbox rows of presentation kinds.
std::expected<void, Error> prepareCatalogue(std::vector<CatalogueRow>& catalogue) {
    for (const CatalogueRow& row : catalogue) {
        if (auto checked = checkName(row.name); !checked) {
            return badName("catalogue", checked.error());
        }
        // The Xbox disc only adds presentation (docs/guides/cpak-format.md#xbox).
        if (row.source == Source::Xbox && isSimulation(row.kind)) {
            return fail(ErrorCode::Invalid, std::format("cpak lookup: {} is a {} from the Xbox disc, which adds only "
                                                        "presentation",
                                                        row.name, kindName(row.kind)));
        }
    }
    std::ranges::sort(catalogue, [](const CatalogueRow& a, const CatalogueRow& b) {
        return std::tie(a.name, a.source) < std::tie(b.name, b.source);
    });
    const auto repeated = std::ranges::adjacent_find(catalogue, [](const CatalogueRow& a, const CatalogueRow& b) {
        return a.name == b.name && a.source == b.source;
    });
    if (repeated != catalogue.end()) {
        return fail(ErrorCode::Invalid,
                    std::format("cpak lookup: the catalogue lists {} twice from one disc", repeated->name));
    }
    // Both discs' rows of a name are the same asset, so of one kind.
    const auto mixed = std::ranges::adjacent_find(
        catalogue, [](const CatalogueRow& a, const CatalogueRow& b) { return a.name == b.name && a.kind != b.kind; });
    if (mixed != catalogue.end()) {
        return fail(ErrorCode::Invalid, std::format("cpak lookup: the catalogue gives {} two kinds", mixed->name));
    }
    return {};
}

// Checks and sorts one mod's entries against the catalogue: names well formed and unique, kinds as the game's.
std::expected<void, Error> prepareMod(Mod& mod, const std::vector<CatalogueRow>& catalogue) {
    for (const ModEntry& entry : mod.entries) {
        if (auto checked = checkName(entry.name); !checked) {
            return badName(std::format("mod {}", mod.id), checked.error());
        }
        // A replacement is of the kind it replaces, so the lookup by kind finds the same entry in every layer.
        for (const CatalogueRow& row : rowsOf(catalogue, entry.name)) {
            if (row.kind != entry.kind) {
                return fail(ErrorCode::Invalid,
                            std::format("cpak lookup: mod {} gives {} as a {}, the game's is a {}", mod.id, entry.name,
                                        kindName(entry.kind), kindName(row.kind)));
            }
        }
    }
    std::ranges::sort(mod.entries, std::ranges::less{}, &ModEntry::name);
    const auto repeated = std::ranges::adjacent_find(mod.entries, std::ranges::equal_to{}, &ModEntry::name);
    if (repeated != mod.entries.end()) {
        return fail(ErrorCode::Invalid, std::format("cpak lookup: mod {} holds {} twice", mod.id, repeated->name));
    }
    return {};
}

} // namespace

std::string_view kindName(Kind kind) {
    const auto index = static_cast<std::size_t>(kind);
    return index < kKindNames.size() ? kKindNames[index].second : "unknown";
}

std::optional<Kind> parseKind(std::string_view name) {
    const auto found = std::ranges::find(kKindNames, name, &std::pair<Kind, std::string_view>::second);
    if (found == kKindNames.end()) {
        return std::nullopt;
    }
    return found->first;
}

bool isSimulation(Kind kind) {
    switch (kind) {
    case Kind::Script:
    case Kind::Data:
    case Kind::World:
    case Kind::Collision:
    case Kind::Objects:
    case Kind::Animation:
    case Kind::Character:
    case Kind::Scene:
        return true;
    case Kind::Text:
    case Kind::Model:
    case Kind::Texture:
    case Kind::Sound:
    case Kind::Music:
    case Kind::Bank:
    case Kind::Movie:
        return false;
    }
    return true; // An out-of-range value is treated as the stricter kind.
}

std::expected<Lookup, Error> Lookup::build(std::vector<CatalogueRow> catalogue, std::vector<Mod> mods,
                                           std::vector<Pack> packs, Mode mode) {
    Lookup lookup;
    lookup.m_mode = mode;
    if (auto prepared = prepareCatalogue(catalogue); !prepared) {
        return std::unexpected(prepared.error());
    }
    // Test mode reads the PS2 rows alone, so the mods and packs are not even kept (nor checked).
    if (mode == Mode::Play) {
        std::vector<std::string_view> modIds;
        std::ranges::transform(mods, std::back_inserter(modIds),
                               [](const Mod& mod) { return std::string_view(mod.id); });
        if (auto checked = checkIds("mod", std::move(modIds)); !checked) {
            return std::unexpected(checked.error());
        }
        std::vector<std::string_view> packIds;
        std::ranges::transform(packs, std::back_inserter(packIds),
                               [](const Pack& pack) { return std::string_view(pack.id); });
        if (auto checked = checkIds("pack", std::move(packIds)); !checked) {
            return std::unexpected(checked.error());
        }
        for (Mod& mod : mods) {
            if (auto prepared = prepareMod(mod, catalogue); !prepared) {
                return std::unexpected(prepared.error());
            }
            lookup.m_simulationModded =
                lookup.m_simulationModded || std::ranges::any_of(mod.entries, isSimulation, &ModEntry::kind);
        }
        for (Pack& pack : packs) {
            std::ranges::sort(pack.textureKeys);
        }
        lookup.m_mods = std::move(mods);
        lookup.m_packs = std::move(packs);
    }
    lookup.m_catalogue = std::move(catalogue);
    return lookup;
}

std::expected<Location, Error> Lookup::resolve(std::string_view name, Kind kind) const {
    // 1. Enabled mods: the later in the load order wins (docs/guides/studio/mods.md#load-order).
    for (const Mod& mod : std::views::reverse(m_mods)) {
        if (const ModEntry* entry = entryOf(mod, name)) {
            if (entry->kind != kind) {
                return fail(ErrorCode::Invalid, std::format("cpak lookup: {} is a {}, not a {}", name,
                                                            kindName(entry->kind), kindName(kind)));
            }
            return Location{.layer = Layer::Mod, .mod = mod.id, .source = Source::Ps2, .archive = entry->archive};
        }
    }
    // 2. Packs replace single textures inside a dictionary: textureFromPack(). 3. The game's archives: the Xbox row
    // of a presentation kind when that disc is installed and this is no test, else the PS2 row.
    const CatalogueRow* picked = nullptr;
    for (const CatalogueRow& row : rowsOf(m_catalogue, name)) {
        if (row.kind != kind) {
            return fail(ErrorCode::Invalid,
                        std::format("cpak lookup: {} is a {}, not a {}", name, kindName(row.kind), kindName(kind)));
        }
        const bool usable = row.source == Source::Ps2 || m_mode == Mode::Play;
        if (usable && (picked == nullptr || row.source == Source::Xbox)) {
            picked = &row;
        }
    }
    if (picked == nullptr) {
        return fail(ErrorCode::NotFound, std::format("cpak lookup: no layer holds {}", name));
    }
    return Location{.layer = Layer::Game, .mod = {}, .source = picked->source, .archive = picked->archive};
}

std::optional<std::string> Lookup::textureFromPack(const Location& dictionary, std::string_view textureKey) const {
    // Mods come before packs, so a dictionary a mod replaced is the mod's whole; test mode keeps no packs.
    if (dictionary.layer != Layer::Game) {
        return std::nullopt;
    }
    for (const Pack& pack : std::views::reverse(m_packs)) {
        if (std::ranges::binary_search(pack.textureKeys, textureKey)) {
            return pack.id;
        }
    }
    return std::nullopt;
}

} // namespace coney::cpak
