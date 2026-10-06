// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/cameras.h"

#include <algorithm>
#include <utility>

#include "camera/camera_lens.h"

namespace coney::camera {

namespace {

// The right stick's raw byte at rest.
constexpr std::uint8_t kStickRest = 128;
// The switches a level's camera reset turns off; the rest are on (0x00122b80).
constexpr std::array<std::size_t, 2> kSwitchesOff{1, 13};

// The follow camera's view: at its position looking at its look-at point, through its lens and the player camera's
// far clip.
CameraView followView(const FollowCamera& follow) {
    return viewLookingAt(follow.position(), follow.lookAt(), follow.fieldOfView(), follow.nearClip(),
                         kPlayerCameraLens.farClip);
}

} // namespace

Cameras::Cameras() {
    m_switches.fill(true);
    for (const std::size_t off : kSwitchesOff) {
        m_switches.at(off) = false;
    }
}

void Cameras::attachFollow(FollowCamera* follow) {
    m_follow = follow;
    if (m_follow == nullptr) {
        return;
    }
    // What the scripts set before the level's player existed: the configuration, then the reset CamSetupFollow asked.
    if (m_pendingSettings) {
        m_follow->configure(*m_pendingSettings);
        m_pendingSettings.reset();
    }
    if (m_followHandle) {
        m_follow->reset();
    }
    if (m_current.kind == CameraKind::None || m_current.kind == CameraKind::Follow) {
        m_current = CameraRef{.kind = CameraKind::Follow, .handle = 0.0};
        m_view = followView(*m_follow);
    }
}

double Cameras::setupFollow(double handle) {
    if (!m_followHandle) {
        m_followHandle = handle;
    }
    if (m_follow != nullptr) {
        m_follow->reset();
    }
    return *m_followHandle;
}

void Cameras::configureFollow(const FollowSettings& settings, float slowMotion) {
    if (m_follow != nullptr) {
        m_follow->configure(settings);
    } else {
        m_pendingSettings = settings;
    }
    m_slowMotion.setFactor(slowMotion);
}

void Cameras::createLocked(double handle, const LockedCamera& camera) { m_locked.insert_or_assign(handle, camera); }

const LockedCamera* Cameras::locked(double handle) const {
    const auto found = m_locked.find(handle);
    return found == m_locked.end() ? nullptr : &found->second;
}

std::optional<CameraRef> Cameras::find(double handle) const {
    if (m_followHandle && *m_followHandle == handle) {
        return CameraRef{.kind = CameraKind::Follow, .handle = 0.0};
    }
    if (m_locked.contains(handle)) {
        return CameraRef{.kind = CameraKind::Locked, .handle = handle};
    }
    return std::nullopt;
}

CameraView Cameras::viewOf(CameraRef ref) const {
    switch (ref.kind) {
    case CameraKind::Follow:
        return m_follow != nullptr ? followView(*m_follow) : m_view;
    case CameraKind::Locked:
        if (const LockedCamera* camera = locked(ref.handle); camera != nullptr) {
            return camera->view();
        }
        return m_view;
    case CameraKind::Scene:
        return m_sceneView;
    case CameraKind::None:
        break;
    }
    return m_view;
}

void Cameras::setCurrent(CameraRef ref) {
    m_blend.reset();
    m_current = ref;
    if (ref.kind == CameraKind::Follow && m_follow != nullptr) {
        m_follow->activate();
    }
    m_view = viewOf(ref);
}

void Cameras::switchTo(CameraRef ref, float seconds) {
    // A blend needs a camera to blend from; the source is the view shown now (a blend's own, mid-way).
    if (seconds > 0.0F && m_current.kind != CameraKind::None) {
        m_blend.emplace(m_view, seconds);
        m_current = ref;
        return;
    }
    setCurrent(ref);
}

void Cameras::makeActive(double handle, float seconds) {
    const std::optional<CameraRef> ref = find(handle);
    if (!ref) {
        return;
    }
    // A scene camera keeps the screen: the script changes what the scene returns to.
    if (m_current.kind == CameraKind::Scene) {
        if (!m_stack.empty()) {
            m_stack.back() = *ref;
        }
        return;
    }
    switchTo(*ref, seconds);
}

void Cameras::reset(double handle) {
    const std::optional<CameraRef> ref = find(handle);
    if (ref && ref->kind == CameraKind::Follow && m_follow != nullptr) {
        m_follow->reset();
    }
}

void Cameras::enable(std::size_t which, bool on) {
    if (which >= kSwitches) {
        return;
    }
    m_switches.at(which) = on;
    if (m_follow == nullptr) {
        return;
    }
    if (which == kSwitchStick) {
        m_follow->enableStick(on);
    } else if (which == kSwitchSprintZoom) {
        m_follow->enableSprintZoom(on);
    }
}

bool Cameras::target(int mode, double human) {
    switch (mode) {
    case 0:
        if (human == 0.0 || m_targets.size() >= kTargetListSize) {
            return false;
        }
        m_targets.push_back(human);
        return true;
    case 1:
        std::erase(m_targets, human);
        return true;
    case 2:
        m_targets.clear();
        return true;
    default:
        return false;
    }
}

void Cameras::setSecondary(double human, float range) {
    m_secondary = human;
    m_secondaryRange = human == 0.0 ? 0.0F : range;
}

std::optional<anim::Vec3> Cameras::secondaryPoint() const {
    if (m_secondary == 0.0 || !m_locate) {
        return std::nullopt;
    }
    return m_locate(m_secondary);
}

void Cameras::setFollowZoom(FollowZoom preset) {
    if (m_follow != nullptr) {
        m_follow->setZoom(preset);
    }
}

void Cameras::setFollowAngle(float degrees) {
    if (m_follow != nullptr) {
        m_follow->setPitch(degrees);
    }
}

void Cameras::beginScene(const CameraView& view) {
    // The camera underneath a blend is its destination.
    if (m_current.kind != CameraKind::None && m_current.kind != CameraKind::Scene) {
        m_stack.push_back(m_current);
    }
    m_sceneView = view;
    setCurrent(CameraRef{.kind = CameraKind::Scene, .handle = 0.0});
}

void Cameras::setSceneView(const CameraView& view) {
    m_sceneView = view;
    if (m_current.kind == CameraKind::Scene && !m_blend) {
        m_view = view;
    }
}

void Cameras::endScene(float blendSeconds) {
    if (m_current.kind != CameraKind::Scene) {
        return;
    }
    // Pop, reset and make current (a blend from the scene camera's last view above 0 s, else a cut).
    CameraRef back{.kind = m_follow != nullptr ? CameraKind::Follow : CameraKind::None, .handle = 0.0};
    if (!m_stack.empty()) {
        back = m_stack.back();
        m_stack.pop_back();
    }
    if (back.kind == CameraKind::Follow && m_follow != nullptr) {
        m_follow->reset();
    }
    m_view = m_sceneView;
    if (back.kind == CameraKind::None) {
        m_current = back;
        return;
    }
    switchTo(back, blendSeconds);
    // The settling update, with the last target and the stick at rest.
    if (m_lastTarget) {
        const FollowTarget target = *m_lastTarget;
        update(target, kStickRest, kStickRest, m_lastMesh, kSceneSettleSeconds);
    }
}

void Cameras::shake(int level) { m_shake.start(level, m_follow != nullptr && m_follow->combatOn()); }

void Cameras::update(const FollowTarget& target, std::uint8_t rawRightX, std::uint8_t rawRightY,
                     const raycast::CollisionMesh* mesh, float seconds) {
    m_lastTarget = target;
    m_lastMesh = mesh;
    // The follow camera runs while it is current or a blend's destination; otherwise it only follows its target.
    if (m_follow != nullptr) {
        if (m_current.kind == CameraKind::Follow) {
            FollowTarget withSecondary = target;
            withSecondary.secondary = secondaryPoint();
            withSecondary.secondaryRange = m_secondaryRange;
            m_follow->update(withSecondary, rawRightX, rawRightY, mesh, seconds);
        } else {
            m_follow->observe(target);
        }
    }
    // The blend toward the destination's live view; at the end the destination becomes current directly.
    CameraView view = viewOf(m_current);
    if (m_blend) {
        view = m_blend->step(view, seconds);
        if (m_blend->done()) {
            setCurrent(m_current);
        }
    }
    // The shake, counted down with the characters' step; its offset only while switch 6 is on.
    m_shake.update(seconds, m_slowMotion.stepSeconds());
    if (enabled(kSwitchShakeView)) {
        view.position = anim::add(view.position, m_shake.offset());
        view.lookAt = anim::add(view.lookAt, m_shake.offset());
    }
    m_view = view;
}

} // namespace coney::camera
