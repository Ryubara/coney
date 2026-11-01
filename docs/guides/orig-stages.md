# The orig study, compare, verify and fails-first commands {#orig-stages}

These four `orig` commands are the study-and-compare stage; the rest of the group, and the usage of each subcommand,
is on [the fidelity page](coney-tools-fidelity.md#orig).

The commands of the study-and-compare stage ([Stamps, studies and pairs](stamps.md)). Each writes into the task's
folder in the scratch folder (the task is `--task`, else `CONEY_TASK`, else the branch) and appends a stamp when its
step ends. Exit codes: 0 done, 1 refused (the output says what to fix), 2 it cannot start, 3 no ghidra-mcp answers.

```sh
uv run --project python coney-tools orig study ADDR [--task T] [--url URL] [--recut] [--print]
uv run --project python coney-tools orig verify ADDR [--task T] [--url URL]
uv run --project python coney-tools orig compare ADDR [--task T] [--base REF] [--coney-function KEY] [--carry] [--show]
uv run --project python coney-tools orig fails-first [--keeps] --test NAME [--test NAME ...] --test-commit SHA [--fix-commit SHA] [--preset P] --agent ID
uv run --project python coney-tools orig fails-first --none REASON
uv run --project python coney-tools orig fails-first --accept-change REASON --test NAME [--test NAME ...] --test-commit SHA [--fix-commit SHA]
```

* `study` reads the function at ADDR from the running ghidra-mcp (reading endpoints only), saves the decompile, callers,
  callees and globals in scratch (never committed), numbers the statements, cuts the pieces and appends `studied`. It
  writes nothing when Ghidra is down. It ends with the lines of `research where` for the address (the owning page, the
  function database entry, the pages that cite it), or one line saying the lookup is not available.
  [Details](stamps.md#study), [the lookup](research-where.md#study)
* `verify` re-reads the function and says whether the saved study is `fresh`, `renamed` (a one-to-one rename only) or
  `stale`; it exits 1 when stale.
* `compare` writes `mapping/<addr>.toml` with a blank pair per piece on its first run; later runs show the decompile beside
  the Coney function, check the pairs, the committed Coney file and `orig check-copy`, and append `compared` when all of
  it holds. A refusal writes no stamp. [Details](stamps.md#compare)
* `fails-first` runs the named tests through `build --test -R` on the test commit (they must fail) and the fix commit (they
  must pass) and appends a `fails-first` stamp per test. With `--keeps` (a test that guards a refactor, a performance
  change or behaviour that already matches) the test must pass on both and the stamp's result is `keeps`. With
  `--accept-change` the coordinator records its yes to a test that had to change between the two commits, and nothing
  runs. [Details](stamps.md#fails-first), [keeps](stamps.md#keeps)

Every option:

| Command | Option | What it does |
| --- | --- | --- |
| all four | `--task T` | The task whose scratch folder and stamp log are used (default: `CONEY_TASK`, else the branch) |
| `study`, `verify`, `compare` | `ADDR` | The original function's entry point, such as `0x00230d00` (another address is refused, naming the start) |
| `study`, `verify` | `--url URL` | The ghidra-mcp server (default `http://127.0.0.1:8090`) |
| `study` | `--recut` | Cut the saved decompile again without Ghidra; refused unless the saved files and the function's name, program and signature match the newest `studied` stamp |
| `study` | `--print` | Also print the numbered decompile |
| `compare` | `--base REF` | What the copy check measures the Coney change from (default `origin/main`) |
| `compare` | `--coney-function KEY` | Which Coney function to compare, when several carry `@orig ADDR` |
| `compare` | `--carry` | Bring the pairs forward onto a changed or renamed study |
| `compare` | `--show` | Print the side-by-side view |
| `fails-first` | `--test NAME` | A whole test name as ctest lists it, found as `TEST_CASE("NAME"` in the test commit's files (adjacent literals are joined, comments between them dropped, escapes unescaped; a `SECTION` name or any other quoted string is refused; repeatable) |
| `fails-first` | `--test-commit SHA` | The commit that adds the test, and only the stubs it needs to compile |
| `fails-first` | `--fix-commit SHA` | The commit that makes it pass (default `HEAD`) |
| `fails-first` | `--preset P` | The CMake preset the tests are built with (default `dev`) |
| `fails-first` | `--agent ID` | Your id for the build slots, as `build --agent` |
| `fails-first` | `--keeps` | The tests guard a refactor or behaviour Coney already has: each must pass on the test commit and on the fix, unchanged, and is stamped `keeps` (not with `--none` or `--accept-change`); the test commit is never the fix commit ([Keeps](stamps.md#keeps)) |
| `fails-first` | `--none REASON` | Run nothing and stamp that the task has no such test (one line, at most 200 characters, not with `--test`) |
| `fails-first` | `--accept-change REASON` | The coordinator only: run nothing and record that the named tests may change between `--test-commit` and `--fix-commit`, with the reason (one line, at most 200 characters); the engineer's next run of the same names and commits is then allowed ([A changed test](stamps.md#changed-test)). The hook lets the coordinator run `orig fails-first` only in this form, alone: not with `--none` or `--keeps` (any prefix of them), which would stamp, and every other `orig fails-first` stays refused to it ([roles](agents.md#roles)) |
