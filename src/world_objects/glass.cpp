// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/glass.h"

#include <algorithm>
#include <cmath>

#include "raycast/collision_mesh.h"

namespace coney::world_objects {

namespace {

// How far a car window frees the stereos around it, metres.
constexpr float kCarStereoReach = 2.0F;
// The shard count of a small shatter, and the factor of a pane's area in whole square metres.
constexpr int kSmallShards = 10;
constexpr int kShardsPerSquareMetre = 10;
// Each shard is tried twice, each try taken at 2 in 3.
constexpr int kShardTries = 2;
constexpr int kShardOdds = 3;
constexpr int kShardTaken = 2;
// The resolution of Coney's stand-in for a random offset in [-1, 1].
constexpr int kOffsetSteps = 1000;
// The low 16 bits of a sprite word: the rectangle.
constexpr std::uint32_t kSpriteRectMask = 0xffffU;
// The alarm bits a pane with the alarm gets.
constexpr std::uint16_t kAlarmBits = 3;

// A random offset in [-1, 1] from the game's numbers. **Coney's stand-in**: the page gives the shards' spread, not how
// a position is drawn.
float randomOffset(GameRandom* random) {
    return (static_cast<float>(randomBelow(random, (2 * kOffsetSteps) + 1)) / kOffsetSteps) - 1.0F;
}

} // namespace

ShatterPlan shatterPlan(std::uint32_t sizeWord) {
    const auto width = static_cast<int>(sizeWord & 0xffffU);
    const auto height = static_cast<int>(sizeWord >> 16U);
    const int count = kShardsPerSquareMetre * width * height;
    if (count < 2 || (width == 1 && height == 1)) {
        return ShatterPlan{.count = kSmallShards, .shardSize = kSmallShardSize, .soundMaterial = material::kGlassSmall};
    }
    return ShatterPlan{
        .count = std::min(count, kMaxShards), .shardSize = kHitShardSize, .soundMaterial = material::kGlass};
}

void GlassPanes::setType(int type, const GlassType& entry) {
    if (type >= 0 && static_cast<std::size_t>(type) < m_types.size()) {
        m_types.at(static_cast<std::size_t>(type)) = entry;
    }
}

const GlassType* GlassPanes::type(int type) const {
    if (type < 0 || static_cast<std::size_t>(type) >= m_types.size()) {
        return nullptr;
    }
    return &m_types.at(static_cast<std::size_t>(type));
}

const GlassPane& GlassPanes::spawn(double handle, const GlassSpawn& spawn, ObjectWorld& world) {
    static const GlassType kNoType{};
    const GlassType* configured = type(spawn.type);
    const GlassType& entry = configured != nullptr ? *configured : kNoType;
    NavLinks links(world.paths);

    // The geometry: the edges from the first corner, the centre half-way along both, the normal width × height.
    GlassPane pane;
    pane.handle = handle;
    pane.type = spawn.type;
    pane.edgeU = anim::subtract(spawn.cornerU, spawn.corner);
    pane.edgeV = anim::subtract(spawn.cornerV, spawn.corner);
    pane.centre = anim::add(spawn.corner, anim::scale(anim::add(pane.edgeU, pane.edgeV), 0.5F));
    pane.normal = anim::normalise(anim::cross(pane.edgeU, pane.edgeV));
    pane.width = anim::length(pane.edgeU);
    pane.height = anim::length(pane.edgeV);
    pane.sizeWord = static_cast<std::uint32_t>(pane.width) | (static_cast<std::uint32_t>(pane.height) << 16U);
    pane.uv0 = spawn.uv0;
    pane.uv1 = spawn.uv1;
    pane.triangles = spawn.triangles;
    pane.polygon = links.polygonAt(pane.centre);

    // Its triangles two-sided with material GLASS; its sprite and colour by type.
    markTriangles(world.collision, pane.triangles, raycast::kTriangleTwoSided, material::kGlass);
    pane.sprite = spawn.type == glass_type::kStained ? kStainedSprite : (entry.sprite & kSpriteRectMask);
    pane.colour = spawn.type == glass_type::kInvisible ? 0U : kPaneColour;
    if (entry.alarm) {
        pane.alarmBits = kAlarmBits;
    }
    // Types 17 and 18 start broken with their triangles off; 18 also opens its polygon.
    if (spawn.type == glass_type::kPreBroken || spawn.type == glass_type::kPreBrokenGap) {
        pane.broken = true;
        setTrianglesEnabled(world.collision, pane.triangles, false);
        if (spawn.type == glass_type::kPreBrokenGap && pane.polygon) {
            links.setPolygonExcluded(*pane.polygon, true);
        }
    }
    // The window link: the nearest choke link is avoided and, in a polygon, it and its reverse are charged through.
    if (entry.windowLink) {
        if (const std::optional<FoundLink> found = links.findNearest(pane.centre, link_kind::kChoke)) {
            pane.link = found->link;
            links.setAvoid(found->link, true);
            if (pane.polygon) {
                links.setKind(found->link, link_kind::kBreakable);
                if (found->back) {
                    links.setKind(*found->back, link_kind::kBreakable);
                }
            }
        }
    }
    m_panes.push_back(pane);
    return m_panes.back();
}

bool GlassPanes::hit(double handle, ObjectWorld& world) {
    GlassPane* pane = findMutable(handle);
    if (pane == nullptr || pane->broken) {
        return false;
    }
    shatter(*pane, true, world);
    if (pane->type == glass_type::kCarWindow && world.services != nullptr) {
        world.services->freeCarStereos(pane->centre, kCarStereoReach);
    }
    return true;
}

void GlassPanes::shatterOnly(double handle, ObjectWorld& world) {
    if (GlassPane* pane = findMutable(handle); pane != nullptr && !pane->broken) {
        shatter(*pane, false, world);
    }
}

void GlassPanes::shatter(GlassPane& pane, bool markBroken, ObjectWorld& world) {
    if (markBroken) {
        // The broken sprite, or hidden when the type has none.
        const GlassType* entry = type(pane.type);
        const std::uint32_t broken = entry != nullptr ? entry->brokenSprite : 0U;
        if (broken == 0) {
            pane.hidden = true;
        } else {
            pane.sprite = broken & kSpriteRectMask;
        }
    }
    shatterEffect(pane, world);
    setTrianglesEnabled(world.collision, pane.triangles, false);
    if (markBroken) {
        // White and faint: no longer whole, the pane never gets its body back.
        pane.colour = kBrokenPaneColour;
        if (world.services != nullptr) {
            world.services->setBody(pane.handle, false);
        }
        pane.broken = true;
    }
}

void GlassPanes::shatterEffect(const GlassPane& pane, ObjectWorld& world) {
    if (world.services == nullptr) {
        return;
    }
    const ShatterPlan plan = shatterPlan(pane.sizeWord);
    world.services->playMaterialPair(plan.soundMaterial, plan.soundMaterial, pane.centre);
    if (!world.services->shardsWanted(pane.centre)) {
        return;
    }
    // Each shard: two tries at 2 in 3, each at a random point within the spread of the half-width and half-height.
    const anim::Vec3 halfU = anim::scale(pane.edgeU, 0.5F * kShardSpread);
    const anim::Vec3 halfV = anim::scale(pane.edgeV, 0.5F * kShardSpread);
    for (int shard = 0; shard < plan.count; ++shard) {
        for (int attempt = 0; attempt < kShardTries; ++attempt) {
            if (randomBelow(world.random, kShardOdds) >= kShardTaken) {
                continue;
            }
            const float u = randomOffset(world.random);
            const float v = randomOffset(world.random);
            const anim::Vec3 at = anim::add(pane.centre, anim::add(anim::scale(halfU, u), anim::scale(halfV, v)));
            world.services->spawnShard(at, plan.shardSize, pane.colour);
        }
    }
}

void GlassPanes::breakBy(double handle, double breaker, ObjectWorld& world) {
    const GlassPane* pane = find(handle);
    if (pane == nullptr) {
        return;
    }
    const GlassType* entry = type(pane->type);
    // The alarm: the crime scene moves here and a break-in is reported.
    if ((pane->alarmBits & kAlarmBits) == kAlarmBits && world.services != nullptr) {
        world.services->moveCrimeSceneFlag(pane->centre);
        world.services->reportCrime(kCrimeBreakIn, pane->centre, breaker);
    }
    // The window link: in a polygon, the polygon opens and its charged link is no longer avoided; else the choke link.
    if (entry != nullptr && entry->windowLink && pane->link) {
        NavLinks links(world.paths);
        if (pane->polygon) {
            links.setPolygonExcluded(*pane->polygon, true);
        }
        links.setAvoid(*pane->link, false);
    }
    // Nobody stands looking through a broken window.
    if (world.services != nullptr) {
        world.services->disableFlagsNear(std::max(pane->width, pane->height), pane->centre, kActivityWindowLook);
    }
}

bool GlassPanes::humanHit(double handle, double attacker, ObjectWorld& world) {
    if (!hit(handle, world)) {
        return false;
    }
    breakBy(handle, attacker, world);
    if (world.services != nullptr) {
        world.services->countPaneBroken(attacker);
    }
    return true;
}

bool GlassPanes::thrownHit(double handle, double thrower, ObjectWorld& world) {
    return humanHit(handle, thrower, world);
}

std::size_t GlassPanes::breakInRadius(anim::Vec3 centre, float radius, ObjectWorld& world) {
    std::size_t broken = 0;
    for (const GlassPane& pane : m_panes) {
        if (anim::distance(pane.centre, centre) <= radius && hit(pane.handle, world)) {
            ++broken;
        }
    }
    if (world.services != nullptr) {
        world.services->breakGlassObjects(centre, radius);
    }
    return broken;
}

const GlassPane* GlassPanes::find(double handle) const {
    const auto found = std::ranges::find(m_panes, handle, &GlassPane::handle);
    return found == m_panes.end() ? nullptr : &*found;
}

GlassPane* GlassPanes::findMutable(double handle) {
    const auto found = std::ranges::find(m_panes, handle, &GlassPane::handle);
    return found == m_panes.end() ? nullptr : &*found;
}

const GlassPane* GlassPanes::findByTriangle(std::uint32_t triangle) const {
    const auto found = std::ranges::find_if(m_panes, [triangle](const GlassPane& pane) {
        return pane.triangles[0] == triangle || pane.triangles[1] == triangle;
    });
    return found == m_panes.end() ? nullptr : &*found;
}

std::vector<double> GlassPanes::bodiesTouching(anim::Vec3 centre, float radius) const {
    std::vector<double> touched;
    for (const GlassPane& pane : m_panes) {
        // Only a pane still whole and shown has its body.
        if (pane.colour != kPaneColour || pane.hidden || pane.width <= 0.0F || pane.height <= 0.0F) {
            continue;
        }
        // The sphere's centre in the box's frame, each axis clamped to the box: the nearest point of the box.
        const anim::Vec3 offset = anim::subtract(centre, pane.centre);
        const anim::Vec3 across = anim::scale(pane.edgeU, 1.0F / pane.width);
        const anim::Vec3 up = anim::scale(pane.edgeV, 1.0F / pane.height);
        const float u = anim::dot(offset, across);
        const float v = anim::dot(offset, up);
        const float n = anim::dot(offset, pane.normal);
        const float du = u - std::clamp(u, -pane.width / 2.0F, pane.width / 2.0F);
        const float dv = v - std::clamp(v, -pane.height / 2.0F, pane.height / 2.0F);
        const float dn = n - std::clamp(n, -kPaneBodyDepth, kPaneBodyDepth);
        if ((du * du) + (dv * dv) + (dn * dn) <= radius * radius) {
            touched.push_back(pane.handle);
        }
    }
    return touched;
}

std::vector<GlassQuad> glassDraws(const GlassPanes& panes, anim::Vec3 camera) {
    std::vector<GlassQuad> far;
    std::vector<GlassQuad> near;
    for (const GlassPane& pane : panes.panes()) {
        // Only a whole pane within reach of a camera has a body, and only a pane with a body is drawn.
        const anim::Vec3 offset = anim::subtract(pane.centre, camera);
        const float distanceSq = anim::dot(offset, offset);
        if (pane.colour != kPaneColour || pane.type == glass_type::kStained ||
            distanceSq >= kPaneBodyReach * kPaneBodyReach) {
            continue;
        }
        const anim::Vec3 halfU = anim::scale(pane.edgeU, 0.5F);
        const anim::Vec3 halfV = anim::scale(pane.edgeV, 0.5F);
        const anim::Vec3 first = anim::subtract(anim::subtract(pane.centre, halfU), halfV);
        GlassQuad quad;
        quad.handle = pane.handle;
        quad.corners = {first, anim::add(first, pane.edgeU), anim::add(first, pane.edgeV),
                        anim::add(anim::add(first, pane.edgeU), pane.edgeV)};
        quad.rect = static_cast<std::uint16_t>(pane.sprite & kSpriteRectMask);
        quad.colour = pane.colour;
        // The near panes go on their own list, drawn after the batches.
        (distanceSq < kNearPaneReach * kNearPaneReach ? near : far).push_back(quad);
    }
    far.insert(far.end(), near.begin(), near.end());
    return far;
}

} // namespace coney::world_objects
