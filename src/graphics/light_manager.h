// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/game_random.h"
#include "world/world_streams.h"

namespace coney::graphics {

/// A light's colour, RGBA floats where 1.0 is full (RenderWare's RwRGBAReal; on the PS2 1.0 is the GS's 0x80).
struct LightColour {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    friend bool operator==(const LightColour&, const LightColour&) = default;
};

/// What kind of RenderWare light a record holds. `SetLight`'s type 0-3 is Point, Spot, Directional, Ambient.
enum class LightType : std::uint8_t {
    Point,       ///< RenderWare 0x80: lights within its radius.
    Spot,        ///< RenderWare 0x81: a point light in a cone.
    Directional, ///< RenderWare 1: lights everything from one direction.
    Ambient,     ///< RenderWare 2: lights everything evenly.
};

/// What a light lights (record `+0x2e`): bit 0 objects and humans, bit 1 the world.
inline constexpr std::uint16_t kLightsObjects = 0x1;
inline constexpr std::uint16_t kLightsWorld = 0x2;

/// The effects word (record `+0x08`): bit 0 spawns light bugs at the light; bits 1-4 select a flicker mode.
inline constexpr std::uint32_t kEffectLightBugs = 0x1;
inline constexpr std::uint32_t kEffectFlickerMask = 0x1e;
/// The flicker modes (the effects word & kEffectFlickerMask).
inline constexpr std::uint32_t kFlickerRandom = 2; ///< Random brightness every rand(200) ms.
inline constexpr std::uint32_t kFlickerFade = 4;   ///< The base colour faded to 72-99 % every rand(42) ms.
inline constexpr std::uint32_t kFlickerBurst = 6;  ///< `SetLightFlicker` with offTime 0: bursts of flickers.
inline constexpr std::uint32_t kFlickerBlink = 8;  ///< `SetLightFlicker` with offTime not 0: on, dimmed, on, ...

/// How many records the manager's pool holds.
inline constexpr std::size_t kLightCapacity = 512;
/// The most lights one atomic is lit by.
inline constexpr std::size_t kMaxSelectedLights = 8;
/// The default brightness option (`PM_Light`, 0-100 in steps of 5).
inline constexpr int kDefaultBrightness = 40;
/// What `SetWorldAmbient` adds to each component.
inline constexpr float kWorldAmbientBias = 0.07F;

/// What a light is made from or set to: the original's light descriptor, as `SetLight`'s long form fills it.
/// Positions and directions are in RenderWare's axes (y up): `SetLight` gives the game's (z up) and turns them into
/// (x, z, -y) (gameToRenderWare()).
struct LightDescriptor {
    LightType type = LightType::Point;
    world::Vec3 position;                  ///< Point and spot lights.
    world::Vec3 direction{0.0F, -1.0F, 0}; ///< Spot and directional lights: the way the light travels (unit).
    LightColour colour;                    ///< The base colour, as the script gave it.
    float radius = 0.0F;                   ///< Point and spot lights, metres; 0 lights nothing (a corona alone).
    float coneAngle = 0.0F;                ///< Spot lights.
    float coronaHeight = 0.0F;             ///< `+0x0c`: metres the corona sits above the light.
    float coronaPull = 0.0F;               ///< `+0x10`: metres the corona is moved towards the camera.
    float coronaSize = 0.0F;               ///< `+0x14`: the corona's size, also the cull radius when radius is 0.
    std::uint16_t lights = 0;              ///< kLightsObjects | kLightsWorld.
    std::uint32_t effects = 0;             ///< kEffectLightBugs | a flicker mode.
    std::int16_t corona = -1;              ///< Rectangle 0-5 of the `lighting` sheet, -1 for none.
    bool on = true;
};

/// `SetLightFlicker`'s ten timings (record `+0x19`-`+0x28`), as the script passes them.
struct FlickerTiming {
    std::uint32_t onTime = 0;      ///< p1, `+0x24`: blink: ms on.
    std::uint32_t onRandom = 0;    ///< p2, `+0x20`: blink: up to this many ms more.
    std::uint32_t offTime = 0;     ///< p3, `+0x26`: blink: ms dimmed; 0 selects burst mode.
    std::uint32_t offRandom = 0;   ///< p4, `+0x22`.
    std::uint32_t pause = 0;       ///< p5, `+0x28`: burst: ms steady between bursts.
    std::uint32_t pauseRandom = 0; ///< p6, `+0x1c`: burst: up to this many ms more; blink: a first delay.
    std::uint32_t burst = 0;       ///< p7, `+0x1a`: burst: flickers in a burst.
    std::uint32_t burstRandom = 0; ///< p8, `+0x1b`.
    std::uint32_t flickerTime = 0; ///< p9, `+0x1e`: burst: each flicker lasts up to this many ms.
    std::uint32_t dim = 0;         ///< p10, `+0x19`: percent: blink's dimmed level, burst's upper bound.
};

/// One light record of the pool (0x50 bytes in the original): the descriptor, the colour the light shines with now
/// (the RpLight's, `+0x04` → `+0x18`) and the flicker state.
struct LightRecord {
    bool inUse = false;
    LightDescriptor desc;        ///< `desc.colour` is the base colour (`+0x40`).
    LightColour current;         ///< What the light shines with (offsets and flicker applied).
    FlickerTiming flicker;       ///< `SetLightFlicker`'s timings.
    std::int32_t timerMs = 0;    ///< `+0x34`: ms to the next flicker step.
    std::uint32_t burstLeft = 0; ///< `+0x18`: flickers left in this burst.
    bool dimmed = false;         ///< Blink and burst: in the dimmed (or flickering) phase.
};

/// A light's handle: its record index plus 1, so 0 is no light, as `SetLight` returns 0 for none.
using LightHandle = std::uint32_t;

/// The camera the manager culls against, in RenderWare's axes.
struct LightView {
    world::Vec3 position;
    world::Vec3 forward{0.0F, 0.0F, 1.0F}; ///< Unit.
    float nearClip = 0.5F;
    float farClip = 115.0F; ///< The draw distance.
};

/// A sphere in RenderWare's axes: an atomic's bounds.
struct LightSphere {
    world::Vec3 centre;
    float radius = 0.0F;
};

/// A human's glow (the human's bytes `+0x644`-`+0x647`): where the manager's glow light is put and its colour.
struct GlowRequest {
    world::Vec3 position;
    std::array<std::uint8_t, 4> rgba{};
};

/// One corona sprite of a frame: the `lighting` sheet's rectangle `rect`, square, `size` metres, facing the camera.
struct CoronaSprite {
    world::Vec3 position; ///< RenderWare's axes.
    float size = 0.0F;
    std::int16_t rect = 0;
    std::array<std::uint8_t, 4> rgba{};
};

/// The lights chosen for one atomic: record indices, ambient and directional lights first, then point lights.
struct LightSelection {
    std::array<std::uint16_t, kMaxSelectedLights> records{};
    std::size_t count = 0;

    [[nodiscard]] std::span<const std::uint16_t> lights() const { return {records.data(), count}; }
};

/// A point in the game's axes (z up) in RenderWare's (y up): (x, z, -y), as `SetLight` stores positions and directions.
[[nodiscard]] constexpr world::Vec3 gameToRenderWare(float x, float y, float z) { return world::Vec3{x, z, -y}; }

/// The light manager (`LightManager`, 0xc0 bytes at `0x0050cce4`): a pool of 512 light records, three of them built in
/// (the world ambient, the pulsing ambient and the glow), the brightness and colour offset added to every ambient and
/// directional light, the per-viewport cull into lists by what the lights light, the per-atomic selection, the flicker
/// and the coronas. Platform-neutral: the platform layer hands the selected records to its renderer.
///
/// **Coney's choices** (determinism, docs/guides/conventions.md#update-and-render): the flicker is stepped by the
/// simulation (advance(), with the step's game time, for the lights its own cull finds visible), not by the render
/// pass as in the original; and it draws from the manager's own GameRandom, so lighting never moves the game's random
/// index. Positions are RenderWare's axes throughout, as the original stores them.
///
/// Research: docs/research/lighting.md
class LightManager {
  public:
    /// The built-in records: the world ambient (light A), the pulsing ambient (light B) and the glow.
    static constexpr std::uint16_t kWorldAmbient = 0;
    static constexpr std::uint16_t kPulse = 1;
    static constexpr std::uint16_t kGlow = 2;
    /// Light B's half period, ms, and its two colours' grey levels.
    static constexpr std::uint64_t kPulseHalfMs = 300;
    static constexpr float kPulseHigh = 0.25F;
    /// The glow's radius, metres.
    static constexpr float kGlowRadius = 0.4F;
    /// A human glows when his glow colour's alpha byte (`+0x647`) is above this.
    static constexpr std::uint8_t kGlowMinAlpha = 10;

    /// The manager after its constructor: the three built-in lights, brightness 40/255, no offset.
    /// @orig 0x0017d640 LightManager_Construct (LightManager.cpp)
    /// @orig 0x0017d338 LightManager_Init (LightManager.cpp)
    LightManager();

    /// Takes the next free record and builds it from `desc`; an ambient or directional light gets the offsets added.
    /// Returns its handle, or 0 when the pool is full.
    /// @orig 0x0017d510 LightManager_AddLight (LightManager.cpp)
    /// @orig 0x0017c508 Light_InitFromDescriptor (LightManager.cpp)
    LightHandle addLight(const LightDescriptor& desc);
    /// Rewrites the light `handle` from `desc` (its flicker timings kept). False for no such light.
    /// @orig 0x0017c840 Light_ApplyDescriptor (LightManager.cpp)
    bool setLight(LightHandle handle, const LightDescriptor& desc);
    /// Switches the light on or off. False for no such light.
    bool setOn(LightHandle handle, bool on);
    /// Returns the record to the pool. False for no such light (built-in lights cannot be removed).
    /// @orig 0x0017d598 LightManager_RemoveLight (LightManager.cpp)
    bool removeLight(LightHandle handle);
    /// The record of `handle`, or null.
    [[nodiscard]] const LightRecord* light(LightHandle handle) const;
    /// The record at pool index `index` (in use or not).
    [[nodiscard]] const LightRecord& record(std::uint16_t index) const { return m_records.at(index); }
    /// Records in use, the built-in three included.
    [[nodiscard]] std::size_t count() const;

    /// `SetLightFlicker`: blink (offTime not 0) or burst (offTime 0) for light `handle`, the mode written into its
    /// effects word. False for no such light. **Coney stand-in:** a light with radius 0, which the original turns into
    /// the particle `sub_flashing_light`, keeps its corona and flickers it instead (Coney has no particles yet).
    /// @orig 0x0017d0b0 Light_SetFlickerTiming (LightManager.cpp)
    bool setFlicker(LightHandle handle, const FlickerTiming& timing);

    /// `SetWorldAmbient(r, g, b)`: the world ambient's colour `+0x50` becomes (r, g, b) + 0.07.
    /// @orig 0x0017f218 LightManager_SetWorldAmbient (LightManager.cpp)
    void setWorldAmbient(float r, float g, float b);
    /// The brightness option, 0-100: brightness = v / 255 in R, G and B; re-applies every ambient and directional
    /// light.
    /// @orig 0x0017ec38 LightManager_SetBrightness (LightManager.cpp)
    void setBrightness(int value);
    /// `SetGammaOffset({r, g, b})`: the colour offset; re-applies every ambient and directional light.
    /// @orig 0x0017ec80 LightManager_SetOffset (LightManager.cpp)
    void setColourOffset(float r, float g, float b);
    /// What every ambient and directional light gets added: max(0, brightness + offset), component-wise.
    [[nodiscard]] LightColour addedColour() const;
    [[nodiscard]] LightColour brightness() const { return m_brightness; }
    [[nodiscard]] LightColour colourOffset() const { return m_offset; }
    /// The world ambient's stored colour `+0x50` (0 until setWorldAmbient()).
    [[nodiscard]] LightColour worldAmbientBase() const { return m_worldAmbient; }

    /// The simulation's part: steps the flicker of every light on whose sphere the cull would keep for `view` by
    /// `elapsedMs` of game time.
    /// @orig 0x0017caa8 Light_UpdateFlickerAndCorona (LightManager.cpp)
    void advance(const LightView& view, std::uint32_t elapsedMs);

    /// The start of a viewport: the two ambients' colours at game time `nowMs`, then the cull of every record into the
    /// lists select() reads, and this frame's coronas. Changes nothing advance() reads.
    /// @orig 0x0017ea60 LightManager_BeginViewport (LightManager.cpp)
    /// @orig 0x0017d880 LightManager_CullForViewport (LightManager.cpp)
    void beginViewport(const LightView& view, std::uint64_t nowMs);

    /// The lights of one atomic: the world's (`objects` false, up to 8) or the objects' (`objects` true, 6 to 3 by
    /// `distSq`, the squared distance to the camera, from list B, or list C with `pulse`), then with `pointLights` the
    /// glow (when `glow` is given with an alpha above kGlowMinAlpha) and the point lights whose sphere meets `sphere`.
    /// @orig 0x0017de10 LightManager_SelectLights (LightManager.cpp)
    [[nodiscard]] LightSelection select(float distSq, const LightSphere& sphere, bool objects, bool pointLights,
                                        bool pulse, const std::optional<GlowRequest>& glow = std::nullopt);

    /// This viewport's corona sprites (beginViewport()), in record order.
    [[nodiscard]] std::span<const CoronaSprite> coronas() const { return m_coronas; }
    /// This viewport's lists: A (ambient and directional lights of the world), B and C (of objects), and the visible
    /// point lights: all, and those that light the world.
    [[nodiscard]] std::span<const std::uint16_t> worldList() const { return m_listA; }
    [[nodiscard]] std::span<const std::uint16_t> objectList() const { return m_listB; }
    [[nodiscard]] std::span<const std::uint16_t> pulseList() const { return m_listC; }
    [[nodiscard]] std::span<const std::uint16_t> visiblePoints() const { return m_points; }
    [[nodiscard]] std::span<const std::uint16_t> visibleWorldPoints() const { return m_worldPoints; }

    /// The random numbers the flicker draws: set the game's table for the original's sequence.
    [[nodiscard]] GameRandom& random() { return m_random; }

  private:
    // The record of `handle` (in use), or null.
    LightRecord* find(LightHandle handle);
    // Writes `desc` into `record` and sets its current colour, offsets added to an ambient or directional light.
    void apply(LightRecord& record, const LightDescriptor& desc) const;
    // Re-applies the offsets to every ambient and directional light: after the brightness or offset changed.
    void reapplyOffsets();
    // Whether the cull keeps the point or spot light `record` for `view`: within (far × 0.75)² + r² and not wholly
    // beyond the far plane or before the near plane.
    [[nodiscard]] static bool visible(const LightRecord& record, const LightView& view);
    // One flicker step of `record` whose timer ran out.
    void flickerStep(LightRecord& record);
    // A random whole number from 0 to n - 1 (0 when n is 0): the game's rand(n).
    [[nodiscard]] std::uint32_t rand(std::uint32_t n);
    // Adds `record`'s corona to this viewport's when it is bright enough.
    void addCorona(const LightRecord& record, const LightView& view);

    std::vector<LightRecord> m_records;
    LightColour m_worldAmbient{0.0F, 0.0F, 0.0F, 1.0F}; // +0x50
    LightColour m_brightness;                           // +0x90
    LightColour m_offset{0.0F, 0.0F, 0.0F, 0.0F};       // +0xa0
    std::vector<std::uint16_t> m_listA;
    std::vector<std::uint16_t> m_listB;
    std::vector<std::uint16_t> m_listC;
    std::vector<std::uint16_t> m_points;      // +0x20
    std::vector<std::uint16_t> m_worldPoints; // +0x30
    std::vector<CoronaSprite> m_coronas;
    GameRandom m_random;
};

} // namespace coney::graphics
