// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/// The GPU renderer's shaders as the executable carries them (docs/guides/shaders.md). The build compiles
/// every `*.hlsl` file of src/platform/shaders/ to SPIR-V (Vulkan), DXIL (Direct3D 12) and MSL (Metal) with
/// SDL_shadercross and embeds the results; the renderer picks the format its SDL GPU device accepts and creates the
/// shader from the bytes. Nothing here touches SDL, so the tests read it without a device.
namespace coney::shaders {

/// Which pipeline stage a shader runs in. The file name says it: `.vert.hlsl`, `.frag.hlsl`, `.comp.hlsl`.
enum class Stage : std::uint8_t { Vertex, Fragment, Compute };

/// A shader format, one per graphics API the GPU renderer reaches through SDL.
enum class Format : std::uint8_t {
    Spirv, ///< Vulkan
    Dxil,  ///< Direct3D 12
    Msl,   ///< Metal; text
};

/// How many of each resource a shader binds, as SDL's GPU API wants them when it creates the shader. The same for
/// every format; read from the SPIR-V by the compiler's reflection.
struct ResourceCounts {
    std::uint32_t samplers;
    std::uint32_t storageTextures;
    std::uint32_t storageBuffers;
    std::uint32_t uniformBuffers;
};

/// One shader, compiled for every format.
struct Shader {
    /// The file name without `.hlsl`, such as `fullscreen.vert`.
    std::string_view name;
    Stage stage;
    ResourceCounts resources;
    /// The compiled code, indexed by Format. MSL is source text, without a terminating zero.
    std::array<std::span<const std::uint8_t>, 3> code;

    /// The compiled bytes of `format`.
    [[nodiscard]] std::span<const std::uint8_t> codeFor(Format format) const {
        return code[static_cast<std::size_t>(format)];
    }
};

/// The entry point to name when creating a shader of `format`: HLSL's `main`, which SPIRV-Cross renames `main0` in MSL
/// because `main` is not a legal Metal function name.
[[nodiscard]] constexpr std::string_view entryPoint(Format format) { return format == Format::Msl ? "main0" : "main"; }

/// Every embedded shader, in the order of their names.
[[nodiscard]] std::span<const Shader> allShaders();

/// The shader named `name` (such as `blit.frag`), or null when none has that name.
[[nodiscard]] const Shader* findShader(std::string_view name);

} // namespace coney::shaders
