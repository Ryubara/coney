// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/level_templates.h"

#include <array>
#include <cstddef>
#include <format>

namespace coney::studio {

namespace {

// Basic, then the courses assets/sandbox ships (docs/guides/sandbox.md#the-shipped-layouts).
constexpr std::array<std::string_view, 4> kTemplates = {kBasicTemplate, "default", "combat", "parkour"};

} // namespace

std::span<const std::string_view> levelTemplates() { return kTemplates; }

std::string basicLevelText() {
    // Unreal's Basic and Roblox's Baseplate in Coney's layout format: the sandbox's own light, a 60 m ground of the
    // dark grid, the spawn point at the middle facing +y and a camera view 8 m behind it, looking down a little.
    return "# A Coney Studio level: the Basic template (docs/guides/studio/editor-experience.md#new-level-coney).\n"
           "title Basic\n"
           "\n"
           "texture floor ../textures/dark_grid.png\n"
           "\n"
           "sky colour=0.62,0.72,0.84\n"
           "sun direction=-0.5,0.45,-0.74 colour=0.62,0.58,0.50\n"
           "ambient sky=0.36,0.40,0.48 ground=0.20,0.19,0.18\n"
           "fog start=80 end=220\n"
           "\n"
           "box at=0,0,-1 size=60,60,1 texture=floor\n"
           "spawn start at=0,0,0 heading=0\n"
           "view start at=0,-8,3 yaw=0 pitch=-12\n";
}

std::string rehomeTextures(std::string_view layoutText, std::string_view prefix) {
    std::string out;
    while (!layoutText.empty()) {
        const auto end = layoutText.find('\n');
        const std::string_view line = layoutText.substr(0, end);
        layoutText = end == std::string_view::npos ? std::string_view{} : layoutText.substr(end + 1);
        const bool newline = end != std::string_view::npos;
        // `texture NAME FILE [# comment]`: the keyword, two words, then whatever follows the file.
        const auto words = [&line] {
            std::array<std::string_view, 3> found{};
            std::size_t at = 0;
            for (auto& word : found) {
                at = line.find_first_not_of(" \t", at);
                if (at == std::string_view::npos) {
                    return std::pair{found, line.size()};
                }
                const auto stop = line.find_first_of(" \t", at);
                word = line.substr(at, stop == std::string_view::npos ? std::string_view::npos : stop - at);
                at = stop == std::string_view::npos ? line.size() : stop;
            }
            return std::pair{found, at};
        }();
        const auto& [word, after] = words;
        if (word[0] == "texture" && !word[2].empty() && word[2].front() != '#') {
            out += std::format("texture {} {}{}{}", word[1], prefix, word[2], line.substr(after));
        } else {
            out += line;
        }
        if (newline) {
            out += '\n';
        }
    }
    return out;
}

} // namespace coney::studio
