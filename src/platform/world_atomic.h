// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "core/error.h"
#include "world/world_streams.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Geometry;
} // namespace rw

namespace coney::platform {

/// Right-to-render data of the game's two atomic pipelines for the streamed world (plugin id 3): the atomics a
/// WorldAtomic unpacks. Research: docs/research/world.md#part-file
inline constexpr std::uint32_t kGamePipelinePlugin = 3;
inline constexpr std::uint32_t kGameAtomicPipelineA = 0x30082;
inline constexpr std::uint32_t kGameAtomicPipelineB = 0x30083;

/// Registers with librw what the streamed world's atomics need: librw's own mesh (0x50E), native data (0x510) and
/// right-to-render (0x1F) plugins; the game's atomic plugin 0x3F0, so its scales are kept with each atomic; and a
/// rights callback for the game's pipeline plugin (id 3), which gives atomics with pipeline 0x30082 or 0x30083 Coney's
/// PS2 pipeline, whose uninstance step decodes the game's vertex layout (librw's own PS2 pipeline cannot). Must run
/// between librw's Engine::init and Engine::open (RenderEngine::start does it); a second call while the same librw
/// engine runs does nothing.
void attachWorldPlugins();

/// The game's atomic plugin data librw kept for `atomic`; the defaults (scales 1) when the stream had none. Only after
/// attachWorldPlugins().
[[nodiscard]] world::AtomicPluginData atomicPluginData(const rw::Atomic* atomic);

/// One atomic of a streamed world's part file, read with librw, with a frame of its own. Owns both and destroys them.
/// Move-only. Needs a running RenderEngine (either backend) started with the world plugins attached, and must be
/// destroyed before the engine stops.
///
/// Research: docs/research/world.md#part-file
class WorldAtomic {
  public:
    /// Reads one standalone atomic section (0x14, header included) and places its frame at `origin`, the sector
    /// plugin's position, as the game does when a part loads. The section is checked with
    /// world::inspectAtomicSection() and every mesh is decoded once with graphics::decodePs2WorldMesh() before librw
    /// sees any of it, so damaged data fails as those do rather than reaching librw; ErrorCode::Invalid if librw still
    /// refuses it or the atomic is not drawn by one of the game's two world pipelines.
    [[nodiscard]] static std::expected<WorldAtomic, Error> read(std::span<const std::byte> section, world::Vec3 origin);

    WorldAtomic(WorldAtomic&& other) noexcept;
    WorldAtomic& operator=(WorldAtomic&& other) noexcept;
    WorldAtomic(const WorldAtomic&) = delete;
    WorldAtomic& operator=(const WorldAtomic&) = delete;
    ~WorldAtomic();

    /// The librw atomic. Valid as long as this object.
    [[nodiscard]] rw::Atomic* atomic() const { return m_atomic; }

    /// What the section's headers said, from the check before reading.
    [[nodiscard]] const world::AtomicSection& info() const { return m_info; }

    /// Turns the PS2 native geometry into plain librw geometry (positions scaled by the 0x3F0 position scale, texture
    /// coordinates, prelighting, normals and triangles), through the atomic's pipeline as librw's own uninstance step,
    /// then hands the atomic back to the platform's default pipeline, so that the GL3 renderer instances it like any
    /// other. Does nothing when the geometry is already plain.
    void unpack();

  private:
    WorldAtomic(rw::Atomic* atomic, world::AtomicSection info) : m_atomic(atomic), m_info(info) {}

    /// Destroys the atomic and its frame, if this object still owns them.
    void destroy() noexcept;

    rw::Atomic* m_atomic = nullptr; // owned, with its frame; null after a move
    world::AtomicSection m_info;
};

} // namespace coney::platform
