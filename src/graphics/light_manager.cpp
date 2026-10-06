// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/light_manager.h"

#include <algorithm>
#include <cmath>

namespace coney::graphics {

namespace {

// a - b.
world::Vec3 sub(world::Vec3 a, world::Vec3 b) { return world::Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }

// The dot product.
float dot(world::Vec3 a, world::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Whether a light of `type` is offset by the brightness and the colour offset: ambient and directional lights only.
bool takesOffsets(LightType type) { return type == LightType::Ambient || type == LightType::Directional; }

// `colour` with `added` added to R, G and B (alpha kept).
LightColour plus(LightColour colour, LightColour added) {
    return LightColour{colour.r + added.r, colour.g + added.g, colour.b + added.b, colour.a};
}

// `colour`'s R, G and B scaled by `scale` (alpha kept), as the flicker scales a light.
LightColour scaled(LightColour colour, float scale) {
    return LightColour{colour.r * scale, colour.g * scale, colour.b * scale, colour.a};
}

// A colour component 0-1 as a byte, ×255 and clamped.
std::uint8_t toByte(float value) { return static_cast<std::uint8_t>(std::clamp(value * 255.0F, 0.0F, 255.0F)); }

// The cull radius of a light: its radius, or the corona's size for a light of radius 0.
float cullRadius(const LightRecord& record) {
    return record.desc.radius > 0.0F ? record.desc.radius : record.desc.coronaSize;
}

// The squared distance of the camera beyond which the corona's alpha is not capped: 8 m.
constexpr float kCoronaFadeDistance = 8.0F;
// The cap's slope: 255 at 8 m.
constexpr float kCoronaFadeSlope = 31.87F;
// The faintest corona drawn.
constexpr int kCoronaMinAlpha = 11;
// The point lights' cull: lights within this fraction of the draw distance (plus their radius).
constexpr float kCullFraction = 0.75F;
// Objects lose a light per 1600 m² of squared distance (40 m, 56.6 m, 69.3 m).
constexpr float kObjectLightStep = 1600.0F;

} // namespace

LightManager::LightManager() : m_records(kLightCapacity) {
    const float level = static_cast<float>(kDefaultBrightness) / 255.0F;
    m_brightness = LightColour{level, level, level, 0.0F};

    // Light A, the world ambient: lights the world, on; its colour is set at each viewport from +0x50.
    LightDescriptor worldAmbient;
    worldAmbient.type = LightType::Ambient;
    worldAmbient.colour = LightColour{0.0F, 0.0F, 0.0F, 1.0F};
    worldAmbient.lights = kLightsWorld;
    addLight(worldAmbient);

    // Light B, the pulsing ambient: lights objects, off; its colour pulses at each viewport.
    LightDescriptor pulse = worldAmbient;
    pulse.lights = kLightsObjects;
    pulse.on = false;
    addLight(pulse);

    // The glow: a white point light of radius 0.4 that lights objects, off; select() places it at a glowing human.
    LightDescriptor glow;
    glow.type = LightType::Point;
    glow.colour = LightColour{1.0F, 1.0F, 1.0F, 1.0F};
    glow.radius = kGlowRadius;
    glow.lights = kLightsObjects;
    glow.on = false;
    addLight(glow);
}

LightHandle LightManager::addLight(const LightDescriptor& desc) {
    const auto free = std::ranges::find_if(m_records, [](const LightRecord& record) { return !record.inUse; });
    if (free == m_records.end()) {
        return 0;
    }
    *free = LightRecord{};
    free->inUse = true;
    apply(*free, desc);
    return static_cast<LightHandle>(free - m_records.begin()) + 1;
}

LightRecord* LightManager::find(LightHandle handle) {
    if (handle == 0 || handle > m_records.size()) {
        return nullptr;
    }
    LightRecord& record = m_records[handle - 1];
    return record.inUse ? &record : nullptr;
}

const LightRecord* LightManager::light(LightHandle handle) const {
    return const_cast<LightManager*>(this)->find(handle); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

std::size_t LightManager::count() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_records, [](const LightRecord& r) { return r.inUse; }));
}

bool LightManager::setLight(LightHandle handle, const LightDescriptor& desc) {
    LightRecord* record = find(handle);
    if (record == nullptr) {
        return false;
    }
    apply(*record, desc);
    return true;
}

bool LightManager::setOn(LightHandle handle, bool on) {
    LightRecord* record = find(handle);
    if (record == nullptr) {
        return false;
    }
    record->desc.on = on;
    return true;
}

bool LightManager::removeLight(LightHandle handle) {
    LightRecord* record = find(handle);
    if (record == nullptr || handle - 1 <= kGlow) {
        return false;
    }
    *record = LightRecord{};
    return true;
}

void LightManager::apply(LightRecord& record, const LightDescriptor& desc) const {
    record.desc = desc;
    record.current = takesOffsets(desc.type) ? plus(desc.colour, addedColour()) : desc.colour;
}

void LightManager::reapplyOffsets() {
    for (LightRecord& record : m_records) {
        if (record.inUse && takesOffsets(record.desc.type)) {
            record.current = plus(record.desc.colour, addedColour());
        }
    }
}

LightColour LightManager::addedColour() const {
    return LightColour{std::max(0.0F, m_brightness.r + m_offset.r), std::max(0.0F, m_brightness.g + m_offset.g),
                       std::max(0.0F, m_brightness.b + m_offset.b), 0.0F};
}

void LightManager::setWorldAmbient(float r, float g, float b) {
    m_worldAmbient = LightColour{r + kWorldAmbientBias, g + kWorldAmbientBias, b + kWorldAmbientBias, 1.0F};
}

void LightManager::setBrightness(int value) {
    const float level = static_cast<float>(value) / 255.0F;
    m_brightness = LightColour{level, level, level, 0.0F};
    reapplyOffsets();
}

void LightManager::setColourOffset(float r, float g, float b) {
    m_offset = LightColour{r, g, b, 0.0F};
    reapplyOffsets();
}

bool LightManager::setFlicker(LightHandle handle, const FlickerTiming& timing) {
    LightRecord* record = find(handle);
    if (record == nullptr) {
        return false;
    }
    record->flicker = timing;
    record->desc.effects =
        (record->desc.effects & ~kEffectFlickerMask) | (timing.offTime != 0 ? kFlickerBlink : kFlickerBurst);
    record->current = record->desc.colour;
    record->dimmed = false;
    if (timing.offTime != 0) {
        // Blink starts on, or after the first delay when there is one.
        record->timerMs = static_cast<std::int32_t>(timing.pauseRandom != 0 ? timing.pauseRandom
                                                                            : timing.onTime + rand(timing.onRandom));
    } else {
        // Burst starts with a pause at the base colour, then the first burst (Coney's reading: the page gives the
        // cycle, not where it starts).
        record->timerMs = static_cast<std::int32_t>(timing.pause + rand(timing.pauseRandom));
        record->burstLeft = timing.burst + rand(timing.burstRandom);
    }
    return true;
}

std::uint32_t LightManager::rand(std::uint32_t n) {
    if (n == 0) {
        return 0;
    }
    return m_random.next() % n;
}

bool LightManager::visible(const LightRecord& record, const LightView& view) {
    const float r = cullRadius(record);
    const world::Vec3 offset = sub(record.desc.position, view.position);
    const float reach = view.farClip * kCullFraction;
    if (dot(offset, offset) >= reach * reach + r * r) {
        return false;
    }
    // RenderWare's first two frustum planes are the far and the near plane.
    const float depth = dot(offset, view.forward);
    return depth <= view.farClip + r && depth >= view.nearClip - r;
}

void LightManager::advance(const LightView& view, std::uint32_t elapsedMs) {
    for (LightRecord& record : m_records) {
        if (!record.inUse || !record.desc.on || (record.desc.effects & kEffectFlickerMask) == 0) {
            continue;
        }
        if (record.desc.type != LightType::Point && record.desc.type != LightType::Spot) {
            continue;
        }
        if (!visible(record, view)) {
            continue;
        }
        record.timerMs -= static_cast<std::int32_t>(elapsedMs);
        if (record.timerMs <= 0) {
            flickerStep(record);
        }
    }
}

void LightManager::flickerStep(LightRecord& record) {
    const FlickerTiming& t = record.flicker;
    const LightColour base = record.desc.colour;
    switch (record.desc.effects & kEffectFlickerMask) {
    case kFlickerRandom:
        record.timerMs = static_cast<std::int32_t>(rand(200));
        record.current = scaled(base, static_cast<float>(rand(100)) / 100.0F);
        break;
    case kFlickerFade:
        // The base becomes the current colour faded; the shining colour is not set here (an open question).
        record.timerMs = static_cast<std::int32_t>(rand(42));
        record.desc.colour = scaled(record.current, static_cast<float>(72 + rand(28)) / 100.0F);
        break;
    case kFlickerBurst:
        if (record.burstLeft > 0) {
            --record.burstLeft;
            record.timerMs = static_cast<std::int32_t>(rand(t.flickerTime));
            record.current = scaled(base, static_cast<float>(rand(t.dim)) / 100.0F);
        } else {
            // The burst is over: a pause at the base colour, and the next burst's count.
            record.timerMs = static_cast<std::int32_t>(t.pause + rand(t.pauseRandom));
            record.current = base;
            record.burstLeft = t.burst + rand(t.burstRandom);
        }
        break;
    case kFlickerBlink:
        record.dimmed = !record.dimmed;
        if (record.dimmed) {
            record.timerMs = static_cast<std::int32_t>(t.offTime + rand(t.offRandom));
            record.current = scaled(base, static_cast<float>(t.dim) / 100.0F);
        } else {
            record.timerMs = static_cast<std::int32_t>(t.onTime + rand(t.onRandom));
            record.current = base;
        }
        break;
    default:
        record.timerMs = 0;
        break;
    }
    // A timer of 0 would step again at once; the next step comes at the next advance() either way.
    record.timerMs = std::max(record.timerMs, 0);
}

void LightManager::beginViewport(const LightView& view, std::uint64_t nowMs) {
    const LightColour added = addedColour();

    // Light A from +0x50; light B a triangle wave between black and 0.25 grey, 600 ms a period.
    m_records[kWorldAmbient].current = plus(m_worldAmbient, added);
    const float phase = static_cast<float>(nowMs % kPulseHalfMs) / static_cast<float>(kPulseHalfMs);
    const bool falling = (nowMs / kPulseHalfMs) % 2 == 1;
    const float pulse = kPulseHigh * (falling ? 1.0F - phase : phase);
    m_records[kPulse].current = plus(LightColour{pulse, pulse, pulse, 1.0F}, added);

    m_listA.clear();
    m_listB.clear();
    m_listC.clear();
    m_points.clear();
    m_worldPoints.clear();
    m_coronas.clear();
    bool pulseTaken = false;
    for (std::size_t i = 0; i < m_records.size(); ++i) {
        const LightRecord& record = m_records[i];
        if (!record.inUse) {
            continue;
        }
        const auto index = static_cast<std::uint16_t>(i);
        // 1. Ambient and directional lights, into the lists by what they light.
        if (takesOffsets(record.desc.type)) {
            if ((record.desc.lights & kLightsWorld) != 0 && record.desc.on) {
                m_listA.push_back(index);
            }
            if ((record.desc.lights & kLightsObjects) != 0) {
                if (record.desc.on) {
                    m_listB.push_back(index);
                    m_listC.push_back(index);
                } else if (!pulseTaken) {
                    m_listC.push_back(index);
                    pulseTaken = true;
                }
            }
            continue;
        }
        // 2. Point and spot lights that are on and visible.
        if (!record.desc.on || !visible(record, view)) {
            continue;
        }
        // 3. A light with a radius joins the point lists; every visible light may draw its corona.
        if (record.desc.radius > 0.0F) {
            m_points.push_back(index);
            if ((record.desc.lights & kLightsWorld) != 0) {
                m_worldPoints.push_back(index);
            }
        }
        if (record.desc.corona >= 0) {
            addCorona(record, view);
        }
    }
}

void LightManager::addCorona(const LightRecord& record, const LightView& view) {
    // Raised by the height, then pulled towards the camera.
    world::Vec3 position = record.desc.position;
    position.y += record.desc.coronaHeight;
    const world::Vec3 toCamera = sub(view.position, position);
    const float distance = std::sqrt(dot(toCamera, toCamera));
    if (distance > 0.0F) {
        const float pull = record.desc.coronaPull / distance;
        position =
            world::Vec3{position.x + toCamera.x * pull, position.y + toCamera.y * pull, position.z + toCamera.z * pull};
    }
    float alpha = std::clamp(record.current.a * 255.0F, 0.0F, 255.0F);
    if (distance < kCoronaFadeDistance) {
        alpha = std::min(alpha, kCoronaFadeSlope * distance);
    }
    if (alpha < static_cast<float>(kCoronaMinAlpha)) {
        return;
    }
    m_coronas.push_back(CoronaSprite{
        .position = position,
        .size = record.desc.coronaSize,
        .rect = record.desc.corona,
        .rgba = {toByte(record.current.r), toByte(record.current.g), toByte(record.current.b),
                 static_cast<std::uint8_t>(alpha)},
    });
}

LightSelection LightManager::select(float distSq, const LightSphere& sphere, bool objects, bool pointLights, bool pulse,
                                    const std::optional<GlowRequest>& glow) {
    LightSelection selection;
    // Ambient and directional lights: the world's list, or the objects' with a cap that falls with distance.
    std::size_t cap = kMaxSelectedLights;
    std::span<const std::uint16_t> lights = m_listA;
    if (objects) {
        const int steps = static_cast<int>(distSq / kObjectLightStep);
        cap = static_cast<std::size_t>(6 - std::min(steps, pulse ? 2 : 3));
        lights = pulse ? std::span<const std::uint16_t>(m_listC) : std::span<const std::uint16_t>(m_listB);
    }
    for (const std::uint16_t index : lights) {
        if (selection.count >= cap) {
            break;
        }
        selection.records.at(selection.count++) = index;
    }
    if (!pointLights) {
        return selection;
    }

    // The glow first, for a human whose glow alpha is above 10, placed at him with its colour.
    if (glow && glow->rgba[3] > kGlowMinAlpha && selection.count < cap) {
        LightRecord& record = m_records[kGlow];
        record.desc.position = glow->position;
        record.current =
            LightColour{static_cast<float>(glow->rgba[0]) / 255.0F, static_cast<float>(glow->rgba[1]) / 255.0F,
                        static_cast<float>(glow->rgba[2]) / 255.0F, static_cast<float>(glow->rgba[3]) / 255.0F};
        selection.records.at(selection.count++) = kGlow;
    }

    // Then the overlapping point lights; when the list is full, one may replace the kept light of smallest score.
    const std::size_t firstPoint = selection.count;
    std::array<float, kMaxSelectedLights> scores{};
    for (const std::uint16_t index : objects ? m_points : m_worldPoints) {
        const LightRecord& record = m_records[index];
        const world::Vec3 offset = sub(record.desc.position, sphere.centre);
        const float reach = sphere.radius + record.desc.radius;
        const float score = dot(offset, offset) - reach * reach;
        if (score > 0.0F) {
            continue;
        }
        if (selection.count < cap) {
            scores.at(selection.count) = score;
            selection.records.at(selection.count++) = index;
            continue;
        }
        if (firstPoint >= selection.count) {
            continue; // no point light kept to replace
        }
        std::size_t smallest = firstPoint;
        for (std::size_t i = firstPoint + 1; i < selection.count; ++i) {
            if (scores.at(i) < scores.at(smallest)) {
                smallest = i;
            }
        }
        if (score < scores.at(smallest)) {
            scores.at(smallest) = score;
            selection.records.at(smallest) = index;
        }
    }
    return selection;
}

} // namespace coney::graphics
