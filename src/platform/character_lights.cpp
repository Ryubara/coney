// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_lights.h"

#include <rw.h>

namespace coney::platform {

namespace {

// librw's vector from ours.
rw::V3d toRw(anim::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

} // namespace

CharacterLights::CharacterLights(float ambient, float directional, anim::Vec3 direction) {
    m_world = rw::World::create();
    m_ambient = rw::Light::create(rw::Light::AMBIENT);
    m_ambient->setColor(ambient, ambient, ambient);
    m_world->addLight(m_ambient);

    // A directional light shines along its frame's `at` axis; the other two axes only need to be a right-handed set.
    m_directional = rw::Light::create(rw::Light::DIRECTIONAL);
    m_directional->setColor(directional, directional, directional);
    rw::Frame* frame = rw::Frame::create();
    const anim::Vec3 at = anim::normalise(direction);
    const anim::Vec3 right = anim::normalise(anim::cross(anim::Vec3{0.0F, 0.0F, 1.0F}, at));
    rw::Matrix matrix;
    matrix.setIdentity();
    matrix.right = toRw(right);
    matrix.up = toRw(anim::cross(at, right));
    matrix.at = toRw(at);
    matrix.update();
    frame->transform(&matrix, rw::COMBINEREPLACE);
    m_directional->setFrame(frame);
    m_world->addLight(m_directional);
}

CharacterLights::~CharacterLights() {
    // Lights out of the world first, then the frame, which Light::destroy leaves alone.
    m_world->removeLight(m_directional);
    m_world->removeLight(m_ambient);
    rw::Frame* frame = m_directional->getFrame();
    m_directional->destroy();
    if (frame != nullptr) {
        frame->destroy();
    }
    m_ambient->destroy();
    m_world->destroy();
}

void CharacterLights::use() const { rw::engine->currentWorld = m_world; }

} // namespace coney::platform
