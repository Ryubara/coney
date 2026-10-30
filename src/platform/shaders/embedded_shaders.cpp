// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/shaders/embedded_shaders.h"

#include <algorithm>

namespace coney::shaders {
namespace {

// kShaders, written by cmake/coney_shader_embed.cmake into the build folder, in the order of the file names.
#include "embedded_shader_data.inc"

} // namespace

std::span<const Shader> allShaders() { return kShaders; }

const Shader* findShader(std::string_view name) {
    const auto found = std::ranges::find(kShaders, name, &Shader::name);
    return found == std::end(kShaders) ? nullptr : &*found;
}

} // namespace coney::shaders
