// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_lookup.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/cpak_fixtures.h"

using coney::ErrorCode;
using coney::cpak::CatalogueRow;
using coney::cpak::isSimulation;
using coney::cpak::Kind;
using coney::cpak::kindName;
using coney::cpak::Layer;
using coney::cpak::Location;
using coney::cpak::Lookup;
using coney::cpak::Mod;
using coney::cpak::Mode;
using coney::cpak::Pack;
using coney::cpak::parseKind;
using coney::cpak::Source;
using coney::test::cpak::failure;

namespace {

// A small synthetic catalogue: a script and a HUD texture dictionary from the PS2 disc, the same dictionary from the
// Xbox disc, and a movie only the Xbox disc has. Names and archives are made up; no game data.
std::vector<CatalogueRow> sampleCatalogue() {
    return {
        {.name = "scripts/main", .kind = Kind::Script, .source = Source::Ps2, .archive = "common/scripts.cpak"},
        {.name = "textures/hud/main", .kind = Kind::Texture, .source = Source::Ps2, .archive = "textures/hud.cpak"},
        {.name = "textures/hud/main",
         .kind = Kind::Texture,
         .source = Source::Xbox,
         .archive = "xbox/textures/hud.cpak"},
        {.name = "movies/intro_hd", .kind = Kind::Movie, .source = Source::Xbox, .archive = "xbox/movies/intro_hd.bik"},
    };
}

// A mod that replaces the HUD dictionary.
Mod hudMod(std::string id, std::string archive) {
    return {.id = std::move(id),
            .entries = {{.name = "textures/hud/main", .kind = Kind::Texture, .archive = std::move(archive)}}};
}

// The lookup of `catalogue`, `mods` and `packs`, which the test expects to build.
Lookup built(std::vector<CatalogueRow> catalogue, std::vector<Mod> mods = {}, std::vector<Pack> packs = {},
             Mode mode = Mode::Play) {
    auto lookup = Lookup::build(std::move(catalogue), std::move(mods), std::move(packs), mode);
    REQUIRE(lookup.has_value());
    return std::move(*lookup);
}

} // namespace

TEST_CASE("every kind has the catalogue's name and reads back from it", "[cpak]") {
    // Expected: ../guides/cpak-format.md#catalogue (the `kind` field's values), Coney's own format
    const std::array<std::pair<Kind, std::string_view>, 15> names{{
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
    for (const auto& [kind, name] : names) {
        CHECK(kindName(kind) == name);
        CHECK(parseKind(name) == kind);
    }
    CHECK_FALSE(parseKind("Texture").has_value());
    CHECK_FALSE(parseKind("").has_value());
}

TEST_CASE("the simulation and presentation kinds are split as the page's table gives them", "[cpak]") {
    // Expected: ../guides/cpak-format.md#kinds, after ../guides/asset-and-script-architecture.md#asset-layers
    for (const Kind kind : {Kind::Script, Kind::Data, Kind::World, Kind::Collision, Kind::Objects, Kind::Animation,
                            Kind::Character, Kind::Scene}) {
        CHECK(isSimulation(kind));
    }
    for (const Kind kind :
         {Kind::Text, Kind::Model, Kind::Texture, Kind::Sound, Kind::Music, Kind::Bank, Kind::Movie}) {
        CHECK_FALSE(isSimulation(kind));
    }
}

TEST_CASE("the game's archives give the Xbox row of a presentation kind and the PS2 row otherwise", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup, item 3, and #xbox (Xbox entries are presentation only)
    const Lookup lookup = built(sampleCatalogue());
    const auto hud = lookup.resolve("textures/hud/main", Kind::Texture);
    REQUIRE(hud.has_value());
    CHECK(hud->layer == Layer::Game);
    CHECK(hud->source == Source::Xbox);
    CHECK(hud->archive == "xbox/textures/hud.cpak");
    const auto script = lookup.resolve("scripts/main", Kind::Script);
    REQUIRE(script.has_value());
    CHECK(script->layer == Layer::Game);
    CHECK(script->source == Source::Ps2);
    CHECK(script->archive == "common/scripts.cpak");

    // Without the Xbox disc the PS2 row serves the presentation kind too.
    auto ps2Only = sampleCatalogue();
    std::erase_if(ps2Only, [](const CatalogueRow& row) { return row.source == Source::Xbox; });
    const auto plain = built(std::move(ps2Only)).resolve("textures/hud/main", Kind::Texture);
    REQUIRE(plain.has_value());
    CHECK(plain->source == Source::Ps2);
    CHECK(plain->archive == "textures/hud.cpak");
}

TEST_CASE("an enabled mod comes before the game, and the later mod in the load order wins", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup, item 1; ../guides/studio/mods.md#load-order (the later mod wins)
    const Lookup one = built(sampleCatalogue(), {hudMod("first", "textures/hud.cpak")});
    const auto fromMod = one.resolve("textures/hud/main", Kind::Texture);
    REQUIRE(fromMod.has_value());
    CHECK(fromMod->layer == Layer::Mod);
    CHECK(fromMod->mod == "first");
    CHECK(fromMod->archive == "textures/hud.cpak");

    const Lookup two =
        built(sampleCatalogue(), {hudMod("first", "textures/hud.cpak"), hudMod("second", "patch/hud.cpak")});
    const auto later = two.resolve("textures/hud/main", Kind::Texture);
    REQUIRE(later.has_value());
    CHECK(later->mod == "second");
    CHECK(later->archive == "patch/hud.cpak");

    // An entry a mod adds, which the game does not have, is found in the mod.
    Mod adds{.id = "adds", .entries = {{.name = "textures/new/sign", .kind = Kind::Texture, .archive = "new.cpak"}}};
    const auto added = built(sampleCatalogue(), {adds}).resolve("textures/new/sign", Kind::Texture);
    REQUIRE(added.has_value());
    CHECK(added->mod == "adds");
}

TEST_CASE("without the mod enabled the game's entry is read again", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup; #798's decision that disabling a mod restores the original
    const Lookup enabled = built(sampleCatalogue(), {hudMod("hud", "textures/hud.cpak")});
    REQUIRE(enabled.resolve("textures/hud/main", Kind::Texture).has_value());
    const Lookup disabled = built(sampleCatalogue());
    const auto game = disabled.resolve("textures/hud/main", Kind::Texture);
    REQUIRE(game.has_value());
    CHECK(game->layer == Layer::Game);
    CHECK(game->mod.empty());
}

TEST_CASE("test mode reads only the PS2 rows: no mod, pack or Xbox entry", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup (the test mode paragraph)
    const Lookup lookup = built(sampleCatalogue(), {hudMod("hud", "textures/hud.cpak")},
                                {{.id = "hd", .textureKeys = {"0001"}}}, Mode::Test);
    const auto hud = lookup.resolve("textures/hud/main", Kind::Texture);
    REQUIRE(hud.has_value());
    CHECK(hud->layer == Layer::Game);
    CHECK(hud->source == Source::Ps2);
    CHECK(hud->archive == "textures/hud.cpak");
    CHECK_FALSE(lookup.textureFromPack(*hud, "0001").has_value());
    CHECK(failure(lookup.resolve("movies/intro_hd", Kind::Movie)) == ErrorCode::NotFound);
    CHECK_FALSE(lookup.simulationModded());
}

TEST_CASE("a texture pack replaces a texture of a game dictionary but not of one a mod replaced", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup, item 2 (mods, then packs, then the game)
    const std::vector<Pack> packs{{.id = "early", .textureKeys = {"0001", "0002"}},
                                  {.id = "late", .textureKeys = {"0002"}}};
    const Lookup plain = built(sampleCatalogue(), {}, packs);
    const auto game = plain.resolve("textures/hud/main", Kind::Texture);
    REQUIRE(game.has_value());
    CHECK(plain.textureFromPack(*game, "0001") == std::optional<std::string>("early"));
    CHECK(plain.textureFromPack(*game, "0002") == std::optional<std::string>("late"));
    CHECK_FALSE(plain.textureFromPack(*game, "0003").has_value());

    const Lookup modded = built(sampleCatalogue(), {hudMod("hud", "textures/hud.cpak")}, packs);
    const auto fromMod = modded.resolve("textures/hud/main", Kind::Texture);
    REQUIRE(fromMod.has_value());
    CHECK_FALSE(modded.textureFromPack(*fromMod, "0001").has_value());
}

TEST_CASE("a mod of a simulation kind marks the session modded and one of presentation kinds does not", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup, item 1 (a simulation mod marks a co-op session as modded)
    CHECK_FALSE(built(sampleCatalogue(), {hudMod("hud", "textures/hud.cpak")}).simulationModded());
    Mod script{.id = "script",
               .entries = {{.name = "scripts/main", .kind = Kind::Script, .archive = "common/scripts.cpak"}}};
    CHECK(built(sampleCatalogue(), {hudMod("hud", "textures/hud.cpak"), script}).simulationModded());
}

TEST_CASE("a name no layer holds is NotFound and a name asked for as another kind is Invalid", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup, Coney's own format
    const Lookup lookup = built(sampleCatalogue(), {hudMod("hud", "textures/hud.cpak")});
    CHECK(failure(lookup.resolve("scripts/missing", Kind::Script)) == ErrorCode::NotFound);
    CHECK(failure(lookup.resolve("scripts/main", Kind::Texture)) == ErrorCode::Invalid);
    CHECK(failure(lookup.resolve("textures/hud/main", Kind::Model)) == ErrorCode::Invalid);
}

TEST_CASE("a catalogue, mod list or pack list that breaks the rules is refused", "[cpak]") {
    // Expected: ../guides/cpak-format.md#lookup and #names, Coney's own format
    const auto refused = [](std::vector<CatalogueRow> catalogue, std::vector<Mod> mods = {},
                            std::vector<Pack> packs = {}) {
        return failure(Lookup::build(std::move(catalogue), std::move(mods), std::move(packs), Mode::Play));
    };
    CHECK_FALSE(refused(sampleCatalogue()).has_value());

    auto badName = sampleCatalogue();
    badName[0].name = "scripts/../main";
    CHECK(refused(badName) == ErrorCode::Invalid);

    auto twice = sampleCatalogue();
    twice.push_back(twice[0]);
    CHECK(refused(twice) == ErrorCode::Invalid);

    auto xboxScript = sampleCatalogue();
    xboxScript.push_back(
        {.name = "scripts/main", .kind = Kind::Script, .source = Source::Xbox, .archive = "xbox/common/scripts.cpak"});
    CHECK(refused(xboxScript) == ErrorCode::Invalid);

    auto twoKinds = sampleCatalogue();
    twoKinds[2].kind = Kind::Model;
    CHECK(refused(twoKinds) == ErrorCode::Invalid);

    CHECK(refused(sampleCatalogue(), {hudMod("", "a.cpak")}) == ErrorCode::Invalid);
    CHECK(refused(sampleCatalogue(), {hudMod("same", "a.cpak"), hudMod("same", "b.cpak")}) == ErrorCode::Invalid);
    Mod repeats = hudMod("repeats", "a.cpak");
    repeats.entries.push_back(repeats.entries[0]);
    CHECK(refused(sampleCatalogue(), {repeats}) == ErrorCode::Invalid);
    Mod otherKind{.id = "kind", .entries = {{.name = "scripts/main", .kind = Kind::Text, .archive = "a.cpak"}}};
    CHECK(refused(sampleCatalogue(), {otherKind}) == ErrorCode::Invalid);
    Mod badModName{.id = "name", .entries = {{.name = "/abs", .kind = Kind::Texture, .archive = "a.cpak"}}};
    CHECK(refused(sampleCatalogue(), {badModName}) == ErrorCode::Invalid);

    CHECK(refused(sampleCatalogue(), {}, {{.id = "", .textureKeys = {}}}) == ErrorCode::Invalid);
    CHECK(refused(sampleCatalogue(), {}, {{.id = "p", .textureKeys = {}}, {.id = "p", .textureKeys = {}}}) ==
          ErrorCode::Invalid);
}
