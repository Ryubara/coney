// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "graphics/light_manager.h"
#include "world/world_streams.h"

// The original's light-selection trace (`coney-tools trace lights`, docs/guides/coney-tools-trace.md#trace-lights)
// read back, and Coney's selection replayed against it: each call's inputs from the trace go through
// graphics::LightManager::select(), and the lights it chooses are compared with the lights the original chose
// (docs/guides/realtime-lighting.md#test-selection). Coney's own debug tooling: the replay ports nothing, so nothing
// here carries an @orig tag; the rule it checks is LightManager_SelectLights (0x0017de10,
// docs/research/lighting.md#select).

namespace coney::debug {

/// One light as the trace writes it: values only, never a pointer.
struct TraceLight {
    std::uint32_t type = 0; ///< RenderWare's light type: 1 directional, 2 ambient, 0x80 point, 0x81 spot.
    float radius = 0.0F;
    graphics::LightColour colour;
    world::Vec3 position; ///< Its frame's position, RenderWare's axes.
};

/// The lists a call's inputs hold, in the order the trace's files give them: the copied lists A (the world's), B (the
/// objects') and C (the objects' with the pulse), then the manager's point lights `+0x20` (all) and `+0x30` (those
/// that light the world).
enum class TraceList : std::uint8_t { A, B, C, Objects, World };
/// How many lists a call holds.
inline constexpr std::size_t kTraceLists = 5;

/// One call of the selection in the trace: its inputs and the lights the original chose, in order.
struct TraceCall {
    std::uint32_t caller = 0; ///< The caller's return address (`0x001746f8` a human, ...).
    float distSq = 0.0F;
    graphics::LightSphere sphere; ///< RenderWare's axes.
    std::uint32_t flags = 0;      ///< Bit 0: the objects' lists, else the world's.
    bool pointLights = false;
    bool pulse = false;
    bool human = false;
    std::optional<std::uint8_t> glow; ///< The human's glow byte (`+0x647`); none for a call with no human.
    std::uint32_t tick = 0;           ///< The game's tick count at the call.
    std::array<std::vector<TraceLight>, kTraceLists> lists;
    std::vector<TraceLight> chosen;

    /// The list `list` as the call found it.
    [[nodiscard]] const std::vector<TraceLight>& list(TraceList list) const {
        return lists.at(static_cast<std::size_t>(list));
    }
    /// Whether the original added the glow in this call: a human's call with point lights and a glow byte above 10.
    [[nodiscard]] bool glows() const;
};

/// The calls of a trace from the text of its three files: the selections (`CSV`), `<stem>.calls.csv` and
/// `<stem>.lists.csv`. Fails (ErrorCode::Invalid) on a file whose header or fields are not the trace's, or whose
/// calls disagree with another's: a call number out of order, a list longer or shorter than the calls file says.
[[nodiscard]] std::expected<std::vector<TraceCall>, Error>
parseLightTrace(std::string_view selections, std::string_view calls, std::string_view lists);

/// The calls of the trace whose selections file is `selections`, its two inputs files read from beside it.
[[nodiscard]] std::expected<std::vector<TraceCall>, Error> readLightTrace(const std::filesystem::path& selections);

/// The name the trace gives the caller at `address` (`human`, `sector`, `object`, `car`, `background`), or `other`.
[[nodiscard]] std::string_view traceCallerName(std::uint32_t address);

/// How far apart an atomic's sphere centre and a trace call's may be and still be one atomic, metres.
inline constexpr float kTraceMatchDistance = 0.05F;

/// The call of `calls` from `caller` whose sphere centre is nearest `centre`, within kTraceMatchDistance; null for
/// none. Stub: finds nothing.
[[nodiscard]] const TraceCall* traceCallNear(std::span<const TraceCall> calls, std::uint32_t caller,
                                             world::Vec3 centre);

/// A light as the lab shows it. Stub: empty.
[[nodiscard]] std::string traceLightText(const TraceLight& light);

/// The lights Coney's selection chooses for `call`: a light manager holding each distinct light of the call's lists,
/// with the call's lists as its viewport's, asked LightManager::select() with the call's arguments. In a glow call the
/// glow is placed and coloured as the original's chosen light after the copied list (the trace holds the glow in no
/// list), so the lights after it are what is compared.
[[nodiscard]] std::vector<TraceLight> replayCall(const TraceCall& call);

/// Whether two lights are the same: the type, and the radius, colour and position within a float's rounding of the
/// trace's text (the glow's colour, a byte over 255, within half a step).
[[nodiscard]] bool sameLight(const TraceLight& a, const TraceLight& b);

/// One call whose lights differ: the original's and Coney's, in order.
struct ReplayDifference {
    std::size_t call = 0; ///< Its row in the trace, from 0.
    std::uint32_t caller = 0;
    std::uint32_t tick = 0;
    std::vector<TraceLight> original;
    std::vector<TraceLight> coney;
};

/// The result of a replay: every call compared, and each that differs.
struct LightReplay {
    std::size_t calls = 0;     ///< Calls replayed.
    std::size_t equal = 0;     ///< Calls where Coney chose the original's lights in the original's order.
    std::size_t glowCalls = 0; ///< Calls that added the glow (compared after it).
    std::vector<ReplayDifference> differences;
};

/// Replays every call of `calls` (replayCall()) and compares Coney's lights with the original's.
[[nodiscard]] LightReplay replayLightTrace(std::span<const TraceCall> calls);

/// The replay as text: the counts, then one line per differing call with its row, caller, tick, both lists by kind and
/// the first slot that differs. Kinds and counts only, no positions: a trace measures the game.
[[nodiscard]] std::string lightReplayReport(const LightReplay& replay);

} // namespace coney::debug
