// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the test commit: the barrier waits for nothing yet.
#include "net/level_barrier.h"

namespace coney::net {

LevelBarrier::LevelBarrier(LevelBarrierConfig config) : m_config(config) {}
const LoadLevel& LevelBarrier::load(const std::string& level, std::uint16_t checkpoint) {
    m_current = LoadLevel{.load = 1, .level = level, .checkpoint = checkpoint, .reason = LoadReason::Change};
    return *m_current;
}
LoadLevel LevelBarrier::joinOrder() const { return m_current.value_or(LoadLevel{}); }
void LevelBarrier::playerJoined(std::uint8_t /*slot*/) {}
void LevelBarrier::playerLeft(std::uint8_t /*slot*/) {}
LoadReply LevelBarrier::loaded(std::uint8_t /*slot*/, std::uint32_t /*load*/) { return LoadReply::Late; }
bool LevelBarrier::loading(std::uint8_t /*slot*/) const { return false; }
bool LevelBarrier::ready() const { return true; }
std::vector<std::uint8_t> LevelBarrier::step() { return {}; }
void LevelBarrier::startMovie(std::uint32_t /*movieStep*/) {}
void LevelBarrier::endHostMovie() {}
void LevelBarrier::movieDone(std::uint8_t /*slot*/, std::uint32_t /*movieStep*/, bool /*skipped*/) {}
bool LevelBarrier::movieMayEnd() const { return true; }

} // namespace coney::net
