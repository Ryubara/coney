// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/test_card_scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

#include <rw.h>

#include "graphics/texture_data.h"
#include "platform/draw_calls.h"
#include "platform/movie_screen.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "platform/world_renderer.h"

namespace coney::platform {

namespace {

using graphics::Rgba;
using Point = std::array<float, 3>;

// The side of the card's textures, in texels.
constexpr int kSide = 16;
// The fog's colour, which the frame is also cleared to, as the world pass clears it.
constexpr Rgba kFog{60, 70, 90, 255};

// What a quad of the card is: its corners as the camera sees them, its normal, how its geometry is flagged and
// coloured, its texture and how often that repeats across it.
struct QuadSpec {
    Point topLeft{};
    Point topRight{};
    Point bottomLeft{};
    Point bottomRight{};
    Point normal{0.0F, 0.0F, -1.0F};
    bool lit = true;
    bool prelit = false;
    Rgba prelight{255, 255, 255, 255};
    rw::Texture* texture = nullptr;
    float repeatU = 1.0F;
    float repeatV = 1.0F;
    bool facingAway = false; // wound clockwise as the camera sees it
};

// A librw atomic of one quad as `spec` says, its frame at the world's origin: texture coordinates from (0, 0) at its
// top left to the repeats at its bottom right, wound counter-clockwise on the screen (the front OpenGL keeps) unless
// it faces away. Freed by destroyAtomic().
rw::Atomic* makeQuad(const QuadSpec& spec) {
    rw::uint32 flags = rw::Geometry::POSITIONS | rw::Geometry::NORMALS | rw::Geometry::TEXTURED;
    if (spec.lit) {
        flags |= rw::Geometry::LIGHT;
    }
    if (spec.prelit) {
        flags |= rw::Geometry::PRELIT;
    }
    rw::Geometry* geometry = rw::Geometry::create(4, 2, flags);
    rw::MorphTarget& target = geometry->morphTargets[0];
    const std::array<Point, 4> corners{spec.topLeft, spec.topRight, spec.bottomLeft, spec.bottomRight};
    const std::array<rw::TexCoords, 4> uvs{
        {{0.0F, 0.0F}, {spec.repeatU, 0.0F}, {0.0F, spec.repeatV}, {spec.repeatU, spec.repeatV}}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        target.vertices[i] = rw::makeV3d(corners[i][0], corners[i][1], corners[i][2]);
        target.normals[i] = rw::makeV3d(spec.normal[0], spec.normal[1], spec.normal[2]);
        geometry->texCoords[0][i] = uvs[i];
        if (spec.prelit) {
            geometry->colors[i] = rw::makeRGBA(spec.prelight.r, spec.prelight.g, spec.prelight.b, spec.prelight.a);
        }
    }
    // Corners 0 top left, 1 top right, 2 bottom left, 3 bottom right.
    const std::array<std::array<rw::uint16, 3>, 2> front{{{0, 2, 1}, {1, 2, 3}}};
    for (std::size_t t = 0; t < front.size(); ++t) {
        rw::Triangle& triangle = geometry->triangles[t];
        triangle.v[0] = front[t][0];
        triangle.v[1] = spec.facingAway ? front[t][2] : front[t][1];
        triangle.v[2] = spec.facingAway ? front[t][1] : front[t][2];
        triangle.matId = 0;
    }
    rw::Material* material = rw::Material::create();
    if (spec.texture != nullptr) {
        material->setTexture(spec.texture);
    }
    geometry->matList.appendMaterial(material);
    material->destroy(); // the list holds its own reference
    geometry->buildMeshes();
    geometry->calculateBoundingSphere();
    rw::Atomic* atomic = rw::Atomic::create();
    atomic->setGeometry(geometry, 0);
    geometry->destroy(); // the atomic holds its own reference
    atomic->setFrame(rw::Frame::create());
    return atomic;
}

// Frees `atomic` and its frame (null: nothing).
void destroyAtomic(rw::Atomic* atomic) {
    if (atomic == nullptr) {
        return;
    }
    rw::Frame* frame = atomic->getFrame();
    atomic->setFrame(nullptr);
    atomic->destroy();
    if (frame != nullptr) {
        frame->destroy();
    }
}

// A checkerboard of 4-texel squares in two colours; with `holes`, its dark squares fully transparent.
std::vector<std::uint8_t> checker(Rgba light, Rgba dark, bool holes) {
    std::vector<std::uint8_t> texels;
    texels.reserve(static_cast<std::size_t>(kSide) * kSide * 4);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            Rgba colour = ((x / 4) + (y / 4)) % 2 == 0 ? light : dark;
            if (holes && colour == dark) {
                colour.a = 0;
            }
            texels.insert(texels.end(), {colour.r, colour.g, colour.b, colour.a});
        }
    }
    return texels;
}

// Yellow texels whose alpha rises from 0 at the top row to 255 at the bottom.
std::vector<std::uint8_t> ramp() {
    std::vector<std::uint8_t> texels;
    texels.reserve(static_cast<std::size_t>(kSide) * kSide * 4);
    for (int y = 0; y < kSide; ++y) {
        const auto alpha = static_cast<std::uint8_t>(y * 255 / (kSide - 1));
        for (int x = 0; x < kSide; ++x) {
            texels.insert(texels.end(), {250, 220, 40, alpha});
        }
    }
    return texels;
}

// The card's camera: 2.5 above the floor looking a little down along RenderWare's z, the screen's right along -x, at a
// 320 x 224 frame's shape, seeing to 60 with the fog from half of it.
WorldView cardView() {
    WorldView view;
    const float down = 0.2F;
    const float length = std::sqrt(1.0F + (down * down));
    view.pose.position = {0.0F, 2.5F, 0.0F};
    view.pose.forward = {0.0F, -down / length, 1.0F / length};
    view.pose.right = {-1.0F, 0.0F, 0.0F};
    view.pose.up = {0.0F, 1.0F / length, down / length}; // right x forward
    view.halfWidth = 0.7F;
    view.halfHeight = 0.49F;
    view.nearClip = 0.5F;
    view.drawDistance = 60.0F;
    view.fogStart = 0.5F;
    return view;
}

} // namespace

TestCardScene::TestCardScene(RenderEngine& engine) : m_engine(engine) {
    addTexture("cardcheck", checker(Rgba{230, 220, 190, 255}, Rgba{70, 110, 90, 255}, false), kSide);
    addTexture("cardholes", checker(Rgba{200, 230, 255, 255}, Rgba{0, 0, 0, 255}, true), kSide);
    addTexture("cardramp", ramp(), kSide);
    rw::Texture* checkered = m_textures[0].texture;

    const graphics::SceneStates world{.fogColour = kFog};
    graphics::SceneStates firstPass = world;
    firstPass.alphaTest = graphics::AlphaTest::GreaterEqual;
    // The screen's left is +x: each quad's corners are given as the camera sees them.
    const QuadSpec floor{.topLeft = {12.0F, 0.0F, 60.0F},
                         .topRight = {-12.0F, 0.0F, 60.0F},
                         .bottomLeft = {12.0F, 0.0F, 1.0F},
                         .bottomRight = {-12.0F, 0.0F, 1.0F},
                         .normal = {0.0F, 1.0F, 0.0F},
                         .prelit = true,
                         .prelight = {90, 90, 90, 255},
                         .texture = checkered,
                         .repeatU = 8.0F,
                         .repeatV = 20.0F};
    const QuadSpec sign{.topLeft = {4.5F, 3.5F, 14.0F},
                        .topRight = {0.5F, 3.5F, 14.0F},
                        .bottomLeft = {4.5F, 0.5F, 14.0F},
                        .bottomRight = {0.5F, 0.5F, 14.0F},
                        .texture = m_textures[1].texture};
    const QuadSpec crate{.topLeft = {1.0F, 2.5F, 7.0F},
                         .topRight = {-1.5F, 2.5F, 7.0F},
                         .bottomLeft = {1.0F, 0.0F, 7.0F},
                         .bottomRight = {-1.5F, 0.0F, 7.0F},
                         .texture = checkered};
    QuadSpec panel{.topLeft = {-3.0F, 3.0F, 10.0F},
                   .topRight = {-6.0F, 3.0F, 10.0F},
                   .bottomLeft = {-3.0F, 0.5F, 10.0F},
                   .bottomRight = {-6.0F, 0.5F, 10.0F},
                   .texture = checkered,
                   .repeatU = 2.0F,
                   .repeatV = 2.0F};
    QuadSpec panelLayer = panel;
    panelLayer.texture = m_textures[2].texture;
    panelLayer.repeatU = 1.0F;
    panelLayer.repeatV = 1.0F;
    const QuadSpec away{.topLeft = {10.0F, 8.0F, 3.0F},
                        .topRight = {-10.0F, 8.0F, 3.0F},
                        .bottomLeft = {10.0F, -2.0F, 3.0F},
                        .bottomRight = {-10.0F, -2.0F, 3.0F},
                        .lit = false,
                        .prelit = true,
                        .prelight = {255, 0, 255, 255},
                        .facingAway = true};

    SceneAssets& assets = m_engine.sceneAssets();
    // Each piece's atomic and dual layer into the scene assets (a mesh that did not convert keeps no id and draws
    // nothing on the GPU renderer).
    const auto add = [&](const QuadSpec& spec, const QuadSpec* layer, graphics::Material3d material,
                         const graphics::SceneStates& states) {
        Piece piece;
        piece.atomic = makeQuad(spec);
        piece.layer = layer != nullptr ? makeQuad(*layer) : nullptr;
        if (auto id = assets.addAtomic(*piece.atomic); id) {
            piece.mesh = *id;
        }
        if (piece.layer != nullptr) {
            if (auto id = assets.addAtomic(*piece.layer); id) {
                piece.layerMesh = *id;
            }
        }
        piece.material = material;
        piece.states = states;
        m_pieces.push_back(piece);
    };
    add(floor, nullptr, graphics::Material3d{.distanceLevels = true}, world);
    add(sign, nullptr, graphics::Material3d{}, firstPass);
    add(crate, nullptr, graphics::Material3d{.colour = graphics::MaterialColour::Tint, .tint = Rgba{255, 60, 60, 128}},
        world);
    add(panel, &panelLayer, graphics::Material3d{}, world);
    add(away, nullptr, graphics::Material3d{}, world);
}

TestCardScene::~TestCardScene() {
    SceneAssets& assets = m_engine.sceneAssets();
    for (const Piece& piece : m_pieces) {
        assets.removeMesh(piece.mesh);
        assets.removeMesh(piece.layerMesh);
        destroyAtomic(piece.atomic);
        destroyAtomic(piece.layer);
    }
    for (CardTexture& texture : m_textures) {
        assets.removeTexture(texture.id);
        if (!texture.movie && texture.texture != nullptr) {
            texture.texture->destroy();
        }
    }
}

void TestCardScene::addTexture(const char* name, const std::vector<std::uint8_t>& rgba, int side) {
    CardTexture made;
    if (m_engine.backend() == RenderBackend::OpenGl) {
        // GL3 draws the librw texture: a raster of the texels (MovieTexture writes them as librw samples them).
        made.movie = std::make_unique<MovieTexture>(graphics::TextureFilter::Linear);
        made.movie->write(rgba, side, side);
        made.texture = made.movie->rwTexture();
    } else {
        made.texture = rw::Texture::create(nullptr);
    }
    if (made.texture != nullptr) {
        made.texture->setAddressU(rw::Texture::WRAP);
        made.texture->setAddressV(rw::Texture::WRAP);
        const std::size_t length = std::min(std::strlen(name), sizeof(made.texture->name) - 1);
        std::memcpy(made.texture->name, name, length);
        made.texture->name[length] = '\0';
        // The GPU renderer's copy, the same texels, filtered linearly and repeated.
        graphics::TextureData data;
        data.name = name;
        data.levels.push_back({static_cast<std::uint32_t>(side), static_cast<std::uint32_t>(side), rgba});
        data.sampler = {graphics::TextureFilter::Linear, graphics::TextureAddress::Wrap,
                        graphics::TextureAddress::Wrap};
        if (auto id = m_engine.sceneAssets().addTexture(*made.texture, std::move(data)); id) {
            made.id = *id;
        }
    }
    m_textures.push_back(std::move(made));
}

void TestCardScene::draw() {
    const WorldView view = cardView();
    m_engine.beginWindowFrame(kFog);
    // GL3 draws through librw's camera, placed as the world pass places it; the GPU renderer through the frame's.
    if (rw::Camera* camera = m_engine.camera(); camera != nullptr) {
        placeWorldCamera(camera, view);
        draw::beginCameraUpdate(camera);
    }
    m_engine.setSceneCamera(sceneCameraOf(view));
    const std::array<graphics::SceneLight, 3> lights{{
        {.type = graphics::LightType::Ambient, .colour = {0.35F, 0.35F, 0.4F}},
        {.type = graphics::LightType::Directional, .colour = {0.5F, 0.45F, 0.4F}, .direction = {0.3F, -1.0F, 0.6F}},
        {.type = graphics::LightType::Point,
         .colour = {0.9F, 0.6F, 0.2F},
         .position = {-1.5F, 1.5F, 9.0F},
         .radius = 6.0F},
    }};
    for (const Piece& piece : m_pieces) {
        if (!piece.mesh.valid()) {
            continue;
        }
        graphics::Draw3d scene;
        scene.mesh = piece.mesh;
        scene.dualLayer = piece.layerMesh;
        scene.material = piece.material;
        scene.states = piece.states;
        m_engine.drawScene(scene, lights);
    }
}

} // namespace coney::platform
