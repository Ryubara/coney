// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/texture_lod.h"

#include <array>
#include <cmath>
#include <cstring>

#include <rw.h>

#include "graphics/texture_lod.h"

namespace coney::platform {

namespace {

// Coney's raster plugin: the GS level parameters of the PS2 raster a GL3 raster was converted from.
constexpr rw::uint32 kTextureLodPluginId = 0x00C0DE02;

struct RasterLodData {
    float k = 0.0F;
    int l = 0;
    bool fromPs2 = false; // false: a texture of Coney's own, which OpenGL picks levels for as usual
};

rw::int32 rasterLodOffset = 0;
rw::int32 lodUniform = -1;

// The four shaders, as librw's default step has them: without and with every light kind, with and without the alpha
// test.
struct LodShaders {
    rw::gl3::Shader* plain = nullptr;
    rw::gl3::Shader* plainNoTest = nullptr;
    rw::gl3::Shader* lit = nullptr;
    rw::gl3::Shader* litNoTest = nullptr;
};
LodShaders shaders;
void (*librwRenderCB)(rw::Atomic*, rw::gl3::InstanceDataHeader*) = nullptr;
bool distanceMipmaps = false;

// A new raster has no level parameters: K 0, L 0.
void* constructRasterLod(void* object, rw::int32 offset, rw::int32 /*size*/) {
    *PLUGINOFFSET(RasterLodData, object, offset) = RasterLodData{};
    return object;
}

// librw's default vertex step, also handing the fragment step the view depth: the camera distance 1/Q the GS uses
// (librw's projection puts the view-space z in w).
const char* const kVertexSource = R"(
VSIN(ATTRIB_POS) vec3 in_pos;

VSOUT vec4 v_color;
VSOUT vec2 v_tex0;
VSOUT float v_fog;
VSOUT float v_depth;

void main(void)
{
    vec4 Vertex = u_world * vec4(in_pos, 1.0);
    gl_Position = u_proj * u_view * Vertex;
    vec3 Normal = mat3(u_world) * in_normal;
    v_tex0 = in_tex0;
    v_color = in_color;
    v_color.rgb += u_ambLight.rgb * surfAmbient;
    v_color.rgb += DoDynamicLight(Vertex.xyz, Normal) * surfDiffuse;
    v_color = clamp(v_color, 0.0, 1.0);
    v_color *= u_matColor;
    v_fog = DoFog(gl_Position.w);
    v_depth = gl_Position.w;
}
)";

// The texture sampled at an explicit level: log2(depth) x 2^L + K while u_coneyLod.z is 1, else level 0. Below 0 is
// magnification, level 0. A texture not from the disc (u_coneyLod.w 1: the sandbox's) is sampled as OpenGL chooses.
// Then fog and the alpha test as librw's own step does them.
const char* const kFragmentSource = R"(
uniform sampler2D tex0;
uniform vec4 u_coneyLod;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;
FSIN float v_depth;

void main(void)
{
    float lod = 0.0;
    if (u_coneyLod.z > 0.5)
        lod = max(log2(max(v_depth, 1.0 / 1024.0)) * u_coneyLod.y + u_coneyLod.x, 0.0);
    vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);
    vec4 color = v_color * (u_coneyLod.w > 0.5 ? texture(tex0, uv) : textureLod(tex0, uv, lod));
    color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);
    DoAlphaTest(color.a);
    FRAGCOLOR(color);
}
)";

// librw's default render step with Coney's shaders and each mesh's level parameters.
void renderWithLod(rw::Atomic* atomic, rw::gl3::InstanceDataHeader* header) {
    const rw::uint32 flags = atomic->geometry->flags;
    rw::gl3::setWorldMatrix(atomic->getFrame()->getLTM());
    const rw::int32 lightBits = rw::gl3::lightingCB(atomic);
    rw::gl3::setupVertexInput(header);
    rw::gl3::InstanceData* inst = header->inst;
    for (rw::uint32 n = 0; n < header->numMeshes; ++n, ++inst) {
        rw::Material* material = inst->material;
        rw::gl3::setMaterial(flags, material->color, material->surfaceProps);
        rw::gl3::setTexture(0, material->texture);
        rw::SetRenderState(rw::VERTEXALPHA, inst->vertexAlpha || material->color.alpha != 0xFF);
        RasterLodData lod;
        if (material->texture != nullptr && material->texture->raster != nullptr) {
            lod = *PLUGINOFFSET(RasterLodData, material->texture->raster, rasterLodOffset);
        }
        std::array<float, 4> values{lod.k, std::ldexp(1.0F, lod.l), distanceMipmaps ? 1.0F : 0.0F,
                                    lod.fromPs2 ? 0.0F : 1.0F};
        rw::gl3::setUniform(lodUniform, values.data());
        const bool lit = (lightBits & rw::gl3::VSLIGHT_MASK) != 0;
        const bool tested = rw::gl3::getAlphaTest() != 0;
        rw::gl3::Shader* shader =
            lit ? (tested ? shaders.lit : shaders.litNoTest) : (tested ? shaders.plain : shaders.plainNoTest);
        shader->use();
        rw::gl3::drawInst(header, inst);
    }
    rw::gl3::teardownVertexInput(header);
}

// librw's default GL3 object pipeline, whose render step Coney replaces.
rw::gl3::ObjPipeline* defaultPipeline() {
    return static_cast<rw::gl3::ObjPipeline*>(rw::engine->driver[rw::PLATFORM_GL3]->defaultPipeline);
}

} // namespace

void attachTextureLodPlugin() {
    rasterLodOffset =
        rw::Raster::registerPlugin(sizeof(RasterLodData), kTextureLodPluginId, constructRasterLod, nullptr, nullptr);
    // Before the device makes its shaders, so every shader has a slot for it.
    lodUniform = rw::gl3::registerUniform("u_coneyLod", rw::gl3::UNIFORM_VEC4);
}

void setRasterLod(rw::Raster* raster, std::uint32_t packedKl) {
    const graphics::TextureLod lod = graphics::unpackTextureLod(packedKl);
    *PLUGINOFFSET(RasterLodData, raster, rasterLodOffset) = RasterLodData{.k = lod.k, .l = lod.l, .fromPs2 = true};
}

bool startTextureLod() {
    // textureLod needs GLSL 1.30 or ES 3.00; librw's GL 2.1 and ES 2 declarations define GL2.
    if (std::strstr(rw::gl3::shaderDecl, "#define GL2") != nullptr || librwRenderCB != nullptr) {
        return librwRenderCB != nullptr;
    }
    const char* allLights = "#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n";
    const char* noTest = "#define NO_ALPHATEST\n";
    const char* vs[] = {rw::gl3::shaderDecl, rw::gl3::header_vert_src, kVertexSource, nullptr};
    const char* vsLit[] = {rw::gl3::shaderDecl, allLights, rw::gl3::header_vert_src, kVertexSource, nullptr};
    const char* fs[] = {rw::gl3::shaderDecl, rw::gl3::header_frag_src, kFragmentSource, nullptr};
    const char* fsNoTest[] = {rw::gl3::shaderDecl, noTest, rw::gl3::header_frag_src, kFragmentSource, nullptr};
    shaders.plain = rw::gl3::Shader::create(vs, fs);
    shaders.plainNoTest = rw::gl3::Shader::create(vs, fsNoTest);
    shaders.lit = rw::gl3::Shader::create(vsLit, fs);
    shaders.litNoTest = rw::gl3::Shader::create(vsLit, fsNoTest);
    if (shaders.plain == nullptr || shaders.plainNoTest == nullptr || shaders.lit == nullptr ||
        shaders.litNoTest == nullptr) {
        stopTextureLod();
        return false;
    }
    rw::gl3::ObjPipeline* pipeline = defaultPipeline();
    librwRenderCB = pipeline->renderCB;
    pipeline->renderCB = renderWithLod;
    return true;
}

void stopTextureLod() {
    if (librwRenderCB != nullptr) {
        defaultPipeline()->renderCB = librwRenderCB;
        librwRenderCB = nullptr;
    }
    for (rw::gl3::Shader** shader : {&shaders.plain, &shaders.plainNoTest, &shaders.lit, &shaders.litNoTest}) {
        if (*shader != nullptr) {
            (*shader)->destroy();
            *shader = nullptr;
        }
    }
    distanceMipmaps = false;
}

void setDistanceMipmaps(bool on) { distanceMipmaps = on; }

} // namespace coney::platform
