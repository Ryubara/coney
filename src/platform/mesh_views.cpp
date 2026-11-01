// SPDX-License-Identifier: GPL-3.0-or-later
// The Debug draw page's mesh views drawn over the play mode's models (docs/guides/debug-menu.md#mesh-views). Coney's
// own debug tooling, in the debug tools' target (docs/guides/building.md#debug-tools).
#include "platform/mesh_views.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include <rw.h>

#include "platform/draw_calls.h"
#include "platform/play_level_controls.h"

namespace coney::platform {

namespace {

// The debug kind of a model the scene draws. The play mode draws no librw model as an effect or outside its scopes;
// one that ever is counts as an object, so the views' "everything" still covers it.
debug::MeshKind meshKindOf(DrawKind kind) {
    switch (kind) {
    case DrawKind::Scenery:
        return debug::MeshKind::Scenery;
    case DrawKind::Humans:
        return debug::MeshKind::Human;
    case DrawKind::Objects:
    case DrawKind::Effects:
    case DrawKind::Other:
        return debug::MeshKind::Object;
    }
    return debug::MeshKind::Object;
}

// What a pass changes on one material, to put back after it.
struct SavedMaterial {
    rw::RGBA colour;
    rw::Texture* texture = nullptr;
    rw::SurfaceProperties surface;
};

// The render states a pass changes, saved as they were and put back when it ends.
class SavedStates {
  public:
    SavedStates()
        : m_fog(draw::getRenderState(rw::FOGENABLE)), m_alphaTest(draw::getRenderState(rw::ALPHATESTFUNC)),
          m_zWrite(draw::getRenderState(rw::ZWRITEENABLE)), m_vertexAlpha(draw::getRenderState(rw::VERTEXALPHA)) {}
    // Puts the saved states back, however the pass ends.
    ~SavedStates() {
        draw::setRenderState(rw::FOGENABLE, m_fog);
        draw::setRenderState(rw::ALPHATESTFUNC, m_alphaTest);
        draw::setRenderState(rw::ZWRITEENABLE, m_zWrite);
        draw::setRenderState(rw::VERTEXALPHA, m_vertexAlpha);
    }
    SavedStates(const SavedStates&) = delete;
    SavedStates& operator=(const SavedStates&) = delete;
    SavedStates(SavedStates&&) = delete;
    SavedStates& operator=(SavedStates&&) = delete;

  private:
    rw::uint32 m_fog;
    rw::uint32 m_alphaTest;
    rw::uint32 m_zWrite;
    rw::uint32 m_vertexAlpha;
};

// librw's colour from ours, opaque.
rw::RGBA rwColour(graphics::Rgba colour) { return rw::makeRGBA(colour.r, colour.g, colour.b, 255); }

} // namespace

MeshViews::~MeshViews() {
    if (m_world != nullptr) {
        m_world->removeLight(m_light);
        rw::Frame* frame = m_light->getFrame();
        m_light->destroy();
        if (frame != nullptr) {
            frame->destroy();
        }
        m_world->destroy();
    }
}

bool MeshViews::beginFrame(const PlayLevelMode& mode) {
    m_drawIndex = 0;
    if (!debug::meshPassesOn(m_options.mesh)) {
        m_selected.clear();
        return false;
    }
    m_selected = PlayLevelControls::atomicsOf(mode, m_options.inspected);
    return true;
}

void MeshViews::draw(rw::Atomic* atomic, DrawKind kind, bool layer, const std::function<void()>& shaded) {
    const bool selected = std::ranges::find(m_selected, atomic) != m_selected.end();
    const debug::MeshDrawPlan plan = debug::meshPlanFor(m_options.mesh, meshKindOf(kind), selected);
    // A second layer (a human's blood) goes with the shaded draw alone.
    if (layer) {
        if (plan.shaded) {
            shaded();
        }
        return;
    }
    if (plan.shaded) {
        shaded();
    }
    if (plan.flat) {
        flatPass(atomic, plan, false);
    }
    // No wire where the draws do not reach OpenGL (the same decision as flatPass makes: glad's table outlives its
    // context, so a pointer says nothing there); and OpenGL ES has no line polygon mode, which its loader leaves unset.
    if (plan.wire && draw::forwarding() && glad_glPolygonMode != nullptr) {
        flatPass(atomic, plan, true);
    }
}

rw::World* MeshViews::flatWorld() {
    if (m_world == nullptr) {
        m_world = rw::World::create();
        m_light = rw::Light::create(rw::Light::AMBIENT);
        m_light->setFlags(rw::Light::LIGHTATOMICS);
        m_light->setFrame(rw::Frame::create());
        m_light->setColor(1.0F, 1.0F, 1.0F);
        m_world->addLight(m_light);
    }
    return m_world;
}

void MeshViews::flatPass(rw::Atomic* atomic, const debug::MeshDrawPlan& plan, bool lines) {
    rw::Geometry* geometry = atomic->geometry;
    // Lit by the white ambient alone and modulated by the material colour, a vertex's colour is the material's
    // (librw clamps the light to 1 before it multiplies, prelit or not).
    const rw::uint32 flags = geometry->flags;
    geometry->flags |= rw::Geometry::LIGHT | rw::Geometry::MODULATE;
    std::vector<SavedMaterial> saved(static_cast<std::size_t>(geometry->matList.numMaterials));
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        rw::Material* material = geometry->matList.materials[i];
        const auto index = static_cast<std::size_t>(i);
        saved[index] =
            SavedMaterial{.colour = material->color, .texture = material->texture, .surface = material->surfaceProps};
        graphics::Rgba colour = plan.wireColour;
        if (!lines) {
            switch (plan.colouring) {
            case debug::MeshShading::DrawCall:
                colour = debug::meshPaletteColour(static_cast<std::uint32_t>(m_drawIndex));
                break;
            case debug::MeshShading::Lod:
                colour = debug::meshLodColour(static_cast<float>(material->color.alpha) / 255.0F);
                break;
            case debug::MeshShading::Material:
            default:
                colour = debug::meshPaletteColour(debug::meshMaterialKey(
                    material->texture != nullptr ? std::string_view(material->texture->name) : std::string_view{},
                    index));
                break;
            }
            ++m_drawIndex;
        }
        material->color = rwColour(colour);
        material->texture = nullptr; // librw then binds its white texture
        material->surfaceProps.ambient = 1.0F;
    }
    const SavedStates states;
    draw::setRenderState(rw::FOGENABLE, 0);
    draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
    if (lines) {
        draw::setRenderState(rw::ZWRITEENABLE, 0);
        draw::setWireframe(true);
    }
    // Drawn in the flat world, so librw lights it with the white ambient alone; not at all while the draws do not reach
    // OpenGL (librw's NULL device, as the tests run it). The renderer's forwarding says so, not glad's pointers: those
    // stay set after an OpenGL renderer stops, so a NULL one started later in the same process would draw through them
    // with no context (issue #1268).
    if (draw::forwarding()) {
        rw::World* current = rw::engine->currentWorld;
        rw::World* home = atomic->world;
        rw::engine->currentWorld = flatWorld();
        atomic->world = flatWorld();
        atomic->render();
        atomic->world = home;
        rw::engine->currentWorld = current;
    }
    if (lines) {
        draw::setWireframe(false);
    }
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        rw::Material* material = geometry->matList.materials[i];
        const SavedMaterial& before = saved[static_cast<std::size_t>(i)];
        material->color = before.colour;
        material->texture = before.texture;
        material->surfaceProps = before.surface;
    }
    geometry->flags = flags;
}

void MeshViewRun::frame(debug::DebugSession& session) {
    if (m_done) {
        return;
    }
    debug::DebugDrawOptions& draw = session.debugDraw();
    if (!m_applied) {
        // Waits for a play mode that lists its entities: the level's first frames.
        const debug::PlayControls* play = session.play();
        const debug::EntityInspect* inspect = play != nullptr ? play->inspect() : nullptr;
        if (inspect == nullptr) {
            return;
        }
        draw.mesh = m_spec.options;
        if (m_spec.select != debug::MeshViewSelect::None) {
            // Player 1 is listed first; the nearest is chosen among the other humans.
            const std::vector<debug::EntityRef> entities = inspect->entities();
            const debug::EntityId player = entities.empty() || entities.front().kind != debug::EntityKind::Player
                                               ? debug::kNoEntity
                                               : entities.front().id;
            std::vector<debug::EntityRef> humans;
            for (const debug::EntityRef& entity : entities) {
                if (entity.kind == debug::EntityKind::Human) {
                    humans.push_back(entity);
                }
            }
            const debug::EntityId nearest = debug::nearestEntity(humans, play->playerFeet());
            draw.inspected =
                m_spec.select == debug::MeshViewSelect::Nearest && nearest != debug::kNoEntity ? nearest : player;
        }
        m_applied = true;
    }
    // After its frames every view goes off, and the selection it made with them.
    if (m_spec.frames > 0 && ++m_frames > m_spec.frames) {
        draw.mesh = debug::MeshViewOptions{};
        if (m_spec.select != debug::MeshViewSelect::None) {
            draw.inspected = debug::kNoEntity;
        }
        m_done = true;
    }
}

} // namespace coney::platform
