// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/shaders/embedded_shaders.h"
#include "support/test_paths.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace coney::shaders;

namespace {

constexpr Format kFormats[] = {Format::Spirv, Format::Dxil, Format::Msl};

// The bytes as text, for the MSL source and for searching a container for a four-character code.
std::string_view asText(std::span<const std::uint8_t> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The 32-bit little-endian word at `offset`.
std::uint32_t wordAt(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
}

// The names of the HLSL files in src/platform/shaders/, without the extension, sorted.
std::vector<std::string> shaderFilesOnDisk() {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(coney::test::shadersDir())) {
        if (entry.path().extension() == ".hlsl") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

} // namespace

// docs/guides/shaders.md: the build compiles every shader of the folder for every format, and a shader that
// does not compile fails the build, so a table that holds all of them, with code for each format, is the proof that
// each one compiled for each.
TEST_CASE("every shader file of the folder is embedded, and nothing else", "[shaders]") {
    // Expected: ../guides/shaders.md#build, Coney's own build (the folder's files are the table's)
    std::vector<std::string> embedded;
    for (const Shader& shader : allShaders()) {
        embedded.emplace_back(shader.name);
    }
    REQUIRE_FALSE(embedded.empty());
    CHECK(embedded == shaderFilesOnDisk());
}

TEST_CASE("every shader has code for every format", "[shaders]") {
    // Expected: ../guides/shaders.md#build, Coney's own build (SPIR-V, DXIL and MSL for each file)
    for (const Shader& shader : allShaders()) {
        for (const Format format : kFormats) {
            INFO(shader.name << " format " << static_cast<int>(format));
            CHECK_FALSE(shader.codeFor(format).empty());
        }
    }
}

TEST_CASE("the SPIR-V is a module the Vulkan backend can load", "[shaders]") {
    // Expected: ../guides/shaders.md#tests, the SPIR-V specification's magic number
    for (const Shader& shader : allShaders()) {
        INFO(shader.name);
        const auto code = shader.codeFor(Format::Spirv);
        // The SPIR-V specification: a stream of 32-bit words that starts with the magic number 0x07230203, followed by
        // a five-word header.
        REQUIRE(code.size() % 4 == 0);
        REQUIRE(code.size() >= 20);
        CHECK(wordAt(code, 0) == 0x07230203U);
    }
}

// DXC signs the DXIL itself since 1.8.2505, so Direct3D 12 accepts it without dxil.dll
// (docs/engine/renderer.md#licences). An unsigned container has an all-zero hash.
TEST_CASE("the DXIL is a signed container", "[shaders]") {
    // Expected: ../guides/shaders.md#compiler, DXC 1.8.2505 and later sign DXIL itself
    for (const Shader& shader : allShaders()) {
        INFO(shader.name);
        const auto code = shader.codeFor(Format::Dxil);
        REQUIRE(code.size() > 36);
        // A DXBC container: the four characters "DXBC", a 16-byte hash, then the version and the sizes.
        CHECK(asText(code).substr(0, 4) == "DXBC");
        const bool hashed = std::any_of(code.begin() + 4, code.begin() + 20, [](std::uint8_t b) { return b != 0; });
        CHECK(hashed);
        // The program part carries DXIL, not the older DXBC shader bytecode.
        CHECK(asText(code).find("DXIL") != std::string_view::npos);
    }
}

TEST_CASE("the MSL is source the Metal backend can compile", "[shaders]") {
    // Expected: ../guides/shaders.md#tests, SPIRV-Cross names the entry point main0
    for (const Shader& shader : allShaders()) {
        INFO(shader.name);
        const std::string_view text = asText(shader.codeFor(Format::Msl));
        CHECK(text.find("#include <metal_stdlib>") != std::string_view::npos);
        // SPIRV-Cross renames HLSL's `main`, which is not a legal Metal function name; the renderer creates the shader
        // with entryPoint(Format::Msl).
        const std::string_view keyword = shader.stage == Stage::Vertex     ? "vertex "
                                         : shader.stage == Stage::Fragment ? "fragment "
                                                                           : "kernel ";
        CHECK(text.find(keyword) != std::string_view::npos);
        CHECK(text.find(" " + std::string(entryPoint(Format::Msl)) + "(") != std::string_view::npos);
    }
}

TEST_CASE("a shader's resource counts come from the compiler's reflection", "[shaders]") {
    // Expected: ../guides/shaders.md#tests, blit.frag.hlsl binds one texture and sampler
    const Shader* vertex = findShader("fullscreen.vert");
    REQUIRE(vertex != nullptr);
    CHECK(vertex->stage == Stage::Vertex);
    CHECK(vertex->resources.samplers == 0);
    CHECK(vertex->resources.uniformBuffers == 0);

    // blit.frag.hlsl samples one texture through one sampler, which SDL's GPU API counts as one sampler.
    const Shader* blit = findShader("blit.frag");
    REQUIRE(blit != nullptr);
    CHECK(blit->stage == Stage::Fragment);
    CHECK(blit->resources.samplers == 1);
    CHECK(blit->resources.storageTextures == 0);
    CHECK(blit->resources.storageBuffers == 0);
    CHECK(blit->resources.uniformBuffers == 0);
}

TEST_CASE("an unknown shader is not found", "[shaders]") {
    // Expected: ../guides/shaders.md#build, Coney's own lookup
    CHECK(findShader("no_such.frag") == nullptr);
    CHECK(findShader("") == nullptr);
}
