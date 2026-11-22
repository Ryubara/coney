// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/wad.h"
#include "human/player.h"
#include "net/character_clips.h"
#include "net/client_world.h"
#include "net/net_id.h"
#include "platform/character_mesh.h"
#include "platform/client_game.h"
#include "platform/play_lighting.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/world_renderer.h"
#include "world/sector_budget.h"

// The client game's scene of a level, drawn through the renderer: the host's level and the humans of the client's
// world. Coney's own; the original has no network play (docs/guides/client-world.md#client-game).

namespace coney::platform {

/// What the client game draws of a level (ClientScene): the level's scenery as the play mode loads and streams it
/// (LevelPlayScenery), lit with the play mode's stand-in lights (PlayLighting without a level's), and each human of
/// the net::ClientWorld as a character mesh posed from its replicated layers (net::humanPose() over the character's
/// clips) at its feet and heading, as ClientWorld::drawn() blends them for the frame. Presentation only.
///
/// **Stand-ins**, until the snapshots carry them: every human is drawn as Rembrandt (`human::kPlayerModel`), since a
/// snapshot does not say which character a human is; and the camera follows the human with the lowest network id (the
/// host's player 1, whose brain comes first) from 4 m behind and 2 m above his feet, looking at his chest, since the
/// guest's own player and its follow camera are not in the snapshots either. With no human, the camera stands at the
/// level's start the same way.
/// @coney-only network play; the original has none
class ClientLevelScene final : public ClientScene {
  public:
    /// Loads level `level` from `wad` (LevelPlayScenery::load(), charging `budget`, which must outlive the scene) and
    /// the stand-in character, drawing through `engine`; `print` takes what was loaded (counts only). Fails as the
    /// loaders do.
    [[nodiscard]] static std::expected<std::unique_ptr<ClientLevelScene>, Error>
    load(RenderEngine& engine, const io::Wad& wad, const LevelAt& level, world::SectorBudget& budget,
         std::function<void(std::string_view)> print);

    ~ClientLevelScene() override;
    ClientLevelScene(const ClientLevelScene&) = delete;
    ClientLevelScene& operator=(const ClientLevelScene&) = delete;
    ClientLevelScene(ClientLevelScene&&) = delete;
    ClientLevelScene& operator=(ClientLevelScene&&) = delete;

    /// Streams the scenery round the camera on the world's newest humans, and finds what it sees.
    void step(const net::ClientWorld& world, const FrameTime& frame) override;
    /// Draws the scenery with the world's humans among it, blended for the frame, and presents.
    void render(const net::ClientWorld& world, const RenderTime& time) override;

    /// How many humans the last frame posed.
    [[nodiscard]] std::size_t humansPosed() const { return m_posed; }
    /// How many of the last frame's playing layers named a clip the character does not have (left out of the pose).
    [[nodiscard]] std::size_t layersMissing() const { return m_layersMissing; }
    /// How many character meshes the scene holds, one per human it has drawn and that is still in the world.
    [[nodiscard]] std::size_t meshes() const { return m_meshes.size(); }
    /// The view the last frame was drawn through (RenderWare's axes).
    [[nodiscard]] const WorldView& lastView() const { return m_lastView; }

  private:
    ClientLevelScene(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<LevelPlayScenery> scenery,
                     std::unique_ptr<human::PlayerCharacter> character, std::vector<TextureDictionary> dictionaries,
                     std::function<void(std::string_view)> print);

    // The stand-in camera's view on `feet` and `heading` (game axes), at the scenery's draw distance.
    [[nodiscard]] WorldView viewOn(anim::Vec3 feet, float heading) const;
    // The stand-in camera's view on the level's start, for a world with no human.
    [[nodiscard]] WorldView startView() const;

    RenderEngine& m_engine;
    std::unique_ptr<LevelPlayScenery> m_scenery;
    std::unique_ptr<human::PlayerCharacter> m_character; // the stand-in model every human is drawn as
    std::vector<TextureDictionary> m_dictionaries;       // its textures
    std::unique_ptr<net::CharacterClips> m_clips;        // its clips, by anim id
    std::unique_ptr<PlayLighting> m_lights;
    std::function<void(std::string_view)> m_print;
    std::map<net::NetId, std::unique_ptr<CharacterMesh>> m_meshes; // by human
    std::size_t m_posed = 0;
    std::size_t m_layersMissing = 0;
    WorldView m_lastView;
};

} // namespace coney::platform
