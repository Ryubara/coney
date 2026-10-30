# The shader build

The GPU renderer ([Renderer](../engine/renderer.md#backend)) draws through SDL3's GPU API, which takes SPIR-V on
Vulkan, DXIL on Direct3D 12 and MSL on Metal. Its shaders are written once, in HLSL, and the build compiles them offline
to all three and embeds the results in the executable, so a player needs no shader compiler and no shader is compiled
while the game runs. This page is how that works, how to add a shader, what the build needs on each system and how it is
tested. The building of the rest of the engine is [Building and testing](building.md).

## The shaders {#shaders}

Every `*.hlsl` file in `src/platform/shaders/` is a shader, named `<name>.<stage>.hlsl` with the stage `vert`, `frag` or
`comp`; its entry point is `main`. `fullscreen.vert.hlsl` and `blit.frag.hlsl` (a full-screen triangle and a texture
copy) prove the build until the renderer's own shaders come. SDL's GPU API fixes where a shader's resources go, the same
on every backend: a vertex shader's samplers in `space0` and uniform buffers in `space1`, a fragment shader's in `space2`
and `space3` (`blit.frag.hlsl` shows it).

**To add a shader**, add the file. The folder's list is read at every configure and again when a file is added or
removed, and the build compiles the new file for every format; the tests below check it at once.

## What the build makes {#build}

For each file the build makes one step per format (`spv`, `dxil`, `msl`) and one for the resource counts SDL's GPU API
wants when it creates a shader (samplers, storage textures, storage buffers, uniform buffers, read from the SPIR-V by
the compiler's reflection), under the build folder's `src/platform/shaders/compiled/`. A shader that does not compile
for any one format fails the build with the compiler's own message. A last step writes the bytes into a C++ table, and
`coney_shaders`, a static library, holds it.

Its interface is `repo:src/platform/shaders/embedded_shaders.h`: `coney::shaders::allShaders()` and `findShader()`, each
shader's code per `Format`, its stage, its resource counts and `entryPoint(Format)`, which is `main` except in MSL, where
SPIRV-Cross renames it `main0` because `main` is not a legal Metal function name. The renderer links `coney_shaders` and
creates the shader from the format its device accepts. The top `CMakeLists.txt` adds the folder and links `coney_shaders`
into the game's executables, so every build of the game compiles the shaders and the linker keeps the ones the renderer
refers to; with `-DCONEY_SHADERS=OFF` (below) there is no `coney_shaders`.

## The compiler {#compiler}

The compiler is SDL_shadercross's command line (zlib), which compiles HLSL to SPIR-V and DXIL with DXC and translates
SPIR-V to MSL with SPIRV-Cross. None of this code is in the game: the tool compiles shaders, and only their output is
embedded. What `cmake/deps.cmake` pins:

| Part | Pin | Where it runs |
| --- | --- | --- |
| SDL_shadercross | its commit on the main branch (the project has no tags or releases) | everywhere |
| SPIRV-Cross | the `vulkan-sdk-1.4.350.1` tag, the commit shadercross's own submodule names | everywhere |
| DXC | Microsoft's v1.9.2602 release archive, checked against its SHA-256 | Windows, and Linux on x86-64 |
| DXC, built from source | the libsdl-org fork of the same release, as shadercross's submodule names it | every other system, macOS among them |

The licences are checked in [Renderer, Licences](../engine/renderer.md#licences).

**When it is built.** `cmake/coney_shaders.cmake` builds the compiler while the project configures, as a project of its
own (`cmake/shadertools/CMakeLists.txt`: always Release, with only SDL3's core, whatever the preset, so the project's
sanitizers, warnings and Debug flags never reach it) and installs it in the shared dependency folder beside the sources
([Shared dependency sources](building.md#shared-deps)), stamped with the pins and the project's hash. So it is built
once per machine and set of pins, a second configure in any worktree finds the stamp and builds nothing, and CI keeps it
in its dependency cache ([Caches](building.md#ci-caches), which also says what keys the entry). Without the shared folder
it is built inside the build folder. A lock keeps two configures from building it at once.

On Windows and Linux that takes a few minutes (SDL3's core and SPIRV-Cross; DXC comes as a binary). On macOS DXC is
compiled from source, which takes much longer the first time; the compiler cache covers it as far as its size allows.

**DXIL needs no extra binary.** Direct3D 12 loads only DXIL that carries the validator's hash. DXC signs it itself since
release 1.8.2505, so the pinned compiler needs no `dxil.dll` (Microsoft's own binary, under its own licence), and Coney
neither downloads nor loads it ([the finding](../engine/renderer.md#licences)). The tests below fail if a pin ever moves
to a compiler that does not sign.

**To update a pin**, change it in `cmake/deps.cmake` (the commit or the archive and its SHA-256), build, and run the
`[shaders]` tests. The pin lines are the stamp's version, so every machine builds the compiler once more. On Windows and
Linux SPIRV-Cross is fetched on its own with git's long paths on (`GIT_CONFIG core.longpaths=true`), because its test
files' names are longer than Windows' 260 characters and a submodule update would not have them.

**To build without it**, configure with `-DCONEY_SHADERS=OFF`: no compiler is fetched or built, no shader is compiled
and the tests below are not built. That is for a system where the compiler cannot be built; nothing in the game uses the
shaders yet. CI builds with the shaders on, on all three systems.

## Tests {#tests}

`repo:tests/platform/embedded_shaders_test.cpp` (tag `[shaders]`, its own small executable `coney_shader_tests`) and three
`coney.shaders.refuse_a_broken_shader_*` tests in `repo:tests/CMakeLists.txt` check, on every system CI builds:

- every file in the folder is in the table and nothing else is, each with code for every format. The build fails when a
  shader does not compile, so a table that holds them all is the proof that each one compiled for each format;
- the SPIR-V starts with its magic number, the DXIL is a container with a non-zero hash and a DXIL part, the MSL is Metal
  source with its entry point `main0`;
- the reflected resource counts of the two first shaders;
- the compile step refuses a shader that does not compile, for each format, and leaves no file that a later build would
  take for a finished one. SDL_shadercross creates its output before it compiles, so `cmake/coney_shader_compile.cmake`
  writes under another name and moves the file into place on success; the broken shader is
  `repo:tests/support/shaders/broken.frag.hlsl`, and a control shader that does compile keeps the check from passing
  because of a missing tool.
