// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/pads.h"

namespace coney {
class LevelPickups;
namespace human {
class Human;
}
namespace world_objects {
class Cars;
class SpawnRecords;
} // namespace world_objects
} // namespace coney

namespace coney::platform {

class PlayLevelMode;

/// What a frame left on screen for a driver program, read from the level in play: everything a player sees and a
/// driver needs to play by sight (docs/guides/building.md#pad-pipe). Null members: nothing of that kind (no level).
struct PipeView {
    const PlayLevelMode* mode = nullptr;
    const world_objects::SpawnRecords* records = nullptr;
    const LevelPickups* pickups = nullptr;
    const world_objects::Cars* cars = nullptr; ///< for the car stereos, which are not spawn records in Coney
};

/// One observation line's JSON (no newline) for `view` at `frame`: the player (feet, heading in degrees), the camera
/// (eye and target), the other humans (`[x, y, alive, standing, enemy]`, enemy meaning of a gang hostile to player
/// 1's) and the world objects whose type name starts with one of `objectPrefixes` (`[type, x, y, shown]`, shown 1, 0,
/// or -1 when no script said; objects removed, hidden or in a hand are left out). A car stereo still in or at its car
/// is listed as a `dyn_carstereo` object, as the original keeps it among the world objects (docs/research/cars.md).
[[nodiscard]] std::string observationJson(std::uint64_t frame, const PipeView& view,
                                          const std::vector<std::string>& objectPrefixes);

/// `--pad-pipe`: player 1's pad from a driver program, in lock step with the game. Before each frame it writes the
/// observation of the frame before (`@obs {json}`) and reads lines until a pad line:
///
/// - `pad BUTTONS RX RY LX LY`: the buttons as a hexadecimal word (pad::kL2 ... pad::kLeft) and the four raw stick
///   bytes, for this frame; pressure buttons held read 255.
/// - `observe PREFIX...`: the world objects to report from now on (observationJson()).
///
/// At the end of its input it gives a released pad. Port 2 stays disconnected. Lines the game prints itself go to the
/// same output, so a driver reads only the lines starting with `@`.
class PadPipe final : public InputSource {
  public:
    /// The view of the level in play when a frame is sampled.
    using Viewer = std::function<PipeView()>;

    /// Reads pads from `in` and writes observations to `out` (both must outlive the pipe).
    PadPipe(std::istream& in, std::ostream& out, Viewer viewer);

    /// The observation of the last frame, then the driver's pad for frame `frame`.
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

    /// Parses a `pad` line's fields (after the word) into `pad`; false when they are not five numbers in range.
    [[nodiscard]] static bool parsePad(std::string_view fields, PadSample& pad);

  private:
    std::istream& m_in;
    std::ostream& m_out;
    Viewer m_viewer;
    std::vector<std::string> m_prefixes; // `observe`'s object type prefixes
    bool m_ended = false;                // the input ended: released pads from now on
};

/// Watches the humans of the level in play for the event log (coney::events): `human_in` when one appears,
/// `human_out` when its health runs out, `human_gone` when it is deleted. Run once a frame, after the frame's step.
class HumanWatch {
  public:
    /// Compares the humans of `mode` (null: none) with the last call's and emits what changed.
    void update(const PlayLevelMode* mode);

  private:
    std::map<const human::Human*, bool> m_out; // each human seen, and whether its health had run out
};

} // namespace coney::platform
