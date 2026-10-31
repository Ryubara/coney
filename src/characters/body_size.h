// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "core/game_random.h"

// A new human's random body size (docs/research/characters.md#body-scale), drawn by Human_Init after the money.

namespace coney::characters {

/// What the random size reads of a new human: his class's record (`+0xcc` after the remap) and his brain.
struct BodySizeInput {
    int classType = 0;  ///< The class (`+0xcc`): characterClassOf() of the type HuCreate was given.
    int brainType = 0;  ///< The brain type Human_Init stores (ai::createdBrainType()): 1 cop, 2 gang, 3 Warrior.
    int category = 0;   ///< The class record's category (`+0x11b`).
    bool woman = false; ///< The class record's `+0x14b` is not 0 (copied to the human's `+0x3b8`, tested as a byte).
    bool hat = false;   ///< The class record's hat (`+0x14c`) is not `none`: an empty name counts as a hat.
    /// The kind (`+0x2c`) of the gang the human joined (brain `+0x20c`); nothing for no gang.
    std::optional<int> gangKind;
};

/// The random size Human_Init gives a new human, drawing `Random_Int(99)` from the gameplay stream
/// (RandomStream::Gameplay, `0x006eb880`), or nothing, with no draw, for a human the size skips: a Warrior (brain
/// type 3), a class of category 12 or 13, and the types the original lists (122, 221, 222, 303-305, 307, 351, 414-416,
/// 425, 458, 459). With the draw `r`: a woman 1 - 0.01 x (r / 25), at least 0.99 with a hat; anyone else 1.0 for r up
/// to 30; 31-54 without a hat 1.02 for a cop or a gang member whose gang's kind is below 23, else 0.98; otherwise 1.01
/// below 90 and 1.0 from 90. The type's fixed size (typeScaleOf()) replaces it afterwards.
/// @orig 0x00218008 Human_Init (unknown)
[[nodiscard]] std::optional<float> randomBodySize(const BodySizeInput& input, GameRandom& random);

} // namespace coney::characters
