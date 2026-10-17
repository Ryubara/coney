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
