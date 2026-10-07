// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenes/scene_player.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <utility>

#include "animation/anim_task.h"
#include "core/assert.h"

namespace coney::scenes {

namespace {

// One scene update, seconds.
constexpr float kUpdateSeconds = 1.0F / kSceneFrameRate;
// A part ends when less than this is left of it, seconds: the clip task's 0.1 ms, so 30 additions of 1/30 s that
// fall a hair short of a whole second still end on the update that reaches it.
constexpr float kEndEpsilon = 1e-4F;
// The scale of a clip key's position components (formats/animation.md#keyframes-chunk-0x00).
constexpr float kKeyXy = 1.0F / 1023.0F;
constexpr float kKeyZ = 1.0F / 2047.0F;
constexpr float kKeyRotation = 1.0F / 32768.0F;

// The track event types the runner acts on (docs/research/scenes.md#events).
constexpr std::uint16_t kEventSoundtrack = 13;
constexpr std::uint16_t kEventSound = 14;
constexpr std::uint16_t kEventSoundAlt = 71;
constexpr std::uint16_t kEventMessage12 = 24;
constexpr std::uint16_t kEventMessage13 = 25;
constexpr std::uint16_t kEventLens = 26;
constexpr std::uint16_t kEventFadeOut = 27;
constexpr std::uint16_t kEventFadeIn = 28;
constexpr std::uint16_t kEventLoopPoint = 29;
constexpr std::uint16_t kEventLight = 30;
constexpr std::uint16_t kEventCallEnd = 31;
constexpr std::uint16_t kEventParticle = 33;
constexpr std::uint16_t kEventCaption = 41;
constexpr std::uint16_t kEventColouredFade = 74;
constexpr std::uint16_t kEventRumble = 76;
// The role clip events that put a human at its marks: a position and a heading.
constexpr std::uint16_t kEventMarkPosition = 21;
constexpr std::uint16_t kEventMarkHeading = 22;
// The caption command that clears it.
constexpr int kCaptionClear = 4;

// `a * b`: the rotation that applies `b`, then `a`.
anim::Quat multiply(anim::Quat a, anim::Quat b) {
    return anim::Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// `v` rotated by `q`.
anim::Vec3 rotate(anim::Quat q, anim::Vec3 v) { return anim::transformDirection(anim::matrixFromQuat(q), v); }

// A pose of the scene's space in the world: scene rotation × position + scene position, and the rotations composed
// (0x00356188).
ScenePose toWorld(const ScenePose& place, const ScenePose& local) {
    return ScenePose{.position = anim::add(rotate(place.rotation, local.position), place.position),
                     .rotation = anim::normalise(multiply(place.rotation, local.rotation))};
}

// Whether an event at `frame` fires on an update that takes the event frame from `before` (exclusive) to `after`.
bool due(std::uint16_t frame, int before, int after) {
    return static_cast<int>(frame) > before && static_cast<int>(frame) <= after;
}

// The strength of a rumble event: sqrt(value × 0.01) × 180 + 75, at most 255.
int rumbleStrength(std::uint16_t value) {
    const float strength = (std::sqrt(static_cast<float>(value) * 0.01F) * 180.0F) + 75.0F;
    return static_cast<int>(std::min(strength, 255.0F));
}

// The strength a skip's flush gives a rumble event: value × 25.5, at most 255.
int flushedRumbleStrength(std::uint16_t value) {
    return static_cast<int>(std::min(static_cast<float>(value) * 25.5F, 255.0F));
}

} // namespace

/// One playing scene: the task the play bindings make (`SceneTask`, 30 updates a second), its runners and its start
/// and end sequences. It lives in the SceneSystem until its scene has ended.
class SceneTask {
  public:
    // A task for the scene in `slot` as `request` asks, made at `nowMs`.
    SceneTask(SceneSystem& system, SceneSlot& slot, PlayRequest request, std::uint64_t nowMs)
        : m_system(system), m_slot(slot), m_request(std::move(request)), m_createdMs(nowMs) {
        m_slot.state = SceneState::Starting;
        // The delay counts in units of 60 updates (inferred: 2 s each).
        m_delayUpdates = static_cast<std::uint64_t>(m_request.delay) * 60;
        if (const std::optional<SceneCameraDef>& cameraDef = m_slot.header->camera) {
            const SceneCameraDef& camera = *cameraDef;
            m_lens =
                SceneLens{.fieldOfView = camera.fieldOfView, .nearClip = camera.nearClip, .farClip = camera.farClip};
        }
    }

    // One update (vtable +0x13c, every 2 ticks).
    // @orig 0x0039cbf0 SceneTask_Update (SceneTask.cpp)
    void update(std::uint64_t nowMs, std::uint16_t buttons) {
        ++m_updates;
        switch (m_slot.state) {
        case SceneState::Starting:
            if (nowMs - m_createdMs >= kStartTimeoutMs) {
                abort();
                return;
            }
            start(nowMs);
            return;
        case SceneState::Playing:
            if (skipPressed(buttons)) {
                skip(buttons);
                return;
            }
            advance();
            return;
        case SceneState::Stopping:
            m_slot.state = SceneState::Ending;
            end();
            return;
        case SceneState::Ending:
            end();
            return;
        default:
            return;
        }
    }

    // `SceneStop(id, force)`.
    // @orig 0x00353a10 Scene_Stop (SceneCache.cpp)
    void stop(bool force) {
        if (m_slot.state == SceneState::Starting) {
            m_slot.state = SceneState::Ending;
            return;
        }
        if (m_slot.state != SceneState::Playing) {
            return;
        }
        // Only a scene with the loop-point flag finishes its pass; any other, looping or not, ends at once, mid-pass
        // (so the front end's Wonder Wheel stops when an attract movie starts and is loaded afresh after it).
        if (m_loopPoint && !force) {
            // @orig 0x003a0be8 SceneTask_StopLooping (SceneTask.cpp)
            m_request.looping = false;
            return;
        }
        endClips();
        m_slot.state = SceneState::Ending;
    }

    // Whether the scene is over and the task can go: state 8 with no humans left.
    [[nodiscard]] bool finished() const { return m_slot.state == SceneState::Ended && m_humansIn == 0; }
    [[nodiscard]] std::uint32_t id() const { return m_slot.id; }
    [[nodiscard]] SceneState state() const { return m_slot.state; }
    [[nodiscard]] bool cinematic() const { return m_request.cinematic; }
    [[nodiscard]] const std::vector<double>& roles() const { return m_slot.roleHandles; }
    // The frame the roles (or else the camera or the first runner) have reached, from the scene's start.
    [[nodiscard]] float frame() const {
        for (const RoleRun& role : m_roles) {
            if (role.handle != 0.0) {
                return role.frameOffset + (role.time * kSceneFrameRate);
            }
        }
        return m_tracks.empty() ? 0.0F : m_tracks.front().partStartFrame + (m_tracks.front().time * kSceneFrameRate);
    }

  private:
    // What a track runner moves.
    enum class Target : std::uint8_t { Object, Camera, Light };

    // A role's runner: its human, its clip's place in the parts and where the scene has put it.
    struct RoleRun {
        double handle = 0.0;
        bool inScene = false;
        std::size_t part = 0;
        float time = 0.0F;        // into the part's clip, seconds
        float frameOffset = 0.0F; // the frames of the parts before this one
        int eventFrame = -1;      // the last scene frame whose events have fired
        anim::Vec3 feet;
        float heading = 0.0F;
    };

    // An object's, the camera's or a light's runner (0x90 bytes in the original).
    struct TrackRun {
        Target target = Target::Object;
        std::size_t index = 0; // the object or light
        std::size_t part = 0;
        float time = 0.0F;
        float partStartFrame = 0.0F;
        int eventFrame = -1; // the last frame of this part whose events have fired
        bool done = false;
        SceneLight light{};
    };

    // ---- Starting ----

    // Re-entered every update until everything is ready; returns at the first thing that is not.
    // @orig 0x0039d870 SceneTask_Start (SceneTask.cpp)
    void start(std::uint64_t /*nowMs*/) {
        SceneHost& host = m_system.host();
        const SceneHeader& header = *m_slot.header;
        // 2. A scene with a camera ends the blur pulse on every view.
        if (!m_blurEnded && header.camera) {
            host.screenEffect(ScreenEffect::EndBlurPulse, 0.0F);
            m_blurEnded = true;
        }
        // 3. Every bound human must be ready.
        for (const double human : m_slot.roleHandles) {
            if (human != 0.0 && !host.humanReady(human)) {
                return;
            }
        }
        // 4. A cinematic steps the scene state, one step an update: 0 -> 1, then 1 -> 2 with the world preloaded round
        // the scene camera's start.
        if (m_request.cinematic && m_system.m_sceneState < 2) {
            if (m_system.m_sceneState == 0) {
                m_system.m_sceneState = 1;
                return;
            }
            m_system.m_sceneState = 2;
            if (header.camera) {
                host.preloadWorld(toWorld(m_request.place, header.camera->role.start).position,
                                  header.camera->preloadRadius, header.name);
            }
            return;
        }
        // 5. The bound objects must be loaded.
        for (const double object : m_slot.objectHandles) {
            if (object != 0.0 && !host.objectReady(object)) {
                return;
            }
        }
        // 6. A cinematic: the soundtrack ready (its stream buffered, or its pending preload run), state 3, one more
        // update, then the pads' skip flags cleared and, unless chained, the chain skip.
        if (m_request.cinematic && !m_waited && !host.soundtrackReady()) {
            return;
        }
        if (m_request.cinematic && !m_waited) {
            m_system.m_sceneState = 3;
            m_waited = true;
            return;
        }
        if (m_request.cinematic && !m_request.chain && !m_chainCleared) {
            m_system.m_chainSkip = false;
            m_chainCleared = true;
        }
        // 7. While the delay lasts, the humans walk to their marks; the start waits until all are there.
        if (m_delayUpdates > 0) {
            bool allThere = true;
            for (std::size_t role = 0; role < m_slot.roleHandles.size(); ++role) {
                const double human = m_slot.roleHandles[role];
                if (human != 0.0 && !host.humanAtMark(human, toWorld(m_request.place, header.roles[role].start))) {
                    allThere = false;
                }
            }
            if (!allThere) {
                --m_delayUpdates;
                return;
            }
        }
        // 8. The caption system is ready at once (Coney's: its text is not known).
        begin();
    }

    // 9. The start itself: the global callback, the camera, the runners, the humans, the letterbox, the brains.
    void begin() {
        SceneHost& host = m_system.host();
        const SceneHeader& header = *m_slot.header;
        if (!m_system.m_globalCallback.empty()) {
            m_system.callLua(m_system.m_globalCallback, {});
        }
        if (header.camera) {
            host.cameraBegin(toWorld(m_request.place, header.camera->role.start), m_lens);
            m_cameraBegun = true;
            m_tracks.push_back(TrackRun{.target = Target::Camera});
        }
        for (std::size_t i = 0; i < header.objects.size(); ++i) {
            m_tracks.push_back(TrackRun{.target = Target::Object, .index = i});
        }
        for (std::size_t i = 0; i < header.lights.size(); ++i) {
            const SceneLightDef& def = header.lights[i];
            TrackRun run{.target = Target::Light, .index = i};
            run.light = SceneLight{.kind = def.kind,
                                   .colour = def.colour,
                                   .coneDegrees = def.coneDegrees,
                                   .range = def.range > 0.0F ? def.range : SceneLightDef::kDefaultLightRange};
            m_tracks.push_back(run);
        }
        // The humans, each at its role's start mark. One in a grab or a mount is skipped (not counted, not reset, no
        // clip) and the scene plays on without it; the grab goes on (docs/research/scenes.md#humans).
        m_roles.resize(m_slot.roleHandles.size());
        for (std::size_t role = 0; role < m_roles.size(); ++role) {
            RoleRun& run = m_roles[role];
            run.handle = m_slot.roleHandles[role];
            const ScenePose start = toWorld(m_request.place, header.roles[role].start);
            run.feet = start.position;
            run.heading = headingOf(start.rotation);
            if (run.handle == 0.0 || clipOf(run) == nullptr || !host.humanFree(run.handle)) {
                run.handle = 0.0;
                continue;
            }
            run.inScene = true;
            ++m_humansIn;
            host.humanEnterScene(run.handle, role);
        }
        if (m_request.cinematic) {
            host.screenEffect(ScreenEffect::LetterboxIn, m_request.chain ? 0.0F : kLetterboxSeconds);
        }
        if (m_request.freeze) {
            host.suspendBrains(true);
        }
        // The loop-point flag (+0xec): a looping scene whose runners or role clips have a loop point.
        m_loopPoint = m_request.looping && hasLoopPoint();
        m_slot.state = SceneState::Playing;
        ++m_system.m_stats.started;
        // Frame 0: every runner's first events and pose.
        for (TrackRun& run : m_tracks) {
            stepTrack(run, 0.0F);
        }
        for (std::size_t role = 0; role < m_roles.size(); ++role) {
            if (m_roles[role].inScene) {
                stepRole(role, 0.0F);
            }
        }
    }

    // Whether a runner's track or a role in the scene's clip, in the header's part, has an event 29 (loop point).
    // @orig 0x0039d870 SceneTask_Start (SceneTask.cpp)
    [[nodiscard]] bool hasLoopPoint() {
        const auto loops = [](const std::vector<SceneEvent>& events) {
            return std::ranges::any_of(events, [](const SceneEvent& event) { return event.type == kEventLoopPoint; });
        };
        for (const TrackRun& run : m_tracks) {
            if (const KeyTrack* track = trackOf(run); track != nullptr && loops(track->events)) {
                return true;
            }
        }
        for (const RoleRun& run : m_roles) {
            if (run.inScene) {
                if (const RoleClip* clip = clipOf(run); clip != nullptr && loops(clip->events)) {
                    return true;
                }
            }
        }
        return false;
    }

    // ---- Playing ----

    // Whether a skip button counts this update.
    [[nodiscard]] bool skipPressed(std::uint16_t buttons) const {
        if (!m_request.skippable || m_skipped) {
            return false; // a skipped looping scene plays out its pass
        }
        if (m_request.chain && m_system.m_chainSkip) {
            return true; // a chained scene skips at once after a START skip
        }
        return m_updates > kSkipDelayUpdates && (buttons & (kSkipCross | kSkipStart)) != 0;
    }

    // A skip: the caption cleared, the view black at once, the scene stopped with the skip flag. A looping scene with
    // a loop point is not cut short: the stop only ends its looping and it plays to the end of its pass.
    // @orig 0x0039cbf0 SceneTask_Update (SceneTask.cpp)
    void skip(std::uint16_t buttons) {
        SceneHost& host = m_system.host();
        host.caption(m_slot.header->name, kCaptionClear);
        host.screenEffect(ScreenEffect::FadeOut, 0.0F);
        if ((buttons & kSkipStart) != 0) {
            m_system.m_chainSkip = true;
        }
        m_skipped = true;
        ++m_system.m_stats.skipped;
        if (m_loopPoint) {
            m_request.looping = false;
            return;
        }
        endClips();
        m_slot.state = SceneState::Ending;
    }

    // A skipped scene's end fires what is left of a track's events, every one not yet reached in track order whatever
    // its frame, but only a reduced set: show and hide, the fades and the coloured fade at once, the end-function
    // call (once per event, so level80's intro, three callbacks, calls its end function four times) and the rumble.
    // Sounds, lens, loop points, light colours, particles, captions and actions are dropped. Returns whether a rumble
    // was set (the camera's pop sets it back to 0).
    // @orig 0x00355798 SceneTrack_Flush (SceneCache.cpp)
    bool flushTrack(TrackRun& run) {
        constexpr int kAllFrames = std::numeric_limits<int>::max();
        SceneHost& host = m_system.host();
        const std::optional<double> object = run.target == Target::Object && objectHandle(run.index) != 0.0
                                                 ? std::optional(objectHandle(run.index))
                                                 : std::nullopt;
        bool rumbled = false;
        TrackRun header = run;
        header.part = 0;
        const KeyTrack* first = trackOf(header);
        const KeyTrack* current = run.part == 0 ? nullptr : trackOf(run);
        for (const KeyTrack* track : {first, current}) {
            if (track == nullptr) {
                continue;
            }
            const std::vector<SceneEvent>& events = track->events;
            for (std::size_t i = 0; i < events.size(); ++i) {
                const SceneEvent& event = events[i];
                if (!due(event.frame, run.eventFrame, kAllFrames)) {
                    continue;
                }
                ++m_system.m_stats.events;
                switch (event.type) {
                case kEventMessage12: {
                    // A show is dropped when the track's next event is a hide.
                    const bool hiddenNext = i + 1 < events.size() && events[i + 1].type == kEventMessage13;
                    if (object && !hiddenNext) {
                        host.objectMessage(*object, 0x12);
                    }
                    break;
                }
                case kEventMessage13:
                    if (object) {
                        host.objectMessage(*object, 0x13);
                    }
                    break;
                case kEventFadeOut:
                    host.screenEffect(ScreenEffect::FadeOut, 0.0F);
                    break;
                case kEventFadeIn:
                    host.screenEffect(ScreenEffect::FadeIn, 0.0F);
                    break;
                case kEventCallEnd:
                    callEnd();
                    break;
                case kEventColouredFade: {
                    const std::uint32_t word = event.u32At(4);
                    host.colouredFade((word & 0x80000000U) != 0, word & 0xffffffU, 0.0F);
                    break;
                }
                case kEventRumble:
                    host.rumble(flushedRumbleStrength(event.u16At(6)));
                    rumbled = true;
                    break;
                default:
                    break;
                }
            }
        }
        run.eventFrame = kAllFrames;
        return rumbled;
    }

    // The runner of the object or light in slot `index`; null when it has none.
    [[nodiscard]] TrackRun* runOf(Target target, std::size_t index) {
        const auto found = std::ranges::find_if(
            m_tracks, [&](const TrackRun& run) { return run.target == target && run.index == index; });
        return found != m_tracks.end() ? &*found : nullptr;
    }

    // One update of the playing scene: the camera, object and light runners, then the roles, then the streaming.
    void advance() {
        for (TrackRun& run : m_tracks) {
            stepTrack(run, kUpdateSeconds);
        }
        for (std::size_t role = 0; role < m_roles.size(); ++role) {
            if (m_roles[role].inScene) {
                stepRole(role, kUpdateSeconds);
            }
        }
        stream();
        if (m_slot.state != SceneState::Playing) {
            return;
        }
        // With bound roles the scene ends when the last has left; without, when every track has.
        const bool anyRoles = std::ranges::any_of(m_roles, [](const RoleRun& r) { return r.handle != 0.0; });
        if (anyRoles ? m_humansIn == 0 : std::ranges::all_of(m_tracks, [](const TrackRun& r) { return r.done; })) {
            m_slot.state = SceneState::Ending;
        }
    }

    // The next segment, once every runner has moved onto the newest part (`+0xf2` ≤ `+0xf4`), unless a chain skip is
    // under way for a cinematic.
    void stream() {
        if (m_request.cinematic && m_system.m_chainSkip && m_request.chain) {
            return;
        }
        const std::size_t newest = m_slot.newestPart();
        if (!SceneCache::hasPart(m_slot, newest + 1)) {
            return;
        }
        const bool allOn =
            std::ranges::all_of(m_tracks, [newest](const TrackRun& r) { return r.done || r.part >= newest; }) &&
            std::ranges::all_of(m_roles, [newest](const RoleRun& r) { return !r.inScene || r.part >= newest; });
        if (allOn) {
            m_system.m_cache.requestNext(m_slot);
        }
    }

    // The track of `run` in its part; null when the part has none.
    [[nodiscard]] const KeyTrack* trackOf(const TrackRun& run) {
        const SceneTracks* tracks = m_system.m_cache.part(m_slot, run.part);
        if (tracks == nullptr) {
            return nullptr;
        }
        switch (run.target) {
        case Target::Camera:
            return tracks->camera ? &*tracks->camera : nullptr;
        case Target::Object:
            return run.index < tracks->objects.size() ? &tracks->objects[run.index] : nullptr;
        case Target::Light:
            return run.index < tracks->lights.size() ? &tracks->lights[run.index] : nullptr;
        }
        return nullptr;
    }

    // Advances a track runner by `seconds`, moving to the next part at the end of one (or back to the start of a
    // looping scene), fires its events and applies its pose.
    // @orig 0x003560a8 SceneTrack_Advance (SceneCache.cpp)
    // @orig 0x003a0400 SceneTask_CameraPartDone (SceneTask.cpp)
    void stepTrack(TrackRun& run, float seconds) {
        if (run.done) {
            return;
        }
        const KeyTrack* track = trackOf(run);
        if (track == nullptr) {
            run.done = true;
            return;
        }
        run.time += seconds;
        while (run.time >= track->duration - kEndEpsilon) {
            const float over = std::max(0.0F, run.time - track->duration);
            TrackRun next = run;
            ++next.part;
            next.partStartFrame += track->duration * kSceneFrameRate;
            const KeyTrack* following = SceneCache::hasPart(m_slot, next.part) ? trackOf(next) : nullptr;
            if (following != nullptr) {
                run.part = next.part;
                run.partStartFrame = next.partStartFrame;
                run.time = over;
            } else if (m_request.looping) {
                // Back to the header's part, from the loop point, the segment chain restarted (slot flag 0x2000).
                run.part = 0;
                run.partStartFrame = 0.0F;
                run.time = over + (static_cast<float>(m_loopFrame) / kSceneFrameRate);
                run.eventFrame = static_cast<int>(m_loopFrame) - 1;
                if (m_slot.newestPart() > 1) {
                    m_system.m_cache.restartChain(m_slot);
                }
                following = trackOf(run);
            } else {
                run.time = track->duration;
                run.done = true;
                fireTrackEvents(run);
                applyTrack(run, *track);
                return;
            }
            if (following == nullptr) {
                run.done = true;
                return;
            }
            track = following;
        }
        fireTrackEvents(run);
        applyTrack(run, *track);
    }

    // Sets the runner's target to the track's pose at its time, in the scene's space.
    // @orig 0x00356188 SceneTrack_Apply (SceneCache.cpp)
    void applyTrack(const TrackRun& run, const KeyTrack& track) {
        SceneHost& host = m_system.host();
        const float frame = run.time * kSceneFrameRate;
        const ScenePose pose = toWorld(m_request.place, ScenePose{track.positionAt(frame), track.rotationAt(frame)});
        switch (run.target) {
        case Target::Camera:
            host.cameraPose(pose, m_lens);
            return;
        case Target::Object:
            if (const double object = objectHandle(run.index); object != 0.0) {
                host.objectPose(object, pose);
            }
            return;
        case Target::Light:
            host.lightSet(run.index, pose, run.light);
            return;
        }
    }

    // The object bound to object slot `index`; 0 for none.
    [[nodiscard]] double objectHandle(std::size_t index) const {
        return index < m_slot.objectHandles.size() ? m_slot.objectHandles[index] : 0.0;
    }

    // Fires the runner's events up to the scene frame it has reached, not fired yet. The header's track holds the
    // events of the whole scene, counted from its start (l99_c1's camera track: up to frame 1990 of 2,026, in a part
    // of 506), as the role clips do; a segment's track holds none, but its own are fired the same way.
    // @orig 0x00354d98 SceneTrack_Events (SceneCache.cpp)
    void fireTrackEvents(TrackRun& run) {
        const int upTo = anim::eventFrame((run.partStartFrame / kSceneFrameRate) + run.time);
        TrackRun header = run;
        header.part = 0;
        const KeyTrack* first = trackOf(header);
        const KeyTrack* current = run.part == 0 ? nullptr : trackOf(run);
        for (const KeyTrack* track : {first, current}) {
            if (track == nullptr) {
                continue;
            }
            for (const SceneEvent& event : track->events) {
                if (due(event.frame, run.eventFrame, upTo)) {
                    trackEvent(run, event);
                }
            }
        }
        run.eventFrame = std::max(run.eventFrame, upTo);
    }

    // One object, camera or light track event.
    void trackEvent(TrackRun& run, const SceneEvent& event) {
        SceneHost& host = m_system.host();
        ++m_system.m_stats.events;
        const std::optional<double> object = run.target == Target::Object && objectHandle(run.index) != 0.0
                                                 ? std::optional(objectHandle(run.index))
                                                 : std::nullopt;
        switch (event.type) {
        case kEventSoundtrack:
            host.soundtrackStart();
            return;
        case kEventSound:
        case kEventSoundAlt:
            host.sound(event.u32At(8), object);
            return;
        case kEventMessage12:
        case kEventMessage13:
            if (object) {
                host.objectMessage(*object, event.type == kEventMessage12 ? 0x12 : 0x13);
            }
            return;
        case kEventLens:
            m_lens =
                SceneLens{.fieldOfView = event.f32At(8), .nearClip = event.f32At(0xc), .farClip = event.f32At(0x10)};
            return;
        case kEventFadeOut:
            host.screenEffect(ScreenEffect::FadeOut, event.f32At(8));
            host.caption(m_slot.header->name, kCaptionClear);
            return;
        case kEventFadeIn:
            host.screenEffect(ScreenEffect::FadeIn, event.f32At(8));
            return;
        case kEventLoopPoint:
            m_loopFrame = event.u16At(4);
            return;
        case kEventLight:
            // **Coney's reading**: the colour as the bytes r, g, b at +8 (the cone's float sits at +0x10).
            run.light.colour = {static_cast<float>(event.u16At(8) & 0xffU) / 255.0F,
                                static_cast<float>(event.u16At(8) >> 8U) / 255.0F,
                                static_cast<float>(event.u16At(10) & 0xffU) / 255.0F};
            run.light.coneDegrees = event.f32At(0x10);
            run.light.range = event.f32At(0x14);
            return;
        case kEventCallEnd:
            callEnd();
            return;
        case kEventParticle: {
            const ScenePose local{
                .position = anim::Vec3{static_cast<float>(event.s16At(8)) * kKeyXy,
                                       static_cast<float>(event.s16At(0xa)) * kKeyXy,
                                       static_cast<float>(event.s16At(0xc)) * kKeyZ},
                .rotation = anim::normalise(anim::Quat{static_cast<float>(event.s16At(0xe)) * kKeyRotation,
                                                       static_cast<float>(event.s16At(0x10)) * kKeyRotation,
                                                       static_cast<float>(event.s16At(0x12)) * kKeyRotation, 1.0F})};
            std::string name;
            for (std::size_t i = 0x14; i < kEventBytes; ++i) {
                const auto c = static_cast<char>(event.args.at(i - 4));
                if (c == '\0') {
                    break;
                }
                name.push_back(c);
            }
            host.particle(name, toWorld(m_request.place, local));
            return;
        }
        case kEventCaption:
            host.caption(m_slot.header->name, event.u16At(4));
            return;
        case kEventColouredFade: {
            const std::uint32_t word = event.u32At(4);
            host.colouredFade((word & 0x80000000U) != 0, word & 0xffffffU, event.f32At(8));
            return;
        }
        case kEventRumble:
            host.rumble(rumbleStrength(event.u16At(6)));
            return;
        default:
            return; // 9, 10, 54, 69 and 73 do nothing in Coney (69 and 73 are not implemented)
        }
    }

    // The clip of a role's runner in its part; null when the part has none for it.
    [[nodiscard]] const RoleClip* clipOf(const RoleRun& run) {
        const std::size_t role = static_cast<std::size_t>(&run - m_roles.data());
        const SceneTracks* tracks = m_system.m_cache.part(m_slot, run.part);
        return tracks != nullptr && role < tracks->clips.size() ? &tracks->clips[role] : nullptr;
    }

    // Advances role `role`'s clip by `seconds`: at its end the same role's clip of the next part (or the start of a
    // looping scene) takes over with the overshoot, else the human leaves the scene. Then its pose, its root motion,
    // its events (which put it at its marks) and the host's human.
    // @orig 0x003a00b8 SceneTask_ClipDone (SceneTask.cpp)
    void stepRole(std::size_t role, float seconds) {
        RoleRun& run = m_roles[role];
        const RoleClip* clip = clipOf(run);
        if (clip == nullptr) {
            leave(role);
            return;
        }
        run.time += seconds;
        while (run.time >= clip->clip.duration - kEndEpsilon) {
            const float over = std::max(0.0F, run.time - clip->clip.duration);
            RoleRun next = run;
            ++next.part;
            const RoleClip* following = nullptr;
            if (SceneCache::hasPart(m_slot, next.part)) {
                const SceneTracks* tracks = m_system.m_cache.part(m_slot, next.part);
                following = tracks != nullptr && role < tracks->clips.size() ? &tracks->clips[role] : nullptr;
            }
            if (following != nullptr) {
                run.frameOffset += clip->clip.duration * kSceneFrameRate;
                run.part = next.part;
            } else if (m_request.looping) {
                fireRoleEvents(run, *clip, std::numeric_limits<int>::max());
                run.part = 0;
                run.frameOffset = 0.0F;
                run.eventFrame = -1;
                following = clipOf(run);
            } else {
                // The clip is over: its remaining events fire, then the human leaves.
                fireRoleEvents(run, *clip, std::numeric_limits<int>::max());
                run.time = clip->clip.duration;
                leave(role);
                return;
            }
            if (following == nullptr) {
                leave(role);
                return;
            }
            run.time = over;
            clip = following;
        }
        // The pose, then the root motion (turned by the heading, the turn per 1/30 s), then the marks.
        const anim::Pose pose = anim::samplePose(clip->clip, run.time, anim::referenceRotations());
        if (seconds > 0.0F) {
            const anim::RootMotion motion = anim::rootMotionOf(pose);
            const float c = std::cos(run.heading);
            const float s = std::sin(run.heading);
            const anim::Vec3 v = motion.velocity;
            run.feet = anim::add(run.feet,
                                 anim::scale(anim::Vec3{(v.x * c) - (v.y * s), (v.x * s) + (v.y * c), v.z}, seconds));
            run.heading += motion.turn * kSceneFrameRate * seconds;
        }
        fireRoleEvents(run, *clip, anim::eventFrame((run.frameOffset / kSceneFrameRate) + run.time));
        m_system.host().humanPose(run.handle,
                                  RoleFrame{.role = role, .pose = pose, .feet = run.feet, .heading = run.heading});
    }

    // Fires a role's clip events of scene frames up to `upTo` not fired yet: the header's clip holds the scene's
    // events (counted from the scene's start); a segment's clip holds none, but its own are fired the same way.
    void fireRoleEvents(RoleRun& run, const RoleClip& current, int upTo) {
        const std::size_t role = static_cast<std::size_t>(&run - m_roles.data());
        const RoleClip* first =
            role < m_slot.header->tracks.clips.size() ? &m_slot.header->tracks.clips[role] : nullptr;
        for (const RoleClip* clip : {first, first == &current ? nullptr : &current}) {
            if (clip == nullptr) {
                continue;
            }
            for (const SceneEvent& event : clip->events) {
                if (due(event.frame, run.eventFrame, upTo)) {
                    roleEvent(run, event);
                }
            }
        }
        run.eventFrame = std::max(run.eventFrame, upTo);
    }

    // One role clip event: 21 puts the human at a position, 22 turns it to a heading (both in the scene's space), 13
    // starts the scene soundtrack; the animation code's other events are not acted on.
    void roleEvent(RoleRun& run, const SceneEvent& event) {
        ++m_system.m_stats.events;
        if (event.type == kEventSoundtrack) {
            m_system.host().soundtrackStart();
        } else if (event.type == kEventMarkPosition) {
            run.feet =
                toWorld(m_request.place, ScenePose{anim::Vec3{event.f32At(8), event.f32At(0xc), event.f32At(0x10)}, {}})
                    .position;
            ++m_system.m_stats.warps;
        } else if (event.type == kEventMarkHeading) {
            run.heading = event.f32At(8) + headingOf(m_request.place.rotation);
            ++m_system.m_stats.warps;
        }
    }

    // Role `role`'s human leaves the scene: no longer driven; the count drops.
    void leave(std::size_t role) {
        RoleRun& run = m_roles[role];
        if (!run.inScene) {
            return;
        }
        run.inScene = false;
        --m_humansIn;
        m_system.host().humanExitScene(run.handle);
    }

    // Ends every role's clip, first firing its remaining events (a stop or a skip).
    // @orig 0x003a0a68 SceneTask_EndClips (SceneTask.cpp)
    void endClips() {
        for (std::size_t role = 0; role < m_roles.size(); ++role) {
            RoleRun& run = m_roles[role];
            if (!run.inScene) {
                continue;
            }
            if (const RoleClip* clip = clipOf(run); clip != nullptr) {
                fireRoleEvents(run, *clip, std::numeric_limits<int>::max());
            }
            leave(role);
        }
    }

    // ---- Ending ----

    // Calls the play binding's end function with the scene id.
    // @orig 0x003a0da8 SceneTask_CallEnd (SceneTask.cpp)
    void callEnd() {
        if (!m_request.onEnd.empty()) {
            const std::array<double, 1> args{static_cast<double>(m_slot.id)};
            m_system.callLua(m_request.onEnd, args);
        }
    }

    // State 7: everything given back, then state 8 and the end function.
    // @orig 0x0039f450 SceneTask_End (SceneTask.cpp)
    void end(bool callEndFunction = true) {
        SceneHost& host = m_system.host();
        const SceneHeader& header = *m_slot.header;
        if (m_request.cinematic) {
            m_system.m_sceneState = 0;
        }
        // The humans: a skipped scene puts them at their end poses; the join goal is popped.
        for (std::size_t role = 0; role < m_slot.roleHandles.size(); ++role) {
            const double human = m_slot.roleHandles[role];
            if (human == 0.0) {
                continue;
            }
            if (role < m_roles.size() && m_roles[role].inScene) {
                leave(role);
            }
            const std::optional<ScenePose> endPose =
                m_skipped ? std::optional(toWorld(m_request.place, header.roles[role].end)) : std::nullopt;
            host.humanRelease(human, endPose);
            m_slot.roleHandles[role] = 0.0;
        }
        // The objects (when skipped, their tracks' remaining events, then their end poses), then the lights (their
        // remaining events first when skipped).
        bool rumbled = false;
        for (std::size_t i = 0; i < m_slot.objectHandles.size(); ++i) {
            const double object = m_slot.objectHandles[i];
            if (object == 0.0) {
                continue;
            }
            if (m_skipped) {
                if (TrackRun* run = runOf(Target::Object, i); run != nullptr) {
                    rumbled = flushTrack(*run) || rumbled;
                }
                host.objectPose(object, toWorld(m_request.place, header.objects[i].end));
            }
            host.objectRelease(object);
            m_slot.objectHandles[i] = 0.0;
        }
        for (TrackRun& run : m_tracks) {
            if (run.target == Target::Light) {
                if (m_skipped) {
                    rumbled = flushTrack(run) || rumbled;
                }
                host.lightRelease(run.index);
            }
        }
        // The camera back (when skipped, its track's remaining events first; the pop stops any rumble they set), the
        // letterbox out, the brains on.
        if (m_cameraBegun) {
            if (m_skipped) {
                for (TrackRun& run : m_tracks) {
                    if (run.target == Target::Camera) {
                        rumbled = flushTrack(run) || rumbled;
                    }
                }
            }
            host.cameraEnd(m_request.blendCam);
            if (rumbled) {
                host.rumble(0);
            }
        }
        if (m_request.cinematic && begun()) {
            host.screenEffect(ScreenEffect::LetterboxOut, kLetterboxSeconds);
        }
        if (m_request.freeze && begun()) {
            host.suspendBrains(false);
        }
        // Only a skip stops the soundtrack; otherwise it plays on to its own end or the next preload.
        if (m_skipped) {
            host.soundtrackStop();
        }
        m_slot.state = SceneState::Ended;
        ++m_system.m_stats.ended;
        if (callEndFunction) {
            callEnd();
        }
        if (m_request.cinematic && !m_request.final && header.camera) {
            host.preloadWorld(toWorld(m_request.place, header.camera->role.start).position,
                              header.camera->preloadRadius, header.name + "_end");
        }
        if (m_skipped && m_request.blendCam > 0.0F) {
            host.screenEffect(ScreenEffect::FadeIn, m_request.blendCam);
        }
    }

    // Whether the scene got as far as its start (its brains were suspended, its letterbox shown).
    [[nodiscard]] bool begun() const { return !m_roles.empty() || !m_tracks.empty() || m_cameraBegun; }

    // A scene that did not start within kStartTimeoutMs is given up as the end gives everything back; whether the
    // end function is called is not traced (Coney: it is not).
    // @orig 0x0039ec60 SceneTask_Abort (SceneTask.cpp)
    void abort() {
        ++m_system.m_stats.aborted;
        m_system.host().log(
            std::format("scene {}: did not start in {} ms; given up\n", m_slot.header->name, kStartTimeoutMs));
        if (m_request.cinematic) {
            m_system.host().soundtrackStop();
        }
        end(false);
    }

    SceneSystem& m_system;
    SceneSlot& m_slot;
    PlayRequest m_request;
    std::uint64_t m_createdMs;
    std::uint64_t m_updates = 0; // +0xe0
    std::uint64_t m_delayUpdates = 0;
    bool m_blurEnded = false;
    bool m_waited = false;
    bool m_chainCleared = false;
    bool m_cameraBegun = false;
    bool m_skipped = false;
    std::uint16_t m_loopFrame = 0;
    bool m_loopPoint = false;   // +0xec: a non-forced stop only ends the looping
    std::size_t m_humansIn = 0; // +0x1e
    SceneLens m_lens;
    std::vector<RoleRun> m_roles;
    std::vector<TrackRun> m_tracks;
};

SceneSystem::SceneSystem(const SceneList& list, SceneRecordSource source, ScriptCall call)
    : m_cache(list, std::move(source)), m_call(std::move(call)) {
    // The scene soundtrack is prepared as the scene loads (0x00352098 -> 0x0010ff68).
    m_cache.setOnLoaded([this](const SceneHeader& header) {
        if (const std::optional<std::uint32_t> hash = soundtrackOf(header); hash) {
            host().soundtrackPrepare(*hash);
        }
    });
}

SceneSystem::~SceneSystem() = default;

void SceneSystem::setHost(SceneHost* host) {
    m_host = host;
    // **Coney's glue**: the humans joined before this host came (a level's start callback binds a scene's roles
    // before Coney has loaded the level) are joined to it now, at their roles' start marks.
    if (m_host == nullptr) {
        return;
    }
    for (const SceneSlot& slot : m_cache.slots()) {
        if (slot.header == nullptr) {
            continue;
        }
        for (std::size_t role = 0; role < slot.roleHandles.size() && role < slot.header->roles.size(); ++role) {
            if (const double human = slot.roleHandles[role]; human != 0.0) {
                m_host->humanJoin(human, slot.id, role, slot.header->roles[role].start, 0);
            }
        }
    }
}

void SceneSystem::queueScreenEffect(int type, float seconds) {
    switch (type) {
    case static_cast<int>(ScreenEffect::FadeIn):
    case static_cast<int>(ScreenEffect::FadeOut):
    case static_cast<int>(ScreenEffect::LetterboxIn):
    case static_cast<int>(ScreenEffect::LetterboxOut):
    case static_cast<int>(ScreenEffect::EndBlurPulse):
        host().screenEffect(static_cast<ScreenEffect>(type), seconds);
        return;
    default:
        return;
    }
}

void SceneSystem::callLua(std::string_view function, std::span<const double> args) {
    if (function.empty()) {
        return;
    }
    if (m_inUpdate) {
        m_pendingCalls.emplace_back(std::string(function), std::vector<double>(args.begin(), args.end()));
        return;
    }
    ++m_stats.callbacks;
    if (m_call) {
        m_call(function, args);
    }
}

SceneTask* SceneSystem::taskOf(std::uint32_t id) const {
    const auto found = std::ranges::find_if(m_tasks, [id](const std::unique_ptr<SceneTask>& task) {
        return task->id() == id && task->state() != SceneState::Ended;
    });
    return found == m_tasks.end() ? nullptr : found->get();
}

bool SceneSystem::inScene(double human) const {
    return std::ranges::any_of(m_tasks, [human](const std::unique_ptr<SceneTask>& task) {
        return task->state() != SceneState::Ended && std::ranges::find(task->roles(), human) != task->roles().end();
    });
}

std::uint32_t SceneSystem::preload(std::string_view name, std::string_view callback) {
    ++m_stats.preloads;
    const std::uint32_t id = m_cache.list().findContaining(name).value_or(0);
    if (m_cache.request(id, callback, m_nowMs) == nullptr) {
        host().log(std::format("scene {} ({}): no slot for it\n", name, id));
    }
    return id;
}

bool SceneSystem::isPreloaded(std::string_view name) const {
    const std::optional<std::uint32_t> id = m_cache.list().findContaining(name);
    const SceneSlot* slot = id ? m_cache.find(*id) : nullptr;
    return slot != nullptr && slot->state == SceneState::Loaded;
}

void SceneSystem::unload(std::uint32_t id) {
    if (const SceneSlot* slot = m_cache.find(id); slot != nullptr && slot->state == SceneState::Loaded) {
        m_cache.unload(id);
    }
}

bool SceneSystem::play(std::uint32_t id, const PlayRequest& request) {
    if (taskOf(id) != nullptr) {
        return false;
    }
    auto slot = m_cache.loadNow(id, m_nowMs);
    if (!slot) {
        host().log(std::format("scene {}: cannot play: {}\n", id, slot.error().message));
        return false;
    }
    if ((*slot)->state != SceneState::Loaded) {
        return false;
    }
    // A finished task of the same scene still waiting to be freed goes now.
    std::erase_if(m_tasks, [id](const std::unique_ptr<SceneTask>& task) { return task->id() == id; });
    m_tasks.push_back(std::make_unique<SceneTask>(*this, **slot, request, m_nowMs));
    return true;
}

void SceneSystem::stop(std::uint32_t id, bool force) {
    if (SceneTask* task = taskOf(id); task != nullptr) {
        task->stop(force);
    }
}

bool SceneSystem::done(std::uint32_t id) const {
    const SceneState s = state(id);
    return s != SceneState::Starting && s != SceneState::Playing;
}

float SceneSystem::length(std::uint32_t id) const {
    const SceneSlot* slot = m_cache.find(id);
    return slot != nullptr && slot->header != nullptr ? slot->header->tracks.duration() : 0.0F;
}

bool SceneSystem::joinHuman(double human, std::uint32_t id, std::size_t role, int gait) {
    SceneSlot* slot = m_cache.find(id);
    if (human == 0.0 || slot == nullptr || slot->header == nullptr || role >= slot->roleHandles.size() ||
        inScene(human)) {
        return false;
    }
    slot->roleHandles[role] = human;
    host().humanJoin(human, id, role, slot->header->roles[role].start, gait);
    return true;
}

void SceneSystem::addObject(std::uint32_t id, double object, std::size_t slotIndex) {
    SceneSlot* slot = m_cache.find(id);
    if (slot != nullptr && slotIndex < slot->objectHandles.size()) {
        slot->objectHandles[slotIndex] = object;
    }
}

void SceneSystem::update(std::uint64_t nowMs, std::uint16_t heldButtons) {
    m_nowMs = nowMs;
    m_inUpdate = true;
    // The records that arrived, and their callbacks.
    for (auto& [callback, id] : m_cache.service()) {
        const std::array<double, 1> args{static_cast<double>(id)};
        callLua(callback, args);
    }
    if (!m_cache.lastError().empty()) {
        host().log(m_cache.lastError() + "\n");
    }
    // Every task, by index: an end function may start another scene.
    // A task over since the last update is freed and its slot unloaded, so the scene loads again for its next play.
    // @orig 0x00353bf0 Scene_FreeTask (SceneCache.cpp)
    std::vector<std::uint32_t> over;
    for (std::size_t i = 0; i < m_tasks.size(); ++i) {
        SceneTask& task = *m_tasks[i];
        if (task.finished()) {
            over.push_back(task.id());
            continue;
        }
        ++m_stats.updates;
        task.update(nowMs, heldButtons);
    }
    std::erase_if(m_tasks, [&over](const std::unique_ptr<SceneTask>& task) {
        return task->finished() && std::ranges::find(over, task->id()) != over.end();
    });
    for (const std::uint32_t id : over) {
        if (const SceneSlot* slot = m_cache.find(id); slot != nullptr && slot->state == SceneState::Ended) {
            m_cache.unload(id);
        }
    }
    m_inUpdate = false;
    // The Lua calls made during the update, in order (the original makes them in place).
    std::vector<std::pair<std::string, std::vector<double>>> calls = std::move(m_pendingCalls);
    m_pendingCalls.clear();
    for (const auto& [function, args] : calls) {
        callLua(function, args);
    }
}

bool SceneSystem::playing() const {
    return std::ranges::any_of(m_tasks, [](const std::unique_ptr<SceneTask>& task) {
        return task->state() == SceneState::Starting || task->state() == SceneState::Playing;
    });
}

bool SceneSystem::cinematicActive() const { return m_sceneState != 0; }

SceneState SceneSystem::state(std::uint32_t id) const {
    const SceneSlot* slot = m_cache.find(id);
    return slot != nullptr ? slot->state : SceneState::Empty;
}

std::optional<std::uint32_t> SceneSystem::idOf(std::string_view name) const {
    return m_cache.list().findContaining(name);
}

std::optional<float> SceneSystem::frame(std::uint32_t id) const {
    const SceneTask* task = taskOf(id);
    if (task == nullptr || task->state() != SceneState::Playing) {
        return std::nullopt;
    }
    return task->frame();
}

} // namespace coney::scenes
