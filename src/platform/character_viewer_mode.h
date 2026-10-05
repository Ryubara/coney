// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_assets.h"
#include "characters/orbit_camera.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "platform/character_lights.h"
#include "platform/character_mesh.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

/// Coney's character viewer, behind `coney --view-character NAME [--anim CLIP]`
/// (docs/guides/building.md#the-character-viewer): one character from the disc, found through the Character List,
/// skinned on the CPU and playing one of its clips in a loop on the fixed timestep, seen through an orbit camera driven
/// by pad 1. The character stands at the origin in the game's axes (z up) and plays in place: the clip's root motion
/// is not applied.
///
/// Every frame: move the camera (characters::OrbitCamera); switch clips on circle or square; advance the clip by the
/// step (looping with the overshoot); sample the pose, build the bone transforms and skinning matrices, skin the
/// vertices; draw. Game time only, so with `--frames` and `--input-script` a run is the same every time (test mode).
///
/// Coney's own tool; the parts it is made of follow docs/research/characters.md and docs/research/formats/animation.md.
class CharacterViewerMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x105;
    /// **Coney's choice** for the background colour.
    static constexpr graphics::Rgba kBackground{48, 52, 64, 255};
    /// **Coney's choice** for the lights that stand in for the game's LightManager: an ambient light and one
    /// directional light from above and in front, each this bright.
    static constexpr float kAmbient = 0.4F;
    static constexpr float kDirectional = 0.8F;
    /// The anim id the viewer plays when no clip is asked for and the character has it: walk, for Rembrandt
    /// (characters.md, Character Data). **Coney's choice**; otherwise the character's first clip.
    static constexpr std::size_t kDefaultAnimId = 408;

    /// Loads the character `name` (a model name from the Character List, such as "warr_re_cv") from `wad`, its
    /// texture dictionary converted for drawing when `engine` draws, and picks `clip`: an anim id in decimal, a clip's
    /// name, or empty for the default (kDefaultAnimId, else the first clip). `print` receives what was loaded (counts
    /// only) and one line each time the clip changes. Everything given must outlive the mode. Fails with
    /// ErrorCode::NotFound for an unknown character or clip, and as the loaders do.
    [[nodiscard]] static std::expected<std::unique_ptr<CharacterViewerMode>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name, std::string_view clip,
           std::function<void(std::string_view)> print);

    ~CharacterViewerMode() override;
    CharacterViewerMode(const CharacterViewerMode&) = delete;
    CharacterViewerMode& operator=(const CharacterViewerMode&) = delete;
    CharacterViewerMode(CharacterViewerMode&&) = delete;
    CharacterViewerMode& operator=(CharacterViewerMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// One frame, as the class comment says.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// One line of counts: frames, the clip playing and how often it looped, the joint check.
    [[nodiscard]] std::string summary() const;

    [[nodiscard]] const characters::CharacterAssets& assets() const { return m_assets; }
    [[nodiscard]] const anim::AnimClip& clip() const { return *m_clips[m_clipIndex]; }
    [[nodiscard]] const anim::AnimCursor& cursor() const { return m_cursor; }
    [[nodiscard]] const characters::OrbitCamera& camera() const { return m_camera; }
    /// The skinned positions of the last frame, in the character's space.
    [[nodiscard]] const std::vector<anim::Vec3>& positions() const { return m_positions; }

  private:
    CharacterViewerMode(RenderEngine& engine, characters::CharacterAssets assets,
                        std::vector<TextureDictionary> dictionaries, std::size_t clipIndex,
                        std::function<void(std::string_view)> print);

    // Plays clip `index` of m_clips from its start and says so.
    void startClip(std::size_t index);
    // Samples the pose at the cursor and skins the vertices into m_positions and m_normals.
    void skin();
    // Draws the character through the camera into the whole window and presents the frame.
    void render();

    RenderEngine& m_engine;
    characters::CharacterAssets m_assets;
    std::vector<TextureDictionary> m_dictionaries; // before the mesh, which holds a reference to their texture
    std::vector<const anim::AnimClip*> m_clips;    // the clips in load order, for cycling
    std::size_t m_clipIndex = 0;
    anim::Skeleton m_skeleton;
    anim::AnimCursor m_cursor;
    characters::OrbitCamera m_camera;
    std::function<void(std::string_view)> m_print;
    std::vector<anim::Vec3> m_positions;
    std::vector<anim::Vec3> m_normals;
    std::unique_ptr<CharacterMesh> m_mesh;
    std::unique_ptr<CharacterLights> m_lights;
    std::uint64_t m_frames = 0;
    std::uint64_t m_loops = 0;
    float m_lastMismatch = 0.0F;
};

} // namespace coney::platform
