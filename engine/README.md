# Engine

Coney's engine: a static library (`engine/src/`), the `coney` executable (`engine/app/`) and a Catch2 test suite
(`engine/tests/`). Format readers live in `src/formats/`, game behaviour in `src/game/`, settings, logging, the clock,
the frame pacer and the main loop in `src/core/`, and everything that talks to SDL in `src/platform/` (no other code
includes SDL headers; the `engine_sdl_confined` test checks it).

```sh
cmake -S engine -B build/engine -G Ninja
cmake --build build/engine
ctest --test-dir build/engine --output-on-failure
```

It needs CMake 3.20 or later, Ninja, GCC or Clang with C++23, SDL 3 and Catch2 3 (set `CXX` to pick the compiler).

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
| Stop after N frames | `--frames N` | run until closed |

At `debug` it logs its tick and frame rates once a second.
