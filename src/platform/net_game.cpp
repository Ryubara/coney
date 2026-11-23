// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/net_game.h"

#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "platform/net_play.h"

namespace coney::platform {

namespace {

// The remote player's port, port 2 (docs/guides/multiplayer.md#player-two).
constexpr std::size_t kRemotePort = 1;

// Opens `path` for writing, replacing it; Io when it cannot be.
std::expected<std::ofstream, Error> openForWriting(const std::string& path, std::string_view flag) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected(Error{ErrorCode::Io, std::format("{}: cannot write {}", flag, path)});
    }
    return file;
}

// The step's number as a record or digest line holds it.
std::uint32_t stepOf(std::uint64_t frame) { return static_cast<std::uint32_t>(frame); }

} // namespace

std::expected<std::unique_ptr<NetGame>, Error> NetGame::open(const Options& options) {
    std::unique_ptr<NetGame> game(new NetGame());
    // Each file the options name: the three written ones replaced, the replay read whole.
    const auto openOptional = [](const std::optional<std::string>& path, std::string_view flag,
                                 std::optional<std::ofstream>& into) -> std::expected<void, Error> {
        if (!path) {
            return {};
        }
        auto file = openForWriting(*path, flag);
        if (!file) {
            return std::unexpected(std::move(file.error()));
        }
        into = std::move(*file);
        return {};
    };
    if (auto opened = openOptional(options.digestFile, "--digest", game->m_digest); !opened) {
        return std::unexpected(std::move(opened.error()));
    }
    if (auto opened = openOptional(options.netDigestFile, "--digest-net", game->m_netDigest); !opened) {
        return std::unexpected(std::move(opened.error()));
    }
    if (auto opened = openOptional(options.netRecordFile, "--net-record", game->m_record); !opened) {
        return std::unexpected(std::move(opened.error()));
    }
    if (const std::optional<std::string>& path = options.netReplayFile; path) {
        std::ifstream file(*path, std::ios::binary);
        if (!file) {
            return std::unexpected(Error{ErrorCode::Io, std::format("--net-replay: cannot read {}", *path)});
        }
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        auto replay = net::SessionReplay::parse(text);
        if (!replay) {
            return std::unexpected(
                Error{ErrorCode::Invalid, std::format("--net-replay {}: {}", *path, replay.error().message)});
        }
        game->m_replay = std::move(*replay);
    }
    // Capture only what needs the humans: a proof, or a session from the command line (one hosted later turns it on
    // after its first step, afterStep()). Offline play keeps no network ids.
    game->m_proofNeedsHumans = options.digestFile.has_value() || options.netReplayFile.has_value();
    game->m_humans.setCapturing(game->m_proofNeedsHumans || options.netHost.has_value() || options.netJoin.has_value());
    return game;
}

PortSamples NetGame::sample(std::uint64_t frame) {
    PortSamples samples = m_inner != nullptr ? m_inner->sample(frame) : PortSamples{};
    if (m_replay) {
        if (const std::optional<PadSample> pad = m_replay->pad(stepOf(frame)); pad) {
            samples.at(kRemotePort) = *pad;
        }
    }
    if (m_record) {
        *m_record << net::formatPadLine(stepOf(frame), samples.at(kRemotePort)) << '\n';
    }
    m_played = samples;
    m_frame = frame;
    return samples;
}

void NetGame::attach(NetPlay& net) {
    if (!m_netDigest) {
        return;
    }
    net.setSnapshotDigests(
        [this](const net::DigestLine& line) { *m_netDigest << net::formatDigestLine(line) << '\n'; });
}

void NetGame::afterStep(NetPlay& net) {
    if (m_digest) {
        const net::SnapshotHumans holding = m_humans.holding();
        *m_digest << net::formatDigestLine(net::DigestLine{.step = stepOf(m_frame),
                                                           .humans = net::humansDigest(holding),
                                                           .humanCount = static_cast<std::uint32_t>(holding.size()),
                                                           .pads = net::padsDigest(m_played)})
                  << '\n';
    }
    net.setGameHumans(m_humans.humans());
    net.setGameHuds(m_humans.huds(), m_humans.hudElements(), m_humans.hudTexts(), m_humans.hudBlips());
    // The next step's capture; with no session nobody can name a retired id, so they are freed for use again.
    const bool session = net.inSession();
    if (!session) {
        m_humans.releaseRetired();
    }
    m_humans.setCapturing(m_proofNeedsHumans || session);
}

bool NetGame::holdsStep(bool sessionHolds) {
    // A replay has no session: the record says which steps the host held.
    if (m_replay) {
        return m_replay->held(stepOf(m_frame));
    }
    if (sessionHolds && m_record) {
        *m_record << net::formatHoldLine(stepOf(m_frame)) << '\n';
    }
    return sessionHolds;
}

void NetGame::note(std::string_view line) {
    if (!m_record) {
        return;
    }
    // Network play prints "net: what happened\n"; the record keeps what happened.
    constexpr std::string_view kPrefix = "net: ";
    if (line.starts_with(kPrefix)) {
        line.remove_prefix(kPrefix.size());
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    if (line.empty() || line.find('\n') != std::string_view::npos) {
        return;
    }
    *m_record << net::formatEventLine(stepOf(m_frame), line) << '\n';
}

} // namespace coney::platform
