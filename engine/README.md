# Engine

Coney's engine: a static library (`engine/src/`), the `coney` executable (`engine/app/`) and a Catch2 test suite
(`engine/tests/`). Format readers live in `src/formats/`, game behaviour in `src/game/`, keyboard and gamepad mapping
in `src/input/`, settings, logging, the clock, the frame pacer and the main loop in `src/core/`, and everything that
talks to SDL in `src/platform/` (no other code includes SDL headers; the `engine_sdl_confined` test checks it).

```sh
cmake -S engine -B build/engine -G Ninja
cmake --build build/engine
ctest --test-dir build/engine --output-on-failure
```

It needs CMake 3.20 or later, Ninja, GCC or Clang with C++23, SDL 3 and Catch2 3 (set `CXX` to pick the compiler).
Every target builds with warnings as errors.

## Build layout

`engine/CMakeLists.txt` lists the engine's sources in `CONEY_ENGINE_SOURCES` and the tests' in `CONEY_TEST_SOURCES`,
the platform layer's in `CONEY_PLATFORM_SOURCES` and its tests' in `CONEY_PLATFORM_TEST_SOURCES`; a new file is one
line in one list. The engine and its tests are built twice:

- `coney_engine` and `coney_tests` with the build type's flags; CTest names are the test case names.
- `coney_engine_sanitized` and `coney_tests_sanitized` with `-fsanitize=address,undefined`, every finding fatal;
  CTest names are `sanitized/` followed by the test case name.

The platform layer, `coney` and `coney_platform_tests` are built once, without the sanitizers.

`CONEY_SANITIZED_TESTS` picks the second build: `AUTO` (the default) builds it when the compiler can link the
sanitizers and warns when it cannot, `ON` makes a missing sanitizer runtime an error, `OFF` leaves it out. Clang
needs its runtime package (`libclang-rt-<version>-dev` on Ubuntu) for the sanitized build.

## Chunk container reader

`src/formats/chunk_container.hpp` reads the containers that `research/pages/chunk-container/page.md` describes.
`parse_flat` reads a flat container (one resource), `parse_pack` a grouped one, and `read_resource` one resource
from the front of a buffer. Each returns a `Result`: the parsed structure, or a `ParseError` giving the kind of
fault and the byte offset of the header at fault. `describe` turns either into text for logs.

Nothing is copied: every chunk's payload is a span into the bytes that were parsed, so the caller keeps those
bytes alive while it uses the result. The reader rejects anything the page's layout does not allow, including
counts below the page's minimums (a resource of no chunks, a pack of fewer than 2 resources).

## Running coney

`build/engine/coney` opens a window, clears it every frame and runs the main loop until the window is closed. In a
container, run it with `SDL_VIDEODRIVER=offscreen`. Its settings (`src/core/settings.hpp`) and their options:

| Setting | Option | Default |
| --- | --- | --- |
| Window title, width, height | `--title`, `--width`, `--height` | `Coney`, 1280 x 720 |
| Logic ticks a second | none | 30, the game's rate |
| Most catch-up ticks in one frame | `--max-catch-up N` | 5 |
| Log level | `--log-level debug\|info\|warning\|error\|off` | `info` |
| Log file, besides standard error | `--log-file PATH` | none |
| Key and gamepad bindings | `--bindings PATH` | the defaults below |
| Write the bindings in use to a file and stop | `--write-bindings PATH` | none |
| Stop after N frames | `--frames N` | run until closed |

At `debug` it logs its tick and frame rates once a second.

## Input bindings

Players 1 and 2 each have one controller with the PS2 pad's buttons and two sticks. A gamepad takes the first free
player; the keyboard drives player 1 while no gamepad does. The defaults (`default_bindings` in
`src/input/bindings.cpp`):

| Control | Keyboard | Gamepad (by position) |
| --- | --- | --- |
| cross, circle, square, triangle | Space, C, F, E | south, east, west, north |
| L1, R1, L2, R2 | Q, R, Left Shift, Tab | shoulders, then triggers |
| L3, R3, SELECT, START | Z, X, Backspace, Return or Escape | stick clicks, back, start |
| d-pad | arrow keys | d-pad |
| left stick, right stick | W A S D, I J K L | left stick, right stick |

To change them, write the defaults with `coney --write-bindings bindings.ini`, edit the file and run
`coney --bindings bindings.ini`; a file needs only the controls it changes. Its format is described in
`src/input/bindings.hpp`.
