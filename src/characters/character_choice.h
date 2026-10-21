// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace coney::characters {

/// A character type a player can be made as, with the model a player of it is drawn as
/// (docs/research/characters.md#type-to-model): what the play mode's change of character and its AI fighters' models
/// choose from. Coney's own list, made from the configuration's types.
struct CharacterChoice {
    int type = 0;      ///< The character type (`CfgChar`'s).
    std::string model; ///< The model a player of the type is drawn as.
};

} // namespace coney::characters
