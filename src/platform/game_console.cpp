// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_console.h"

#include <fstream>
#include <iterator>
#include <span>
#include <utility>

#include <SDL3/SDL.h>

#include "console/catalog.h"
#include "core/log.h"
#include "net/net_status.h"

namespace coney::platform {

class GameConsole::NetworkRole final : public console::NetworkHost {
  public:
    explicit NetworkRole(const NetPlay& net) : m_net(net) {}

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

  private:
    const NetPlay& m_net;
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

GameConsole::GameConsole(const Options& options, const NetPlay& net, console::Access highest)
    : m_network(std::make_unique<NetworkRole>(net)) {
    // A test-mode run touches no user folder (docs/guides/console.md#test-mode).
    const bool testMode = isTestMode(options);
    if (!testMode) {
        if (std::optional<std::filesystem::path> path = consoleHistoryPath()) {
            m_history = std::make_unique<HistoryFile>(std::move(*path));
        }
    }
    m_console = std::make_unique<console::Console>(
        console::ConsoleSettings{.testMode = testMode, .highest = highest},
        console::ConsoleHosts{.network = m_network.get(), .history = m_history.get()});
    // The player catalog. Its handlers come with the player commands (docs/guides/studio/console.md#build-issues,
    // issue 2); until then every entry stays out of the registry and is unknown when typed.
    const auto registered = console::registerBuiltins(m_console->registry(), console::playerCatalog(), {});
    if (!registered) {
        logging::error(logging::Category::Engine, "console: " + registered.error().message);
    }
}

GameConsole::~GameConsole() = default;

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
}

} // namespace coney::platform
