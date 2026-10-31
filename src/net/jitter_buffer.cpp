// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/jitter_buffer.h"

#include <cstdint>
#include <optional>

#include "net/snapshot.h"

namespace coney::net {

JitterBuffer::JitterBuffer(JitterConfig config) : m_config(config) {}

bool JitterBuffer::push(std::uint32_t /*step*/, const SnapshotHumans& /*humans*/) { return false; }

void JitterBuffer::advance() {}

std::optional<JitterSample> JitterBuffer::sample() const { return std::nullopt; }

std::uint32_t JitterBuffer::newestStep() const { return 0; }

std::uint64_t JitterBuffer::delay() const { return 0; }

bool JitterBuffer::settled() const { return false; }

std::uint64_t JitterBuffer::target() const { return 0; }

void JitterBuffer::prune() {}

} // namespace coney::net
