// SPDX-License-Identifier: GPL-3.0-or-later
// From a character type to the model it is drawn as (docs/research/characters.md#type-to-model): Human_Init's class
// switch, the player's plain-alias rule and the `_a` models of levels 60-64. The model names come from a stand-in for
// the recorded `CfgChar` calls written here.
#include "characters/character_class.h"

#include <map>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

using coney::characters::characterClassOf;
using coney::characters::modelNameFor;
using coney::characters::modelRecordType;

namespace {

// The model-name argument of a few types' CfgChar calls, as the reference lists them.
std::optional<std::string> modelOf(int type) {
    static const std::map<int, std::string> kModels{{1, "warr_cl"},  {2, "warr_cl_gen"}, {3, "warr_cl_ghost"},
                                                    {30, "warr_re"}, {32, "warr_re_cv"}, {33, "warr_sn"}};
    const auto found = kModels.find(type);
    return found == kModels.end() ? std::nullopt : std::optional<std::string>(found->second);
}

} // namespace

TEST_CASE("Human_Init's switch maps a type to its class, as a plain alias or a variant", "[character_class]") {
    CHECK(characterClassOf(2).id == 1);
    CHECK(!characterClassOf(2).variant);
    CHECK(characterClassOf(4).id == 1);
    CHECK(characterClassOf(4).variant);
    CHECK(characterClassOf(32).id == 30);
    CHECK(characterClassOf(32).variant);
    CHECK(characterClassOf(31).id == 30);
    CHECK(characterClassOf(40).id == 38);
    CHECK(characterClassOf(222).id == 221);
    // A type not in the switch is its own class.
    CHECK(characterClassOf(33).id == 33);
    CHECK(characterClassOf(352).id == 352);
    CHECK(!characterClassOf(352).variant);
}

TEST_CASE("a player made as a plain alias is drawn as the class's model; anyone else as the type's own",
          "[character_class]") {
    CHECK(modelRecordType(2, 1) == 1);
    CHECK(modelRecordType(2, 0) == 2);
    CHECK(modelRecordType(32, 1) == 32);
    CHECK(modelRecordType(3, 1) == 3);
    CHECK(modelNameFor(2, 1, 5, modelOf) == std::optional<std::string>("warr_cl"));
    CHECK(modelNameFor(2, 0, 5, modelOf) == std::optional<std::string>("warr_cl_gen"));
    CHECK(modelNameFor(32, 1, 99, modelOf) == std::optional<std::string>("warr_re_cv"));
    CHECK(modelNameFor(33, 1, 3, modelOf) == std::optional<std::string>("warr_sn"));
}

TEST_CASE("levels 60 to 64 draw the `_a` model, and a type with no CfgChar call has none", "[character_class]") {
    CHECK(modelNameFor(1, 1, 60, modelOf) == std::optional<std::string>("warr_cl_a"));
    CHECK(modelNameFor(1, 1, 64, modelOf) == std::optional<std::string>("warr_cl_a"));
    CHECK(modelNameFor(1, 1, 65, modelOf) == std::optional<std::string>("warr_cl"));
    CHECK(!modelNameFor(500, 1, 2, modelOf).has_value());
    CHECK(!modelNameFor(1, 1, 2, {}).has_value());
}
