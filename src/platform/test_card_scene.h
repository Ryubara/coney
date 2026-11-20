// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#include "graphics/asset_store.h"
#include "graphics/frame_description.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Texture;
} // namespace rw

namespace coney::platform {

class MovieTexture;
class RenderEngine;

/// The `scene` test card (`--test-card scene`, docs/guides/running-viewers.md#test-cards): a small 3D scene of Coney's
/// own meshes and textures drawn through the frame description's scene pass, so CI's software pictures compare the GL3
/// renderer's and the GPU renderer's 3D drawing (docs/guides/testing.md#software-pictures). Its meshes are librw
/// atomics in the engine's scene assets (converted for the GPU renderer, drawn as atomics by GL3), seen through one
/// camera and lit by an ambient, a directional and a point light:
///
/// - a checkered floor, lit, prelit and fogged into the distance, its texture repeated;
/// - a sign in front with holes in its texture, drawn with a first pass's alpha test (the GS test's two passes);
/// - a crate side tinted red and half transparent (an object's tint);
/// - a panel with a dual layer of an alpha ramp over its checkerboard;
/// - a wall facing away from the camera, which back-face culling drops.
///
/// A debug tool, in the debug tools' build only; Coney's own, with no counterpart in the original.
class TestCardScene {
  public:
    /// Makes the meshes and textures in `engine`'s scene assets; `engine` must outlive it.
    explicit TestCardScene(RenderEngine& engine);
    /// Takes the meshes and textures out of the scene assets and frees them.
    ~TestCardScene();
    TestCardScene(const TestCardScene&) = delete;
    TestCardScene& operator=(const TestCardScene&) = delete;
    TestCardScene(TestCardScene&&) = delete;
    TestCardScene& operator=(TestCardScene&&) = delete;

    /// Begins a frame and draws the scene into it; the caller presents it.
    void draw();

  private:
    /// One drawn quad: its atomic and dual layer (null for none), their mesh ids, and how it is drawn.
    struct Piece {
        rw::Atomic* atomic = nullptr;
        rw::Atomic* layer = nullptr;
        graphics::MeshId mesh;
        graphics::MeshId layerMesh;
        graphics::Material3d material;
        graphics::SceneStates states;
    };

    /// A texture of the card: librw's (with a raster under GL3, which draws it) and its id in the scene assets.
    struct CardTexture {
        rw::Texture* texture = nullptr;      // owned, unless `movie` holds it
        std::unique_ptr<MovieTexture> movie; // GL3's raster and texture
        graphics::TextureId id;
    };

    // Makes texture `name` of `rgba` (`side` square, rows top first), repeated past its edges.
    void addTexture(const char* name, const std::vector<std::uint8_t>& rgba, int side);

    RenderEngine& m_engine;
    std::vector<CardTexture> m_textures;
    std::vector<Piece> m_pieces;
};

} // namespace coney::platform
