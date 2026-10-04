// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/pad.h"

namespace coney {

/// The two controller ports of a PS2.
inline constexpr std::size_t kPadPorts = 2;

/// One sample for each port, port 1 first: what an input source delivers once per step.
using PortSamples = std::array<PadSample, kPadPorts>;

/// Where pad samples come from: SDL gamepads and the keyboard in a normal run, a script in test mode. The engine asks
/// it once per step and never reads a device itself, so a test can replace it.
class InputSource {
  public:
    virtual ~InputSource() = default;
    InputSource() = default;
    InputSource(const InputSource&) = delete;
    InputSource& operator=(const InputSource&) = delete;
    InputSource(InputSource&&) = delete;
    InputSource& operator=(InputSource&&) = delete;

    /// The pads' state for frame `frame` (0-based, FrameTime::index). The loop asks for each frame once, in order.
    [[nodiscard]] virtual PortSamples sample(std::uint64_t frame) = 0;
};

/// All the game's pad records: 8, of which only record 0 (port 1) and record 4 (port 2, first multitap slot) are ever
/// updated, as in the original. The others stay disconnected; they exist so record numbers match the original's.
///
/// Research: docs/research/frontend.md#pad-record
class Pads {
  public:
    /// How many records there are.
    static constexpr std::size_t kRecords = 8;

    /// The record a port's pad is read into: 0 for port 1, 4 for port 2 (CONEY_ASSERT for any other port).
    [[nodiscard]] static std::size_t recordOfPort(std::size_t port);

    /// Updates the records of both ports from one sample each.
    /// @orig 0x001454a8 Pads_Update (unknown)
    void update(const PortSamples& samples);

    /// Record `index`, 0 to kRecords - 1 (CONEY_ASSERT otherwise).
    [[nodiscard]] const Pad& record(std::size_t index) const;
    /// The record of `port` (0 for port 1, 1 for port 2).
    [[nodiscard]] const Pad& port(std::size_t port) const { return record(recordOfPort(port)); }

  private:
    std::array<Pad, kRecords> m_records{};
};

} // namespace coney
