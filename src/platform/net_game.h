// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>

#include "core/error.h"
#include "core/options.h"
#include "core/pads.h"
#include "net/game_humans.h"
#include "net/session_record.h"
#include "net/state_digest.h"

// Network play's view of the running game, and the files that prove it without a person: the humans the host's
// snapshots send, the state digest of every step (`--digest`), the digest of every snapshot (`--digest-net`) and a
// host's session record and replay (`--net-record`, `--net-replay`). Coney's own; the original has no network play
// (docs/guides/multiplayer.md#digest).

namespace coney::platform {

class NetPlay;

/// The run's side of network play's proof: the play modes capture their humans into humans() each step; the game's pads
/// pass through input(), which replays or records the remote port; afterStep() writes the step's digest and hands the
/// humans to network play before it sends.
class NetGame final : public InputSource {
  public:
    /// Opens the files `options` name (`--digest`, `--digest-net`, `--net-record`, `--net-replay`); fails with Io when
    /// one cannot be written or read, and with Invalid for a replay that is not a session record.
    [[nodiscard]] static std::expected<std::unique_ptr<NetGame>, Error> open(const Options& options);

    /// The game's humans, which the play modes capture into after each step (PlayLevelMode::shareHumansWith()).
    [[nodiscard]] net::GameHumans& humans() { return m_humans; }
    /// Puts `inner` (null: no pads) under input(): what the game reads is its sample, with port 2 replayed from the
    /// `--net-replay` record when there is one, and recorded when `--net-record` asks.
    void setInner(InputSource* inner) { m_inner = inner; }
    /// The inner source's sample for `frame` (the step's number), port 2 replayed or recorded; remembers what the step
    /// played for its digest.
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;
    /// Gives `net` the digest sink of `--digest-net`, when asked for.
    void attach(NetPlay& net);
    /// After a step, before network play sends: writes the step's digest line (`--digest`) and hands the humans to
    /// `net` for its snapshots. Then decides the next step's capture: the humans are captured only while `net` has a
    /// session or `--digest` or `--net-replay` needs them, and with no session their retired ids are released, since
    /// no peer can name them.
    void afterStep(NetPlay& net);
    /// The step gate's network part, asked once for each due step after its pads were read: whether the step is held
    /// for the loading barrier (docs/guides/multiplayer.md#levels). Live, it is `sessionHolds`
    /// (NetLevels::holdsStep()), and a held step goes in the session record as a hold line; replaying a record, it is
    /// whether the host held this step, so the replay reads the same pads on the same steps and its digest lines match
    /// the host's.
    [[nodiscard]] bool holdsStep(bool sessionHolds);
    /// Notes a line of network play's log in the session record, at the step being played.
    void note(std::string_view line);

  private:
    NetGame() = default;

    net::GameHumans m_humans;
    bool m_proofNeedsHumans = false; // `--digest` or `--net-replay`: capture with no session too
    InputSource* m_inner = nullptr;
    std::optional<net::SessionReplay> m_replay;
    std::optional<std::ofstream> m_digest;
    std::optional<std::ofstream> m_netDigest;
    std::optional<std::ofstream> m_record;
    PortSamples m_played{};    // the pads the last step read
    std::uint64_t m_frame = 0; // the last step's number
};

} // namespace coney::platform
