# Engine

Coney's engine: a static library (`engine/src/`) and a Catch2 test suite (`engine/tests/`). Format readers live in
`src/formats/`, game behaviour in `src/game/`.

```sh
cmake -S engine -B build/engine -G Ninja
cmake --build build/engine
ctest --test-dir build/engine --output-on-failure
```

It needs CMake 3.20 or later, Ninja, GCC or Clang with C++23, and Catch2 3 (set `CXX` to pick the compiler). Every
target builds with warnings as errors.

## Build layout

`engine/CMakeLists.txt` lists the engine's sources in `CONEY_ENGINE_SOURCES` and the tests' in `CONEY_TEST_SOURCES`;
a new file is one line in one list. The engine and its tests are built twice:

| Targets | Built with | CTest names |
| --- | --- | --- |
| `coney_engine`, `coney_tests` | the build type's flags | the test case names |
| `coney_engine_sanitized`, `coney_tests_sanitized` | `-fsanitize=address,undefined`, any finding fatal | `sanitized/` then the name |

`CONEY_SANITIZED_TESTS` picks the second build: `AUTO` (the default) builds it when the compiler can link the
sanitizers and warns when it cannot, `ON` makes a missing sanitizer runtime an error, `OFF` leaves it out. Clang
needs its runtime package (`libclang-rt-<version>-dev` on Ubuntu) for the sanitized build.
