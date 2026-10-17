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

- `coney_engine` and `coney_tests` with the build type's flags; CTest names are the test case names.
- `coney_engine_sanitized` and `coney_tests_sanitized` with `-fsanitize=address,undefined`, every finding fatal;
  CTest names are `sanitized/` followed by the test case name.

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
