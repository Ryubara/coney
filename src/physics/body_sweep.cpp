// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/body_sweep.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace coney::physics {

namespace {

using anim::Vec3;

Vec3 operator+(Vec3 a, Vec3 b) { return anim::add(a, b); }
Vec3 operator-(Vec3 a, Vec3 b) { return anim::subtract(a, b); }
Vec3 operator*(Vec3 a, float s) { return anim::scale(a, s); }
float dot3(Vec3 a, Vec3 b) { return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z); }

// A move this small (squared) is no move (`0x358637bd`).
constexpr float kNoMove = 1e-6F;
// The passes a sweep makes at most.
constexpr int kPasses = 3;
// The advance stops this short of the contact along the normal (`0x3c23d70a`); it is made only beyond it.
constexpr float kBackOff = 0.01F;
// Code 4 gives the other body this share of the pass's move into it (`0x3f000000`).
constexpr float kPushShare = 0.5F;
// A level triangle with this flag asks for no velocity write-back (bit 8).
constexpr std::uint16_t kTriangleNoWriteBack = 0x0100;
// The wall threshold the walking sweep passes as the minimum n.z (`0xbf266666`).
constexpr float kMinNormalZ = -0.65F;
// The landing segment's floor: a hit normal's z above this (`0x3f266666`).
constexpr float kLandingNormalZ = 0.65F;
// The contact scales (`0x3f7ae148`, `0x3e800000`).
constexpr float kLevelScale = 0.98F;
constexpr float kAirborneScale = 0.25F;

// The body along `n` to 0.01 m short of where the swept shape touched, when that is more than 0.01 m away: the codes'
// advance. Only the normal part is made; the next pass covers the rest.
void advance(BodyOwner& owner, const Contact& contact, Vec3 pass) {
    const float d = contact.fraction * dot3(pass, xyz(contact.halves[1].normal));
    if (d < -kBackOff) {
        owner.setPosition(owner.position() + (xyz(contact.halves[1].normal) * (d + kBackOff)));
    }
}

// Both velocities slid along `n` (code 1).
void slideBoth(MoveState& state, Vec3 n) {
    removeNormalPart(state.move, n);
    removeNormalPart(state.velocity, n);
}

// Whether the pair of owners are two airborne humans.
bool bothAirborneHumans(const BodyOwner& own, const BodyOwner* other) {
    return other != nullptr && (own.typeMask() & kTypeHuman) != 0 && (other->typeMask() & kTypeHuman) != 0 &&
           own.airborne() && other->airborne();
}

} // namespace

void ContactList::clear() {
    pool.clear();
    sorted.clear();
}

void MeshWorld::sweepMesh(const Box& box, Body& body, Vec3 move, ContactList& list) {
    BodyOwner& owner = *body.owner;
    // 1. The shape's mesh sweep, one contact asked for; the capsule writes them all regardless.
    m_found.clear();
    if (body.shape.type == ShapeType::Capsule) {
        const CapsuleSweep sweep{.feet = owner.position(),
                                 .radius = body.shape.radius,
                                 .move = move,
                                 .ownerAirborne = owner.airborne(),
                                 .footGap = body.shape.footGap,
                                 .minWallHeight = body.shape.minWallHeight};
        sweepCapsuleMesh(m_mesh, box, sweep, m_scratch, m_found);
    } else {
        const SphereSweep sweep{.sphere = Sphere{owner.position(), body.shape.radius},
                                .move = move,
                                .options = (body.typeMask >> 4U) & kQueryMinNormalZ,
                                .minNormalZ = kMinNormalZ,
                                .maxNormalZ = 0.0F,
                                .bodyFlags = body.flags,
                                .max = 1};
        sweepSphereMesh(m_mesh, box, sweep, m_scratch, m_found);
    }
    // 2. Only the first, the nearest, is inserted.
    if (!m_found.empty()) {
        list.pool.push_back(m_found.front());
        insertSorted(list.pool.back(), list.sorted, &owner, nullptr, false);
    }
    // 3. An airborne human's landing segment.
    if ((body.typeMask & kTypeHuman) != 0 && owner.airborne()) {
        if (std::optional<Contact> landing = owner.landing(move)) {
            landing->flags = kContactLanding;
            list.pool.push_back(*landing);
            insertSorted(list.pool.back(), list.sorted, &owner, nullptr, false);
        }
    }
}

std::uint16_t MeshWorld::triangleFlags(std::uint32_t index) const { return m_mesh.triangles()[index].flags; }

Box shapeBox(const Body& body, Vec3 position) {
    const float r = body.shape.radius;
    const Vec3 centre =
        body.shape.type == ShapeType::Capsule ? position + Vec3{0.0F, 0.0F, r + body.shape.footGap} : position;
    return Box{centre - Vec3{r, r, r}, centre + Vec3{r, r, r}};
}

std::optional<Vec3> sweepBody(float dt, SweepWorld& world, Body& body, Vec3 velocity) {
    BodyOwner& owner = *body.owner;
    // 1. The move velocity: the pending push-out over dt (and the push weight) on top of the owner's velocity.
    Vec3 push = body.pushOut * (1.0F / dt);
    const float pushWeight = owner.attribute(kAttributePushWeight);
    if ((body.flags & kBodyPushUndivided) == 0) {
        push = push * (1.0F / pushWeight);
    }
    MoveState state{.move = velocity + push, .velocity = velocity};
    // 2. The push-out is used once, whatever happens next.
    body.pushOut = Vec3{};
    // 3. Too slow to move: nothing at all happens.
    if (dot3(state.move, state.move) <= kNoMove) {
        return std::nullopt;
    }
    // 4. Set up: the box at the start, computed once and not moved between passes.
    body.flags |= kBodyDirty;
    const Vec3 start = owner.cachedPosition();
    int passes = 0;
    std::size_t found = 0;
    int last = 0;
    if (body.shape.enabled) {
        const Box box = shapeBox(body, owner.position());
        ContactList list;
        // 5. The passes, each sweeping the whole current move from where the body is. (The original also scales the
        // box by body +0x60 and drops the result; the queries get this one.)
        while (true) {
            const Vec3 pass = state.move * dt;
            if (dot3(pass, pass) <= kNoMove) {
                passes = kPasses;
                break;
            }
            Box query = box;
            extendByMove(query, pass);
            list.clear();
            world.sweepMesh(query, body, pass, list);
            if ((body.flags & kBodySweepBodies) != 0) {
                world.sweepBodies(query, body, pass, list);
            }
            last = resolveContacts(world, body, list.sorted, pass, state);
            found += list.sorted.size();
            if (last <= 0) {
                break;
            }
            if (++passes == kPasses) {
                break;
            }
        }
    }
    // 6. Aborted: whatever the contact did stays, and nothing else happens.
    if (last < 0) {
        return std::nullopt;
    }
    Vec3 position = owner.position();
    if (passes < kPasses || found == 0) {
        // Moved: the rest of the move in full.
        position = position + (state.move * dt);
        owner.setPosition(position);
        body.stuck = 0;
    } else {
        // Stuck: the body stays where the advances left it, its horizontal velocity goes, whatever the flags.
        state.velocity.x = 0.0F;
        state.velocity.y = 0.0F;
        owner.setVelocity(state.velocity);
        ++body.stuck;
    }
    if ((state.flags & kMoveScale) != 0) {
        for (Vec3* v : {&state.velocity, &state.move}) {
            v->x *= state.scale;
            v->y *= state.scale;
            if (v->z > 0.0F) {
                v->z *= state.scale;
            }
        }
    }
    if ((body.flags & kBodyWriteVelocity) != 0 && (state.flags & kMoveWriteBack) != 0) {
        owner.setVelocity(state.velocity);
    }
    owner.afterMove();
    return position - start;
}

int resolveContacts(const SweepWorld& world, Body& body, const std::vector<Contact*>& sorted, Vec3 pass,
                    MoveState& state) {
    const Vec3 direction = pass * (1.0F / std::sqrt(dot3(pass, pass)));
    for (Contact* contact : sorted) {
        // 1. Skip a triangle disabled since it was found, an owner-less body, and another owner with no body.
        if (contact->triangle && (world.triangleFlags(*contact->triangle) & raycast::kTriangleEnabled) == 0) {
            continue;
        }
        if (body.owner == nullptr) {
            continue;
        }
        if (contact->halves[1].owner != nullptr && contact->halves[1].owner->body() == nullptr) {
            continue;
        }
        BodyOwner& owner = *body.owner;
        // 2-3. The owner's answer; code 0 goes on to the next contact unless told to stop or abort.
        const std::uint32_t answer = owner.onContact(*contact);
        const std::uint32_t code = answer & kAnswerCodeMask;
        if (code == 0) {
            if ((answer & kAnswerStop) != 0) {
                return 0;
            }
            if ((answer & kAnswerAbort) != 0) {
                return -1;
            }
            continue;
        }
        // 4. Codes 1 to 4.
        const Vec3 n = xyz(contact->halves[1].normal);
        std::uint32_t act = code;
        if (act == kCodePush) {
            Body* other = contact->halves[1].owner->body();
            if ((other->flags & kBodyNotPushable) == 0) {
                Vec3 push = n * (kPushShare * dot3(pass, n));
                const float ownWeight = owner.attribute(kAttributeWeight);
                const float otherWeight = contact->halves[1].owner->attribute(kAttributeWeight);
                const float otherPushWeight = contact->halves[1].owner->attribute(kAttributePushWeight);
                push = push * (ownWeight / (otherWeight * otherPushWeight));
                other->pushOut = other->pushOut + push;
                contact->halves[1].owner->onPushed(*contact, push, pass);
            }
            act = kCodeSlide;
        }
        if (act == kCodeSlide) {
            advance(owner, *contact, pass);
            slideBoth(state, n);
        } else if (act == kCodeBounce) {
            advance(owner, *contact, pass);
            bounce(state.move, n, owner.attribute(kAttributeRestitution));
            bounce(state.velocity, n, owner.attribute(kAttributeRestitution));
        } else if (act == kCodeStop) {
            advance(owner, *contact, pass);
            state.move = Vec3{};
            state.velocity = Vec3{};
        }
        // 5. The tail: the write-back, unless a level triangle with bit 8 or two airborne humans.
        const bool quietTriangle = contact->halves[1].owner == nullptr && contact->triangle &&
                                   (world.triangleFlags(*contact->triangle) & kTriangleNoWriteBack) != 0;
        if (!quietTriangle && !bothAirborneHumans(owner, contact->halves[1].owner)) {
            state.flags |= kMoveWriteBack;
        }
        if ((contact->flags & kContactAddSlide) != 0) {
            // The corner slide joins the move velocity only, less any part against the pass; no scale rule.
            if (!owner.inPairOrBlock() && (body.flags & kBodyNoCornerSlide) == 0) {
                Vec3 slide = xyz(contact->slide);
                removeNormalPart(slide, direction);
                state.move = state.move + slide;
            }
        } else {
            if ((contact->flags & kContactStoreSlide) != 0) {
                Vec3 slide = xyz(contact->slide);
                removeNormalPart(slide, direction);
                if ((state.flags & kMoveSlideStored) == 0) {
                    state.flags |= kMoveSlideStored;
                    body.pushOut = slide;
                }
            }
            if ((answer & kAnswerScale) != 0) {
                state.flags |= kMoveScale;
                state.scale = std::max(state.scale, contactScale(body, *contact));
            }
        }
        if ((answer & kAnswerStop) != 0) {
            return 0;
        }
        return (answer & kAnswerAbort) != 0 ? -1 : 1;
    }
    return 0;
}

void backOffPlane(Vec3& point, Vec3 move, Vec3 normal, float fraction) {
    const float d = dot3(move * fraction, normal);
    if (d < -kBackOff) {
        point = point + (normal * (d + kBackOff));
    }
}

void removeNormalPart(Vec3& v, Vec3 n) {
    const float d = dot3(v, n);
    if (d < 0.0F) {
        v = v - (n * d);
    }
}

void bounce(Vec3& v, Vec3 n, float e) {
    const float d = dot3(v, n);
    if (d < 0.0F) {
        v = v - (n * d);
    }
    v = v - (n * (d * e));
}

void scaleAboutCentre(Box& box, Vec3 s) {
    if (s.x == 1.0F && s.y == 1.0F && s.z == 1.0F) {
        return;
    }
    // Per axis about the centre; min and max are re-sorted so a negative scale stays a valid box.
    const auto axis = [](float& lo, float& hi, float scale) {
        const float centre = (lo + hi) * 0.5F;
        const float half = (hi - lo) * 0.5F * scale;
        lo = std::min(centre + half, centre - half);
        hi = std::max(centre + half, centre - half);
    };
    axis(box.min.x, box.max.x, s.x);
    axis(box.min.y, box.max.y, s.y);
    axis(box.min.z, box.max.z, s.z);
}

void extendByMove(Box& box, Vec3 d) {
    const Vec3 a = box.min + d;
    const Vec3 b = box.max + d;
    box.min = Vec3{std::min({box.min.x, a.x, b.x}), std::min({box.min.y, a.y, b.y}), std::min({box.min.z, a.z, b.z})};
    box.max = Vec3{std::max({box.max.x, a.x, b.x}), std::max({box.max.y, a.y, b.y}), std::max({box.max.z, a.z, b.z})};
}

float contactScale(const Body& body, const Contact& contact) {
    if (body.scaleKind == ContactScale::Human) {
        return body.owner != nullptr && body.owner->airborne() ? kAirborneScale : 1.0F;
    }
    return contact.halves[1].shape ? 1.0F : kLevelScale;
}

std::optional<Contact> landingContact(const raycast::CollisionMesh& mesh, Vec3 feet, Vec3 upper, Vec3 move, float raise,
                                      float reach) {
    // The moved (possibly raised) feet and the upper point; the ray runs from the higher down to the lower.
    Vec3 lower = feet + Vec3{0.0F, 0.0F, raise} + move;
    Vec3 origin = upper;
    if (origin.z < lower.z) {
        std::swap(origin, lower);
    }
    const Vec3 segment = lower - origin;
    const float length = std::sqrt(dot3(segment, segment));
    if (length <= 0.0F) {
        return std::nullopt;
    }
    const Vec3 direction = segment * (1.0F / length);
    const raycast::Ray ray{.origin = raycast::Vec3{origin.x, origin.y, origin.z},
                           .direction = raycast::Vec3{direction.x, direction.y, direction.z},
                           .length = length};
    const auto hit = mesh.rayCast(ray, {}, 0);
    if (!hit || hit->normal.z <= kLandingNormalZ) {
        return std::nullopt;
    }
    // The fraction is measured on the part of the segment beyond `reach` from the origin.
    Contact contact;
    setXyz(contact.halves[1].normal, Vec3{hit->normal.x, hit->normal.y, hit->normal.z});
    contact.triangle = hit->triangle;
    contact.fraction = hit->t < reach ? 0.0F : (hit->t - reach) / (length - reach);
    return contact;
}

} // namespace coney::physics
