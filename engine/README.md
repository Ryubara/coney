# Engine

Coney's engine: a static library (`engine/src/`) and a Catch2 test suite (`engine/tests/`). Format readers live in
`src/formats/`, game behaviour in `src/game/`.

```sh
cmake -S engine -B build/engine -G Ninja
cmake --build build/engine
ctest --test-dir build/engine --output-on-failure
```

It needs CMake 3.20 or later, Ninja, GCC or Clang with C++23, and Catch2 3 (set `CXX` to pick the compiler).
