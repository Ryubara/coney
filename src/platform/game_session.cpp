// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_session.h"

#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/ai_config.h"
#include "audio/game_sound.h"
#include "audio/object_sounds.h"
#include "audio/sound_engine.h"
#include "characters/character_types.h"
#include "core/assert.h"
#include "core/game_random.h"
#include "fileio/executable.h"
#include "gamemodes/legal_screen_mode.h"
#include "gamemodes/memory_card_mode.h"
#include "gui/text_layout.h"
#include "human/player.h"
#include "movies/disc_captions.h"
#include "platform/audio_output.h"
#include "platform/ffmpeg_movie_decoder.h"
#include "platform/front_end_scene.h"
#include "platform/hud_layer.h"
#include "platform/sprite_sheets.h"
#include "scenes/scene_disc.h"
#include "scripting/config_strings.h"

namespace coney::platform {

namespace {

// Player 1's start as the level scripts left him, in the play mode's terms: where a TeleportToFlag put him, else where
// HuCreate made him; nothing when the scripts made none.
std::optional<human::PlayerStart> playerStartOf(const LevelStart& start) {
    if (!start.player) {
        return std::nullopt;
    }
    const HumanCreation& player = *start.player;
    if (player.teleported) {
        const std::array<float, 3>& p = player.teleported->position;
        return human::PlayerStart{.position = anim::Vec3{p[0], p[1], p[2]},
                                  .headingDegrees = player.teleported->headingDegrees};
    }
    if (!player.position) {
        return std::nullopt;
    }
    const std::array<float, 3>& p = *player.position;
    return human::PlayerStart{.position = anim::Vec3{p[0], p[1], p[2]}, .headingDegrees = player.headingDegrees};
}

// Who player 1 is and whether his start snaps: the model his type names, no snap after a TeleportToFlag; the AI
// fighters and the character types as the scripts configured them.
PlayerSetup playerSetupOf(const LevelStart& start, const script::RecordedCalls& recorded) {
    PlayerSetup setup;
    if (start.player) {
        if (!start.player->model.empty()) {
            setup.model = start.player->model;
        }
        setup.type = start.player->type;
        setup.snapToGround = !start.player->teleported;
    }
    setup.ai = ai::aiConfigFrom(recorded);
    setup.types = characters::CharacterTypes::fromRecorded(recorded);
    return setup;
}

} // namespace

GameSession::GameSession(RenderEngine& renderer, GameModeStack& stack, const io::Wad& wad,
                         const chunk::ChunkHandlerTable& chunkHandlers, world::SectorBudget& budget,
                         gui::GlobalStrings& strings, audio::ObjectSounds& objectSounds,
                         const GameSessionSettings& settings, std::function<void(std::string_view)> log)
    : m_renderer(renderer), m_stack(stack), m_wad(wad), m_chunkHandlers(chunkHandlers), m_budget(budget),
      m_objectSounds(objectSounds), m_log(std::move(log)) {
    // The start-up flow, as the original's main pushes it (docs/research/boot.md#main). Gameplay (mode 1) loads each
    // chosen level as the play mode, with player 1 where the level script made him.
    LegalScreenSettings legal;
    legal.language = settings.language;
    m_flow = std::make_unique<StartUpFlow>(
        renderer, stack, [this](std::string_view name) { return loadSheet(name); }, strings, legal, m_log,
        script::wadScriptSource(wad),
        [this](const LevelStart& start, const ScriptedCast& cast) { return loadLevel(start, cast); }, settings.profiles,
        settings.cardCheckingMs);
    if (settings.profiles) {
        m_log(std::format("profiles: {} in {}\n", m_flow->profiles().count(), settings.profiles->string()));
    }

    // The front-end level's world behind the menus, with the dynamic objects the flow's scripts spawn (the Wonder
    // Wheel), which the scene binds and moves (docs/research/frontend.md#background).
    m_flow->levelFlow().setSceneLoader(
        [this](std::string_view level) -> std::expected<std::unique_ptr<FrontEndScene>, Error> {
            const FrontEndObjectSource objects{&m_flow->spawnRecords(), &m_flow->objectTypes()};
            auto scene = FrontEndWorldScene::create(m_renderer, m_wad, level, m_log, objects);
            if (!scene) {
                return std::unexpected(std::move(scene.error()));
            }
            return std::unique_ptr<FrontEndScene>(std::move(*scene));
        });
    m_flow->gameplay().setObjectSounds(&m_objectSounds);
    // The sheet-table records the pause menu and the mission-failed screen draw (docs/research/pause.md#layout-gui-
    // coordinates): a record's sheet is the WAD file named by its name hash.
    m_flow->setSheetRecordLoader(
        [this, hashes = sheetTableHashes(wad)](std::uint32_t record) -> std::expected<graphics::SpriteSheet, Error> {
            if (record >= hashes.size()) {
                return fail(ErrorCode::NotFound, std::format("no sheet-table record {}", record));
            }
            auto entry = m_wad.lookup(std::to_string(hashes[record]));
            if (!entry) {
                return std::unexpected(std::move(entry.error()));
            }
            return loadSpriteSheet(m_wad, **entry, m_chunkHandlers, m_renderer.drawsPixels());
        });
    // The game's random table, from the disc's own executable (docs/research/flags.md#player-starts).
    if (auto table = io::readExecutableWords(wad.disc(), GameRandom::kExecutableName, GameRandom::kTableAddress,
                                             GameRandom::kTableSize);
        table && table->size() == GameRandom::kTableSize) {
        m_flow->state().random.setTable(*table);
    }
    // Each level's scenes, and the front end's (the Wonder Wheel's `WonderWheel_100`), over the disc's scene list.
    m_flow->gameplay().setSceneMaker([this] { return makeScenes(); });
    m_flow->levelFlow().setScenes([this] { return makeScenes(); }, &m_flow->context());

    // The movie player (docs/research/movies.md#coneys-implementation): every movie the flow asks for, drawn when the
    // renderer draws pixels, its sound on the mixer once the sound is attached.
    if (renderer.drawsPixels()) {
        m_movieScreen.emplace();
    }
    movies::MovieSettings movieSettings;
    movieSettings.present = renderer.drawsPixels();
    movieSettings.skipAll = settings.skipMovies;
    movieSettings.subtitlesOn = [this] { return m_flow->state().subtitles; };
    m_movieMode.emplace(stack, renderer, discMovieOpener(wad.disc()), m_movieScreen ? &*m_movieScreen : nullptr,
                        nullptr, std::move(movieSettings), m_log);
    m_movieMode->setCaptionSource(movies::wadCaptionSource(wad, [this] { return m_flow->state().language; }));
    if (auto font = loadSheet(gui::kBigFontSheet).and_then(graphics::Font::fromSheet); font) {
        m_captionFont.emplace(std::move(*font));
        m_movieMode->setCaptionFont(&*m_captionFont);
    }
    m_flow->services().attachMoviePlayer(&*m_movieMode);
    if (settings.rumble) {
        m_flow->rumbleMenu().setLaunchOverride(settings.rumble);
    }

    // The loading screen (docs/research/level-loading.md#loading-screen). The game's sound picks the bank (load_NN,
    // from its seeded start, or armload for an Armies level) and plays it; stopping them also loads the level's bank.
    // Silent until the sound is attached.
    LoadScreenSounds loadSounds;
    loadSounds.start = [this] {
        if (m_audio != nullptr) {
            const GameState& state = m_flow->state();
            const LevelRecord* record = state.levels.at(state.currentLevel);
            m_audio->game().levelLoadStarted(record != nullptr ? static_cast<int>(record->number) : 0);
        }
    };
    loadSounds.stop = [this] {
        if (m_audio != nullptr) {
            m_audio->game().levelLoaded();
        }
    };
    m_loadingScreen.emplace(
        renderer, [this](std::string_view name) { return loadSheet(name); },
        [this](std::string_view name) { return m_wad.lookup(resourceFileName(name)).has_value(); },
        LoadScreenSettings{.language = settings.language}, std::move(loadSounds), m_log);
    m_flow->gameplay().setLoadingScreen(&*m_loadingScreen);
    // The same screen in its memory-card form for the start-up front end's load, pulsing the HUD's spinner.
    m_loadingScreen->setSpinner(&m_flow->hud().spinner());
    m_flow->levelFlow().setLoadingScreen(&*m_loadingScreen);

    // What the pause and the mission-failed screen ask of the game (docs/research/pause.md#pausing): all sound paused,
    // the HUD's objectives for the Objectives screen, both radars off.
    auto radarsBeforePause = std::make_shared<std::array<bool, 2>>(std::array<bool, 2>{true, true});
    m_flow->setPauseHooks(PauseHooks{
        .pauseSound =
            [this](bool paused) {
                if (m_audio != nullptr && paused) {
                    m_audio->sounds().pauseAll();
                } else if (m_audio != nullptr) {
                    m_audio->sounds().resumeAll();
                }
            },
        .objectives =
            [this] {
                std::array<std::vector<std::string>, 3> lists;
                const hud::Checklist& checklist = m_flow->hud().checklist();
                for (std::size_t i = 0; i < lists.size(); ++i) {
                    if (const auto& line = checklist.slots.at(i)) {
                        lists.at(i).push_back(line->text);
                    }
                }
                return lists;
            },
        // Both radars off while paused, leaving the scripts' own radar wish alone, and back as they were after.
        .radarsOff =
            [this, radarsBeforePause] {
                *radarsBeforePause = m_flow->hud().radar().on;
                m_flow->hud().radar().on = {false, false};
            },
        .radarsBack = [this, radarsBeforePause] { m_flow->hud().radar().on = *radarsBeforePause; },
    });
}

void GameSession::attachAudio(AudioOutput* audio) {
    m_audio = audio;
    if (audio == nullptr) {
        return;
    }
    // The glass panes' and doors' sounds.
    m_objectSounds.setPlayer(&audio->sounds());
    m_objectSounds.setMaterialSounds(&audio->game().materialSounds());
    // A movie's sound on the mixer; a movie stops every other sound.
    CONEY_ASSERT(m_movieMode.has_value()); // made with the session
    m_movieMode->setMixer(&audio->sounds().mixer());
    m_movieMode->setSoundStop([audio] {
        if (audio::SoundEngine* engine = audio->sounds().engine(); engine != nullptr) {
            engine->music().stop();
            engine->stopAll();
        }
    });
    // The HUD's sounds through the game-facing player (docs/research/hud.md#coneys-implementation).
    m_flow->hud().setSoundOutput([audio](std::string_view name) { audio->sounds().play(name); });
    // The front end's banks, music and cues, and the scripts' sound bindings, go to the game's sound.
    audio::GameSound& game = audio->game();
    m_flow->context().sound = &game;
    game.connect(&m_flow->scripts(), &m_flow->context());
    m_flow->services().attachAudio(&game);
}

void GameSession::startStory() { m_flow->start(); }

void GameSession::startAtLevel(std::string_view level, int checkpoint, std::function<void()> ready) {
    m_flow->startAtLevel(level, checkpoint, std::move(ready));
}

bool GameSession::jumpToLevel(std::string_view level, int checkpoint) {
    if (!m_flow->state().levels.find(level)) {
        return false;
    }
    m_pendingLevel = std::string(level);
    m_pendingCheckpoint = checkpoint;
    return true;
}

void GameSession::beginFrame() {
    if (m_pendingLevel) {
        const std::string level = std::exchange(m_pendingLevel, std::nullopt).value_or(std::string{});
        (void)m_flow->jumpToLevel(level, m_pendingCheckpoint);
    }
}

bool GameSession::inPlay() const { return m_stack.top() == &m_flow->gameplay() && m_flow->gameplay().playing(); }

PlayLevelMode* GameSession::play() { return dynamic_cast<PlayLevelMode*>(m_flow->gameplay().level()); }

std::expected<graphics::SpriteSheet, Error> GameSession::loadSheet(std::string_view name) const {
    auto sheet = loadSpriteSheetResource(m_wad, m_chunkHandlers, name, m_renderer.drawsPixels());
    // A sheet whose resource name is not known is asked for by its WAD file name (the Rumble menu's background).
    if (!sheet && sheet.error().code == ErrorCode::NotFound) {
        if (auto entry = m_wad.lookup(name); entry) {
            return loadSpriteSheet(m_wad, **entry, m_chunkHandlers, m_renderer.drawsPixels());
        }
    }
    return sheet;
}

std::expected<std::unique_ptr<GameMode>, Error> GameSession::loadLevel(const LevelStart& start,
                                                                       const ScriptedCast& cast) {
    auto mode = PlayLevelMode::create(m_renderer, m_wad, start.level, m_budget, m_log, playerStartOf(start),
                                      playerSetupOf(start, *cast.recorded), &cast);
    if (!mode) {
        return std::unexpected(std::move(mode.error()));
    }
    (*mode)->setDebugDraw(m_debugDraw);
    (*mode)->setSounds(m_audio != nullptr ? &m_audio->sounds() : nullptr);
    // The game's HUD, which the scripts' bindings act on.
    (*mode)->useHud(m_flow->hud());
    if (LevelSetup setup = std::exchange(m_firstLevelSetup, LevelSetup{}); setup) {
        if (auto done = setup(**mode); !done) {
            return std::unexpected(std::move(done.error()));
        }
    }
    return std::unique_ptr<GameMode>(std::move(*mode));
}

std::unique_ptr<scenes::SceneSystem> GameSession::makeScenes() {
    if (m_sceneListFailed) {
        return nullptr;
    }
    if (!m_sceneList) {
        auto list = scenes::loadSceneList(m_wad);
        if (!list) {
            m_sceneListFailed = true;
            m_log(std::format("scenes: {}; the scene bindings stand in\n", list.error().message));
            return nullptr;
        }
        m_sceneList = std::move(*list);
    }
    return std::make_unique<scenes::SceneSystem>(*m_sceneList, scenes::wadSceneSource(m_wad),
                                                 scenes::SceneSystem::ScriptCall{});
}

void connectDebugServices(debug::DebugServices& services, const std::function<GameSession*()>& session) {
    services.scripts = [session]() -> script::ScriptSystem* {
        GameSession* game = session();
        return game != nullptr ? &game->flow().scripts() : nullptr;
    };
    services.recorded = [session]() -> const script::RecordedCalls* {
        GameSession* game = session();
        return game != nullptr ? &game->flow().recorded() : nullptr;
    };
    services.gameState = [session]() -> GameState* {
        GameSession* game = session();
        return game != nullptr ? &game->flow().state() : nullptr;
    };
}

} // namespace coney::platform
