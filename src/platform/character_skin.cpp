// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_skin.h"

#include <cstring>

#include <rw.h>

#include "core/assert.h"
#include "platform/texture_lod.h"

namespace coney::platform {

namespace {

// Coney's geometry plugin (a skinned geometry's vertex weights) and atomic plugin (its bone palette).
constexpr rw::uint32 kSkinVerticesPluginId = 0x00C0DE03;
constexpr rw::uint32 kSkinPalettePluginId = 0x00C0DE04;

rw::int32 verticesOffset = 0;
rw::int32 paletteOffset = 0;
rw::int32 bonesUniform = -1;

// The shaders, as the default pipeline's (platform/texture_lod.cpp): without and with every light kind, with and
// without the alpha test.
struct SkinShaders {
    rw::gl3::Shader* plain = nullptr;
    rw::gl3::Shader* plainNoTest = nullptr;
    rw::gl3::Shader* lit = nullptr;
    rw::gl3::Shader* litNoTest = nullptr;
};
SkinShaders shaders;
rw::gl3::ObjPipeline* pipeline = nullptr;
bool allowed = true;

// Plugin constructors: no data until useCharacterSkin().
void* constructNull(void* object, rw::int32 offset, rw::int32 /*size*/) {
    *PLUGINOFFSET(const void*, object, offset) = nullptr;
    return object;
}

// The default pipeline's vertex step with the vertex and its normal first blended by up to four bones. The palette
// holds the whole placement in the world (u_world is the identity for a skinned character), and the blended normal
// is normalised, as the CPU skinning normalised it (characters::skinVertices()).
const char* const kVertexSource = R"(
uniform mat4 u_coneyBones[64];

VSIN(ATTRIB_POS) vec3 in_pos;

VSOUT vec4 v_color;
VSOUT vec2 v_tex0;
VSOUT float v_fog;
VSOUT float v_depth;

void main(void)
{
    vec3 skinned = vec3(0.0);
    vec3 skinnedNormal = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        mat4 bone = u_coneyBones[int(in_indices[i])];
        skinned += (bone * vec4(in_pos, 1.0)).xyz * in_weights[i];
        skinnedNormal += (mat3(bone) * in_normal) * in_weights[i];
    }
    vec4 Vertex = u_world * vec4(skinned, 1.0);
    gl_Position = u_proj * u_view * Vertex;
    vec3 Normal = mat3(u_world) * normalize(skinnedNormal);
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

// One vertex attribute of the interleaved buffer.
rw::gl3::AttribDesc attribute(rw::uint32 index, rw::int32 type, rw::int32 size, rw::uint32 offset) {
    rw::gl3::AttribDesc desc{};
    desc.index = index;
    desc.type = type;
    desc.normalized = GL_FALSE;
    desc.size = size;
    desc.offset = offset;
    return desc;
}

// Fills the vertex buffer once: position, normal, texture coordinates (the geometry's one set), weights and bone
// indices, interleaved. A skinned geometry is never locked again (its pose is the palette), so a reinstance has
// nothing to redo but the upload.
void instanceSkinned(rw::Geometry* geometry, rw::gl3::InstanceDataHeader* header, rw::bool32 reinstance) {
    const SkinVertexData* data = *PLUGINOFFSET(const SkinVertexData*, geometry, verticesOffset);
    CONEY_ASSERT(data != nullptr && geometry->numTexCoordSets == 1);
    constexpr rw::uint32 kStride = 12 + 12 + 8 + 16 + 4;
    if (!reinstance) {
        const std::array<rw::gl3::AttribDesc, 5> attributes{
            attribute(rw::gl3::ATTRIB_POS, GL_FLOAT, 3, 0), attribute(rw::gl3::ATTRIB_NORMAL, GL_FLOAT, 3, 12),
            attribute(rw::gl3::ATTRIB_TEXCOORDS0, GL_FLOAT, 2, 24), attribute(rw::gl3::ATTRIB_WEIGHTS, GL_FLOAT, 4, 32),
            attribute(rw::gl3::ATTRIB_INDICES, GL_UNSIGNED_BYTE, 4, 48)};
        header->numAttribs = static_cast<rw::int32>(attributes.size());
        header->attribDesc = rwNewT(rw::gl3::AttribDesc, header->numAttribs, rw::MEMDUR_EVENT | rw::ID_GEOMETRY);
        for (std::size_t i = 0; i < attributes.size(); ++i) {
            header->attribDesc[i] = attributes[i];
            header->attribDesc[i].stride = kStride;
        }
        header->vertexBuffer =
            rwNewT(rw::uint8, header->totalNumVertex * kStride, rw::MEMDUR_EVENT | rw::ID_GEOMETRY);
        glGenBuffers(1, &header->vbo);
        const rw::MorphTarget& target = geometry->morphTargets[0];
        CONEY_ASSERT(data->weights.size() >= static_cast<std::size_t>(header->totalNumVertex) &&
                     data->indices.size() == data->weights.size());
        for (rw::int32 i = 0; i < header->totalNumVertex; ++i) {
            rw::uint8* vertex = header->vertexBuffer + static_cast<std::size_t>(i) * kStride;
            const auto index = static_cast<std::size_t>(i);
            std::memcpy(vertex, &target.vertices[i], 12);
            std::memcpy(vertex + 12, &target.normals[i], 12);
            std::memcpy(vertex + 24, &geometry->texCoords[0][i], 8);
            std::memcpy(vertex + 32, data->weights[index].data(), 16);
            std::memcpy(vertex + 48, data->indices[index].data(), 4);
        }
    }
#ifdef RW_GL_USE_VAOS
    glBindVertexArray(header->vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, header->ibo);
#endif
    glBindBuffer(GL_ARRAY_BUFFER, header->vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(header->totalNumVertex) * kStride, header->vertexBuffer,
                 GL_STATIC_DRAW);
#ifdef RW_GL_USE_VAOS
    rw::gl3::setAttribPointers(header->attribDesc, header->numAttribs);
    glBindVertexArray(0);
#endif
}

// The default pipeline's render step (platform/texture_lod.cpp) with the skinned shaders and the atomic's palette.
void renderSkinned(rw::Atomic* atomic, rw::gl3::InstanceDataHeader* header) {
    const rw::uint32 flags = atomic->geometry->flags;
    rw::gl3::setWorldMatrix(atomic->getFrame()->getLTM());
    const rw::int32 lightBits = rw::gl3::lightingCB(atomic);
    rw::gl3::setupVertexInput(header);
    const BonePalette* palette = *PLUGINOFFSET(const BonePalette*, atomic, paletteOffset);
    CONEY_ASSERT(palette != nullptr);
    rw::gl3::setUniform(bonesUniform, const_cast<float*>(palette->data()));
    rw::gl3::InstanceData* inst = header->inst;
    for (rw::uint32 n = 0; n < header->numMeshes; ++n, ++inst) {
        rw::Material* material = inst->material;
        rw::gl3::setMaterial(flags, material->color, material->surfaceProps);
        rw::gl3::setTexture(0, material->texture);
        rw::SetRenderState(rw::VERTEXALPHA, inst->vertexAlpha || material->color.alpha != 0xFF);
        setTextureLodFor(material);
        const bool lit = (lightBits & rw::gl3::VSLIGHT_MASK) != 0;
        const bool tested = rw::gl3::getAlphaTest() != 0;
        rw::gl3::Shader* shader =
            lit ? (tested ? shaders.lit : shaders.litNoTest) : (tested ? shaders.plain : shaders.plainNoTest);
        shader->use();
        rw::gl3::drawInst(header, inst);
    }
    rw::gl3::teardownVertexInput(header);
}

} // namespace

void attachCharacterSkin() {
    verticesOffset = rw::Geometry::registerPlugin(sizeof(const SkinVertexData*), kSkinVerticesPluginId, constructNull,
                                                  nullptr, nullptr);
    paletteOffset =
        rw::Atomic::registerPlugin(sizeof(const BonePalette*), kSkinPalettePluginId, constructNull, nullptr, nullptr);
    // Before the device makes its shaders, so every shader has a slot for it.
    bonesUniform =
        rw::gl3::registerUniform("u_coneyBones", rw::gl3::UNIFORM_MAT4, static_cast<rw::int32>(kMaxSkinBones));
}

bool startCharacterSkin() {
    if (pipeline != nullptr) {
        return true;
    }
    // The fragment step samples at an explicit level, which OpenGL 2.1 and ES 2 cannot (platform/texture_lod.h).
    if (std::strstr(rw::gl3::shaderDecl, "#define GL2") != nullptr) {
        return false;
    }
    const char* allLights = "#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n";
    const char* noTest = "#define NO_ALPHATEST\n";
    const char* fragment = textureLodFragmentSource();
    const char* vs[] = {rw::gl3::shaderDecl, rw::gl3::header_vert_src, kVertexSource, nullptr};
    const char* vsLit[] = {rw::gl3::shaderDecl, allLights, rw::gl3::header_vert_src, kVertexSource, nullptr};
    const char* fs[] = {rw::gl3::shaderDecl, rw::gl3::header_frag_src, fragment, nullptr};
    const char* fsNoTest[] = {rw::gl3::shaderDecl, noTest, rw::gl3::header_frag_src, fragment, nullptr};
    shaders.plain = rw::gl3::Shader::create(vs, fs);
    shaders.plainNoTest = rw::gl3::Shader::create(vs, fsNoTest);
    shaders.lit = rw::gl3::Shader::create(vsLit, fs);
    shaders.litNoTest = rw::gl3::Shader::create(vsLit, fsNoTest);
    if (shaders.plain == nullptr || shaders.plainNoTest == nullptr || shaders.lit == nullptr ||
        shaders.litNoTest == nullptr) {
        stopCharacterSkin();
        return false;
    }
    pipeline = rw::gl3::ObjPipeline::create();
    pipeline->instanceCB = instanceSkinned;
    pipeline->uninstanceCB = nullptr;
    pipeline->renderCB = renderSkinned;
    return true;
}

void stopCharacterSkin() {
    if (pipeline != nullptr) {
        pipeline->destroy();
        pipeline = nullptr;
    }
    for (rw::gl3::Shader** shader : {&shaders.plain, &shaders.plainNoTest, &shaders.lit, &shaders.litNoTest}) {
        if (*shader != nullptr) {
            (*shader)->destroy();
            *shader = nullptr;
        }
    }
}

bool characterSkinOn() { return pipeline != nullptr && allowed; }

void allowCharacterSkin(bool allow) { allowed = allow; }

void useCharacterSkin(rw::Atomic* atomic, const SkinVertexData& vertices, const BonePalette& palette) {
    CONEY_ASSERT(characterSkinOn() && atomic != nullptr);
    *PLUGINOFFSET(const SkinVertexData*, atomic->geometry, verticesOffset) = &vertices;
    *PLUGINOFFSET(const BonePalette*, atomic, paletteOffset) = &palette;
    atomic->pipeline = pipeline;
}

} // namespace coney::platform
