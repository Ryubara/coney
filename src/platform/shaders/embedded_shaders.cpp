// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/shaders/embedded_shaders.h"

namespace coney::shaders {
namespace {

// A stub so the shader tests compile before the embedded data is wired in: one shader with no code.
constexpr Shader kStub{"stub", Stage::Vertex, {}, {}};

} // namespace

std::span<const Shader> allShaders() { return {&kStub, 1}; }

const Shader* findShader(std::string_view) { return &kStub; }

} // namespace coney::shaders
