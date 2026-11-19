// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_console.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <utility>

#include <SDL3/SDL.h>

#include "console/catalog.h"
#include "console/player_commands.h"
#include "core/log.h"
#include "net/net_status.h"

namespace coney::platform {

// The game's side of the console's session calls: what the entry points linked, the settings, and the quit flag the
// frame hook reads.
class GameConsole::Session final : public console::SessionHost {
  public:
    // What the game gives now; replaces what was given before.
    void link(GameConsoleLinks links) { m_links = std::move(links); }
    [[nodiscard]] bool quitRequested() const { return m_quit; }
    // The features built, for the registry.
    [[nodiscard]] const std::vector<std::string>& builtFeatures() const { return m_links.builtFeatures; }

    [[nodiscard]] bool has(console::Need need) const override { return m_links.has && m_links.has(need); }

    [[nodiscard]] std::optional<std::vector<std::string>> values(console::ArgType type,
                                                                 std::span<const std::string> earlier) const override {
        switch (type) {
        case console::ArgType::Setting: {
            std::vector<std::string> names;
            names.reserve(m_links.settings.size());
            for (const GameSetting& setting : m_links.settings) {
                names.push_back(setting.name);
            }
            return names;
        }
        case console::ArgType::Mission: {
            if (!m_links.completedMissions) {
                return std::nullopt;
            }
            std::vector<std::string> numbers;
            for (const console::CompletedMission& mission : m_links.completedMissions()) {
                numbers.push_back(std::to_string(mission.mission));
            }
            return numbers;
        }
        case console::ArgType::Checkpoint: {
            // The checkpoints of the mission typed before it: 1 to the mission's count.
            if (!m_links.completedMissions || earlier.empty()) {
                return std::nullopt;
            }
            std::vector<std::string> numbers;
            for (const console::CompletedMission& mission : m_links.completedMissions()) {
                if (std::to_string(mission.mission) != earlier.back()) {
                    continue;
                }
                for (int checkpoint = 1; checkpoint <= mission.checkpoints; ++checkpoint) {
                    numbers.push_back(std::to_string(checkpoint));
                }
            }
            return numbers;
        }
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<console::BuildInfo> buildInfo() const override {
        if (!m_links.buildInfo) {
            return std::nullopt;
        }
        return m_links.buildInfo();
    }

    [[nodiscard]] std::optional<std::string> levelName() const override {
        if (!m_links.levelName) {
            return std::nullopt;
        }
        std::string name = m_links.levelName();
        if (name.empty()) {
            return std::nullopt;
        }
        return name;
    }

    [[nodiscard]] std::vector<console::SettingInfo> settings() const override {
        std::vector<console::SettingInfo> all;
        all.reserve(m_links.settings.size());
        for (const GameSetting& setting : m_links.settings) {
            all.push_back({setting.name, setting.get(), setting.help});
        }
        return all;
    }

    [[nodiscard]] std::expected<std::string, std::string> setSetting(std::string_view name,
                                                                     std::string_view value) override {
        const auto found = std::ranges::find(m_links.settings, name, &GameSetting::name);
        if (found == m_links.settings.end()) {
            return std::unexpected(std::format(R"(no setting named "{}")", name));
        }
        return found->set(value);
    }

    [[nodiscard]] std::string writeBugReport() override {
        return m_links.writeBugReport ? m_links.writeBugReport() : std::string();
    }

    // The frame hook ends the run at the next frame, through the exit closing the window takes.
    bool requestQuit() override {
        m_quit = true;
        return true;
    }

    [[nodiscard]] std::vector<console::CompletedMission> completedMissions() const override {
        return m_links.completedMissions ? m_links.completedMissions() : std::vector<console::CompletedMission>();
    }

    [[nodiscard]] std::expected<std::string, std::string> startMission(int mission, int checkpoint) override {
        if (!m_links.startMission) {
            return std::unexpected(std::string("this game cannot start a mission from the console yet"));
        }
        return m_links.startMission(mission, checkpoint);
    }

  private:
    GameConsoleLinks m_links;
    bool m_quit = false;
};

class GameConsole::NetworkRole final : public console::NetworkHost {
  public:
    explicit NetworkRole(NetPlay& net) : m_net(net) {}

    // A host's console changes the session; a joined game's is a guest's.
    [[nodiscard]] console::SessionRole role() const override {
        switch (m_net.status().role) {
        case net::NetRole::Host:
            return console::SessionRole::Host;
        case net::NetRole::Client:
            return console::SessionRole::Guest;
        case net::NetRole::Offline:
            break;
        }
        return console::SessionRole::Offline;
    }

    // Hosts on every interface, on `port` or the one network play has (the command line's, or its default).
    [[nodiscard]] std::expected<std::string, std::string> host(std::optional<int> port) override {
        if (port) {
            m_net.setPort(static_cast<std::uint16_t>(*port));
        }
        if (!m_net.host()) {
            return std::unexpected(lastLogLine("the game cannot be hosted"));
        }
        return std::format("hosting on port {}", m_net.status().port);
    }

    // Joins `address` (`host` or `host:port`); whether it joined shows in the Network panel a few steps later.
    [[nodiscard]] std::expected<std::string, std::string> join(std::string_view address) override {
        m_net.setAddress(std::string(address));
        if (!m_net.join()) {
            return std::unexpected(lastLogLine("the game cannot be joined"));
        }
        return std::format("connecting to {}", address);
    }

    // A guest tells the host (the Player 2 quit), the host tells every guest.
    bool leave() override {
        if (m_net.status().role == net::NetRole::Offline) {
            return false;
        }
        m_net.disconnect();
        return true;
    }

  private:
    // What network play last said, which is why a host or join failed; `fallback` when it said nothing.
    [[nodiscard]] std::string lastLogLine(std::string_view fallback) const {
        const std::vector<std::string> log = m_net.log();
        return log.empty() ? std::string(fallback) : log.back();
    }

    NetPlay& m_net;
};

class GameConsole::HistoryFile final : public console::HistoryStore {
  public:
    explicit HistoryFile(std::filesystem::path path) : m_path(std::move(path)) {}

    // The file's text, or nothing when there is none yet.
    [[nodiscard]] std::optional<std::string> load() override {
        std::ifstream file(m_path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    // Rewrites the file; a failure only costs the history, so it is a warning, once.
    void save(std::string_view text) override {
        std::ofstream file(m_path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file && !m_warned) {
            m_warned = true;
            logging::warn(logging::Category::Engine, "console: the history file cannot be written");
        }
    }

  private:
    std::filesystem::path m_path;
    bool m_warned = false;
};

std::optional<std::filesystem::path> consoleHistoryPath() {
    char* folder = SDL_GetPrefPath("Coney", "Coney");
    if (folder == nullptr) {
        return std::nullopt;
    }
    std::filesystem::path path =
        std::filesystem::path(reinterpret_cast<const char8_t*>(folder)) / "console-history.txt";
    SDL_free(folder);
    return path;
}

GameConsole::GameConsole(const Options& options, NetPlay& net, console::Access highest, GameConsoleLinks links)
    : m_session(std::make_unique<Session>()), m_network(std::make_unique<NetworkRole>(net)) {
    // A test-mode run touches no user folder (docs/guides/console.md#test-mode).
    const bool testMode = isTestMode(options);
    if (!testMode) {
        if (std::optional<std::filesystem::path> path = consoleHistoryPath()) {
            m_history = std::make_unique<HistoryFile>(std::move(*path));
        }
    }
    m_console = std::make_unique<console::Console>(
        console::ConsoleSettings{.testMode = testMode, .highest = highest},
        console::ConsoleHosts{.session = m_session.get(), .network = m_network.get(), .history = m_history.get()});
    // The player catalog's commands that are built (docs/guides/console.md#player-commands).
    const auto registered = console::registerPlayerCommands(*m_console);
    if (!registered) {
        logging::error(logging::Category::Engine, "console: " + registered.error().message);
    }
    setLinks(std::move(links));
}

GameConsole::~GameConsole() = default;

void GameConsole::setLinks(GameConsoleLinks links) {
    m_session->link(std::move(links));
    m_console->registry().setBuiltFeatures(m_session->builtFeatures());
}

bool GameConsole::quitRequested() const { return m_session->quitRequested(); }

void GameConsole::listenTo(ScriptedInput& script) {
    script.setConsoleSink([this](std::size_t port, std::string_view line) {
        (void)m_console->submit(line, console::LineSource::Script, static_cast<console::PlayerSlot>(port));
    });
}

void GameConsole::addHooks(FrameHooks& hooks) {
    hooks.beforeStep = [inner = std::move(hooks.beforeStep), this] {
        if (inner) {
            inner();
        }
        m_console->runQueued();
    };
    // `quit` ends the run at the next frame, as the window's close does.
    hooks.beginFrame = [inner = std::move(hooks.beginFrame), this] {
        if (m_session->quitRequested()) {
            return false;
        }
        return inner ? inner() : true;
    };
}

} // namespace coney::platform
