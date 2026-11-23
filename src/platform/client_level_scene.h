// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "characters/character_list.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "hud/hud_owner.h"
#include "human/player.h"
#include "net/character_clips.h"
#include "net/character_roster.h"
#include "net/client_world.h"
#include "net/net_id.h"
#include "platform/character_mesh.h"
#include "platform/client_game.h"
#include "platform/hud_layer.h"
#include "platform/play_lighting.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/world_renderer.h"
#include "world/sector_budget.h"

// The client game's scene of a level, drawn through the renderer: the host's level and the humans of the client's
// world. Coney's own; the original has no network play (docs/guides/client-world.md#client-game).

namespace coney::platform {

/// One character the client game draws humans with: his model and textures, his clips by anim id, ready to pose and
/// draw. What net::CharacterRoster keeps for each character a snapshot names.
/// @coney-only network play; the original has none
struct DrawnCharacter {
    std::unique_ptr<human::PlayerCharacter> character; ///< the model, its own clips and the skeleton
    std::vector<TextureDictionary> dictionaries;       ///< its textures
    std::unique_ptr<net::CharacterClips> clips;        ///< its clips, by anim id
};

/// What the client game draws of a level (ClientScene): the level's scenery as the play mode loads and streams it
/// (LevelPlayScenery), lit with the play mode's stand-in lights (PlayLighting without a level's), and each human of the
/// net::ClientWorld as the character his snapshot names (his Character List index, net::characterListIndexOf(), loaded
/// from the guest's own disc the first time a human of it is drawn) posed from its replicated layers (net::humanPose()
/// over that character's clips) at its feet and heading, as ClientWorld::drawn() blends them for the frame, with the
/// HUD the client game steps (platform::HudLayer over its hud::HudOwner) drawn over the frame. A human whose snapshot
/// names no character, or one that does not load, is drawn as Rembrandt (`human::kPlayerModel`), **Coney's choice** as
/// the play mode draws a cast human whose model fails to load. Presentation only.
///
/// It draws through the client's own camera (net::ClientCamera, handed to step() and render()): the scenery streams,
/// the lights and the visibility pass are placed by its newest view, and each frame is drawn through its view at the
/// frame's alpha. **Stand-in** while the camera has no view (the world has no human yet): the level's start, 4 m behind
/// it and 2 m above, looking at the height of a chest (**Coney's choice**).
/// @coney-only network play; the original has none
class ClientLevelScene final : public ClientScene {
  public:
    /// Loads level `level` from `wad` (LevelPlayScenery::load(), charging `budget`, which must outlive the scene, as
    /// must `wad`) and the fallback character, drawing through `engine`, and the HUD's sheets for the HUD `hud` holds
    /// (which must outlive the scene); `print` takes what was loaded (counts only). The humans' own characters load
    /// from `wad` as they are first drawn. Fails as the loaders do.
    [[nodiscard]] static std::expected<std::unique_ptr<ClientLevelScene>, Error>
    load(RenderEngine& engine, const io::Wad& wad, const LevelAt& level, world::SectorBudget& budget,
         hud::HudOwner& hud, std::function<void(std::string_view)> print);

    ~ClientLevelScene() override;
    ClientLevelScene(const ClientLevelScene&) = delete;
    ClientLevelScene& operator=(const ClientLevelScene&) = delete;
    ClientLevelScene(ClientLevelScene&&) = delete;
    ClientLevelScene& operator=(ClientLevelScene&&) = delete;

    /// Streams the scenery round the client's camera (its newest view), finds what it sees and queues the sprites of
    /// the HUD's newest step.
    void step(const net::ClientWorld& world, const net::ClientCamera& camera, const FrameTime& frame) override;
    /// Draws the scenery through the client's camera at the frame's alpha with the world's humans among it, blended for
    /// the frame, and the HUD over it, and presents.
    void render(const net::ClientWorld& world, const net::ClientCamera& camera, const RenderTime& time) override;
    /// The level's collision, which the camera's pull-in reads.
    [[nodiscard]] const raycast::CollisionMesh* collision() const override;
    /// The gait speeds of the character `character` names (human::speedsOf() over his clips as a player's are read),
    /// loading him from the disc the first time; the fallback's for a character that is not named or does not load.
    [[nodiscard]] human::Speeds speedsOf(std::int32_t character) override;

    /// How many humans the last frame posed.
    [[nodiscard]] std::size_t humansPosed() const { return m_posed; }
    /// How many of the last frame's playing layers named a clip the character does not have (left out of the pose).
    [[nodiscard]] std::size_t layersMissing() const { return m_layersMissing; }
    /// How many character meshes the scene holds, one per human it has drawn and that is still in the world.
    [[nodiscard]] std::size_t meshes() const { return m_meshes.size(); }
    /// How many sprites the HUD's newest step queued (HudLayer::spritesQueued()).
    [[nodiscard]] std::size_t hudSpritesQueued() const { return m_hudLayer->spritesQueued(); }
    /// The character the last frame drew human `id` with; null for a human it did not draw.
    [[nodiscard]] const human::PlayerCharacter* characterOf(net::NetId id) const;
    /// How many characters the scene holds: the one it falls back to and each one a human it drew was.
    [[nodiscard]] std::size_t charactersLoaded() const { return m_characters->loaded() + 1; }
    /// The view the last frame was drawn through (RenderWare's axes).
    [[nodiscard]] const WorldView& lastView() const { return m_lastView; }

  private:
    ClientLevelScene(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<LevelPlayScenery> scenery,
                     characters::CharacterList list, std::unique_ptr<DrawnCharacter> fallback,
                     std::unique_ptr<HudLayer> hudLayer, std::function<void(std::string_view)> print);

    // One human's mesh, and the character it was made for: it is made afresh when his snapshot names another.
    struct HumanMesh {
        const DrawnCharacter* character = nullptr;
        std::unique_ptr<CharacterMesh> mesh;
    };

    // The world view through the client camera's `view`, at the scenery's draw distance (at most the view's far clip);
    // the level's start (startView()) when the camera has none.
    [[nodiscard]] WorldView viewThrough(const std::optional<camera::CameraView>& view) const;
    // The stand-in view on `feet` and `heading` (game axes), 4 m behind and 2 m above, at the scenery's draw distance.
    [[nodiscard]] WorldView viewOn(anim::Vec3 feet, float heading) const;
    // The stand-in view on the level's start, for a camera with no view.
    [[nodiscard]] WorldView startView() const;

    RenderEngine& m_engine;
    const io::Wad& m_wad;
    std::unique_ptr<LevelPlayScenery> m_scenery;
    characters::CharacterList m_list; // the disc's Character List, which names the characters by index
    std::unique_ptr<net::CharacterRoster<DrawnCharacter>> m_characters; // the characters, by id
    std::unique_ptr<PlayLighting> m_lights;
    std::unique_ptr<HudLayer> m_hudLayer; // the HUD's sheets and sprites; draws the HUD the client game steps
    std::function<void(std::string_view)> m_print;
    std::map<net::NetId, HumanMesh> m_meshes; // by human
    std::size_t m_posed = 0;
    std::size_t m_layersMissing = 0;
    WorldView m_lastView;
};

} // namespace coney::platform
