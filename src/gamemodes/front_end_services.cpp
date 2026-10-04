// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/front_end_services.h"

#include <format>
#include <utility>

namespace coney {

FrontEndServices::FrontEndServices(std::function<void(std::string_view)> log) : m_log(std::move(log)) {}

void FrontEndServices::playMusic(std::string_view track) {
    m_music = track;
    write(std::format("music: {} (no audio yet)\n", track));
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
    write(std::format("script: {}({}) skipped (no level scripts yet)\n", function, arguments));
}

void FrontEndServices::write(const std::string& line) const {
    if (m_log) {
        m_log(line);
    }
}

} // namespace coney
