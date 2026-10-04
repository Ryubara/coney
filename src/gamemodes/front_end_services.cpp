// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/front_end_services.h"

#include <format>
#include <utility>
#include <vector>

#include "scripting/lua_value.h"
#include "scripting/script_system.h"

namespace coney {

FrontEndServices::FrontEndServices(std::function<void(std::string_view)> log) : m_log(std::move(log)) {}

void FrontEndServices::playMusic(std::string_view track) {
    m_music = track;
    write(std::format("music: {} (no audio yet)\n", track));
}

void FrontEndServices::stopMusic() {
    if (!m_music.empty()) {
        write(std::format("music: {} stopped\n", m_music));
    }
    m_music.clear();
}

void FrontEndServices::playCue(int cue) {
    m_cues.push_back(cue);
    write(std::format("sound cue: {} (no audio yet)\n", cue));
}

void FrontEndServices::playMovie(std::string_view name) {
    m_movies.emplace_back(name);
    write(std::format("movie: {} skipped (no video decoder yet)\n", name));
}

void FrontEndServices::callScript(std::string_view function, std::span<const double> args) {
    m_scriptCalls.emplace_back(function);
    std::string arguments;
    for (const double arg : args) {
        arguments += arguments.empty() ? std::format("{}", arg) : std::format(", {}", arg);
    }
    if (m_scripts == nullptr || !m_scripts->exists()) {
        write(std::format("script: {}({}) skipped (no script system)\n", function, arguments));
        return;
    }
    write(std::format("script: {}({})\n", function, arguments));
    const std::vector<script::Value> values(args.begin(), args.end());
    m_scripts->call(function, values);
}

void FrontEndServices::write(const std::string& line) const {
    if (m_log) {
        m_log(line);
    }
}

} // namespace coney
