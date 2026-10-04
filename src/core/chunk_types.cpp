// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chunk_types.h"

#include <array>

namespace coney::chunk {

namespace {

// The names are facts read from the original's table (docs/research/chunk-system.md#chunk-type-table), kept with
// their original spelling so they can be matched against that page.
constexpr std::array<std::string_view, kChunkTypeCount> kNames = {
    "Anim Rot Keyframes",      // 0x00
    "Anim Pos Keyframes",      // 0x01
    "Anim Data",               // 0x02
    "Collision Mesh",          // 0x03
    "Collision Triangles",     // 0x04
    "Collision Grid",          // 0x05
    "Collision Strings",       // 0x06
    "Collision Vertex Buffer", // 0x07
    "Character Data",          // 0x08
    "Character DFF Data",      // 0x09
    "Character Device OID",    // 0x0A
    "Texture Dictionary TID",  // 0x0B
    "Camera Rail Nodes",       // 0x0C
    "Camera Rail Header",      // 0x0D
    "Camera Animation",        // 0x0E
    "English String Table",    // 0x0F
    "French String Table",     // 0x10
    "Italian String Table",    // 0x11
    "Spanish String Table",    // 0x12
    "German String Table",     // 0x13
    "Null Pointer",            // 0x14
    "Sector BSP Data",         // 0x15
    "World Header",            // 0x16
    "Level Header",            // 0x17
    "The One World",           // 0x18
    "Particle Types",          // 0x19
    "Particle Code Memory",    // 0x1A
    "Particle Vector Const",   // 0x1B
    "Particle Float Const",    // 0x1C
    "Particle Int Constants",  // 0x1D
    "Particle Trigger List",   // 0x1E
    "Particle Particle Types", // 0x1F
    "Particle Asm Debug",      // 0x20
    "Particle Source",         // 0x21
    "Game Object Instance",    // 0x22
    "Game Object Definition",  // 0x23
    "Game Object List",        // 0x24
    "Dynamic Obj DFF Data",    // 0x25
    "Static Obj DFF Data",     // 0x26
    "Level Header Obj List",   // 0x27
    "Chunk Bone Offsets",      // 0x28
    "Static Sounds",           // 0x29
    "Renderware Texture Dic",  // 0x2A
    "Game Map",                // 0x2B
    "Path Grid",               // 0x2C
    "Path Nodes",              // 0x2D
    "Grid Connections",        // 0x2E
    "Grid Connections2",       // 0x2F
    "GBH Script",              // 0x30
    "Music",                   // 0x31
    "Palette Bitmap Raw Data", // 0x32
    "Image Palettes",          // 0x33
    "Image Textures",          // 0x34
    "Texture Dictionary",      // 0x35
    "Subway Map",              // 0x36
    "Fonts",                   // 0x37
    "Scene Data",              // 0x38
    "SceneBip",                // 0x39
    "SceneDyn",                // 0x3A
    "SceneAnimKeyFrames",      // 0x3B
    "SceneAnimStreamTypes",    // 0x3C
    "SceneAnimationData",      // 0x3D
    "MoveKeyFrames",           // 0x3E
    "MoveData",                // 0x3F
    "PathData",                // 0x40
    "Object Device OID",       // 0x41
    "Object Device SID",       // 0x42
    "Scene List",              // 0x43
    "Character List",          // 0x44
    "Anim Range List",         // 0x45
    "Object List",             // 0x46
    "Preinstance Object",      // 0x47
    "Sound Command Data",      // 0x48
    "Sound Material Data",     // 0x49
    "Sound Anim Data",         // 0x4A
    "Light Glow Data",         // 0x4B
    "Particle Page",           // 0x4C
    "Particle Page Header",    // 0x4D
    "Anim List",               // 0x4E
    "Dependency List",         // 0x4F
    "ImportCars",              // 0x50
    "Subtitles",               // 0x51
    "Collision Checked",       // 0x52
    "Occluders",               // 0x53
};

} // namespace

std::string_view chunkTypeName(std::uint32_t type) {
    return type < kChunkTypeCount ? kNames[type] : std::string_view{};
}

} // namespace coney::chunk
