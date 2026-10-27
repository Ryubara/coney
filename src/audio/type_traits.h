// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "audio/human_sound_events.h"

namespace coney::script {
class RecordedCalls;
} // namespace coney::script

namespace coney::audio {

/// The traits of character `type` that `recorded` (the scripts' recorded configuration calls) gives: female and the
/// boss class from the type's last `CfgChar` record, the combat factor from the last `CfgBreathingSound`; canSpeak is
/// left true (it is the human's, not the type's). Called for every sound a human makes, so it parses only the records
/// of `type`: a crowd's footsteps and blows each parsing every type's record were the frame spikes of issue 995
/// (docs/guides/performance.md#spikes-995).
[[nodiscard]] HumanTraits typeTraits(const script::RecordedCalls& recorded, int type);

} // namespace coney::audio
