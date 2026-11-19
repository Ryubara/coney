// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_lookup.h"

namespace coney::cpak {

// Stubs: the lookup order comes in the next commit.

std::string_view kindName(Kind /*kind*/) { return "unknown"; }

std::optional<Kind> parseKind(std::string_view /*name*/) { return std::nullopt; }

bool isSimulation(Kind /*kind*/) { return false; }

std::expected<Lookup, Error> Lookup::build(std::vector<CatalogueRow> /*catalogue*/, std::vector<Mod> /*mods*/,
                                           std::vector<Pack> /*packs*/, Mode /*mode*/) {
    return Lookup();
}

std::expected<Location, Error> Lookup::resolve(std::string_view name, Kind /*kind*/) const {
    return fail(ErrorCode::NotFound, std::string(name));
}

std::optional<std::string> Lookup::textureFromPack(const Location& /*dictionary*/,
                                                   std::string_view /*textureKey*/) const {
    return std::nullopt;
}

} // namespace coney::cpak
