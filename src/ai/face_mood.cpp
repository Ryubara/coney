// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/face_mood.h"

// Stubs for the tests to compile against before the mood is ported: they return defaults.

namespace coney::ai {

human::FaceMood faceMoodOf(const FaceMoodFacts& /*facts*/) { return human::FaceMood::Neutral; }

FaceMoodFacts faceMoodFacts(Brain& /*brain*/) { return {}; }

} // namespace coney::ai
