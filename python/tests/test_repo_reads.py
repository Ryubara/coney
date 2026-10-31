# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the reads watch (repo_reads.py): which reads of the checkout count, and what a test without its marker is
told. The recorder is driven by hand with audit events, so no real file of the checkout is read here."""

import contextlib
import os
import shutil
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

import pytest
import repo_reads

SCOPE = repo_reads.load_scope()


def recorder() -> repo_reads.Recorder:
    """A recorder of this checkout, not installed as a hook (the tests call `hook` themselves)."""
    return repo_reads.Recorder(SCOPE)


def inside(*parts: str) -> str:
    """A path inside the checkout."""
    return str(repo_reads.REPO.joinpath(*parts))


def test_the_scope_is_the_conventions_one() -> None:
    assert SCOPE.scoped == ("docs_only", "engine")
    assert SCOPE.class_of("docs/guides/testing.md") == "docs_only"
    assert SCOPE.class_of("src/core/a.cpp") == "engine"
    # a change there runs the whole suite anyway, so reading it needs no marker
    for whole in ("python/src/coney_tools/cli.py", ".github/conventions.yml", ".claude/hooks/a.py", "tools/a.ps1", ""):
        assert SCOPE.class_of(whole) is None


def test_a_watched_test_records_its_scoped_reads_and_nothing_else() -> None:
    watch = recorder()
    watch.hook("open", (inside("docs", "guides", "testing.md"), "r", 0))  # not watching yet
    watch.start()
    watch.hook("open", (inside("docs", "guides", "testing.md"), "r", 0))
    watch.hook("os.scandir", (inside("src", "ai"),))
    watch.hook("open", (inside("python", "src", "coney_tools", "cli.py"), "r", 0))
    watch.hook("open", (str(Path(os.sep, "elsewhere", "docs", "a.md")), "r", 0))
    watch.hook("open", (3, "r", 0))  # a file descriptor
    watch.hook("compile", (b"x", "docs/a.md"))  # not a read
    assert watch.stop() == {"docs_only": {"docs/guides/testing.md"}, "engine": {"src/ai"}}
    assert watch.stop() == {}


def test_a_relative_path_is_read_against_the_current_folder(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(repo_reads.REPO)
    watch = recorder()
    watch.start()
    watch.hook("open", ("research/missions.yaml", "r", 0))
    assert watch.stop() == {"docs_only": {"research/missions.yaml"}}


def test_a_module_loaded_from_its_bytecode_cache_counts_as_its_source() -> None:
    watch = recorder()
    watch.start()
    watch.hook("open", (inside("__pycache__", "mkdocs_hooks.cpython-314.pyc"), "rb", 0))
    assert watch.stop() == {"docs_only": {"mkdocs_hooks.py"}}


def test_what_a_module_reads_while_imported_counts_for_each_of_its_tests() -> None:
    watch = recorder()
    watch.start()
    watch.hook("open", (inside("CMakeLists.txt"), "r", 0))
    watch.by_module["tests/test_x.py"] = watch.stop()
    watch.start("tests/test_x.py")
    assert watch.stop() == {"engine": {"CMakeLists.txt"}}
    watch.start("tests/test_y.py")
    assert watch.stop() == {}


def test_a_child_process_in_the_checkout_is_noted_but_never_fails_a_test() -> None:
    watch = recorder()
    watch.start()
    watch.hook("subprocess.Popen", ("git", ["git", "status"], str(repo_reads.REPO), None))
    reads = watch.stop()
    assert repo_reads.SPAWNED in reads
    assert repo_reads.missing(reads, set()) == {}


def test_a_test_that_reads_a_class_its_marker_does_not_name_is_told_the_marker_to_add() -> None:
    reads = {"docs_only": {"docs/b.md", "docs/a.md"}, "engine": {"src/a.cpp"}}
    named = repo_reads.declared([("engine",)])
    unnamed = repo_reads.missing(reads, named)
    assert unnamed == {"docs_only": ["docs/a.md", "docs/b.md"]}
    message = repo_reads.failure("tests/test_x.py::test_y", unnamed)
    assert message.startswith("tests/test_x.py::test_y reads the checkout's docs_only: docs/a.md, docs/b.md")
    assert '@pytest.mark.reads("docs_only")' in message and "pytestmark" in message
    assert repo_reads.missing(reads, repo_reads.declared([("docs_only", "engine")])) == {}


def test_the_log_line_names_the_test_and_its_reads() -> None:
    line = repo_reads.log_line("t::u", {"engine": {"src/b.cpp", "src/a.cpp"}})
    assert line == '{"test": "t::u", "reads": {"engine": ["src/a.cpp", "src/b.cpp"]}}'


def test_the_marker_is_registered_with_the_scoped_classes(pytestconfig: pytest.Config) -> None:
    [line] = [line for line in pytestconfig.getini("markers") if line.startswith("reads(")]
    assert "docs_only, engine" in line


def test_a_top_folder_listing_is_classed_like_the_folder_with_its_slash() -> None:
    for folder in ("docs", "src", "tests", "research", "assets", "cmake"):
        assert SCOPE.class_of(folder) == SCOPE.class_of(folder + "/") is not None
        watch = recorder()
        watch.start()
        watch.hook("os.listdir", (inside(folder),))
        assert watch.stop() == {SCOPE.class_of(folder): {folder}}
    assert SCOPE.class_of("docs/guides") == "docs_only"
    assert SCOPE.class_of("python") is None


class Wrapped:
    """The stat calls wrapped for one test, with a recorder that hears them for a checkout in `root` (a temporary
    folder, so no real file of this checkout is looked at)."""

    def __init__(self, root: Path) -> None:
        self.watch = repo_reads.Recorder(SCOPE, root=os.path.normcase(str(root)))
        self.replaced = repo_reads.wrap_stat_calls(self.watch)

    def __enter__(self) -> repo_reads.Recorder:
        self.watch.start()
        return self.watch

    def __exit__(self, *_: object) -> None:
        repo_reads.unwrap_stat_calls(self.replaced)


def test_a_stat_call_on_a_scoped_file_is_a_read_of_it(tmp_path: Path) -> None:
    # every way a test can check a file without opening it, which CPython raises no audit event for
    present = tmp_path / "a.txt"
    present.write_text("x", encoding="utf-8")
    page = tmp_path / "docs" / "guides" / "testing.md"
    source = tmp_path / "src" / "core" / "a.cpp"
    calls: dict[str, Callable[[Path], object]] = {
        "Path.exists": lambda target: target.exists(),
        "Path.is_file": lambda target: target.is_file(),
        "Path.is_dir": lambda target: target.is_dir(),
        "Path.stat": lambda target: target.stat(),
        "os.stat": lambda target: os.stat(target),  # noqa: PTH116
        "os.lstat": lambda target: os.lstat(target),
        "os.access": lambda target: os.access(target, os.R_OK),
        "os.path.exists": lambda target: os.path.exists(target),  # noqa: PTH110
        "os.path.isfile": lambda target: os.path.isfile(target),  # noqa: PTH113
        "os.path.isdir": lambda target: os.path.isdir(target),  # noqa: PTH112
        "os.path.getsize": lambda target: os.path.getsize(target),  # noqa: PTH202
        "os.path.getmtime": lambda target: os.path.getmtime(target),  # noqa: PTH204
    }
    for name, call in calls.items():
        for target, kind in ((page, "docs_only"), (source, "engine")):
            with Wrapped(tmp_path) as watch:
                with contextlib.suppress(OSError):  # the file need not exist: the look is what counts
                    call(target)
                assert watch.stop() == {kind: {target.relative_to(tmp_path).as_posix()}}, name
        with Wrapped(tmp_path) as watch:
            call(present)
            assert watch.stop() == {}, name


def test_wrapped_stat_calls_give_the_same_result_and_error_and_are_put_back(tmp_path: Path) -> None:
    before = (os.stat, os.path.exists, os.path.isfile)
    present = tmp_path / "a.txt"
    present.write_text("abc", encoding="utf-8")
    with Wrapped(tmp_path) as watch:
        assert os.stat(present).st_size == 3  # noqa: PTH116
        assert os.path.getsize(present) == 3  # noqa: PTH202
        assert not os.path.exists(tmp_path / "missing")  # noqa: PTH110
        assert os.path.isfile(path=present)  # noqa: PTH113
        with pytest.raises(FileNotFoundError):
            os.stat(tmp_path / "missing")  # noqa: PTH116
        watch.stop()
    assert (os.stat, os.path.exists, os.path.isfile) == before


def test_a_stat_call_is_not_noted_while_nothing_is_watched(tmp_path: Path) -> None:
    watch = repo_reads.Recorder(SCOPE, root=os.path.normcase(str(tmp_path)))
    replaced = repo_reads.wrap_stat_calls(watch)
    try:
        (tmp_path / "docs").exists()
        assert watch.current is None
    finally:
        repo_reads.unwrap_stat_calls(replaced)


def test_a_name_relative_to_a_directory_descriptor_is_not_a_read_of_the_current_folder(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """`shutil.rmtree` on Linux and macOS stats and opens each entry of a tree by its bare name and a dir_fd; read
    against the current folder (a checkout's root) `docs` was the checkout's docs, and CI failed 7 tests for it."""
    monkeypatch.chdir(tmp_path)
    opened: list[object] = []

    def fake_open(path: object, flags: int = 0, *, dir_fd: int | None = None) -> int:
        opened.append(path)
        watch.hook("open", (path, None, flags))  # the audit event the real os.open raises inside the call
        return 3

    watch = repo_reads.Recorder(SCOPE, root=os.path.normcase(str(tmp_path)))
    watch.start()
    muted = repo_reads._muting(watch, fake_open)
    noting = repo_reads._noting(watch, lambda path, **_: None)
    muted("docs", os.O_RDONLY, dir_fd=5)
    noting("research", dir_fd=5, follow_symlinks=False)
    assert watch.relative_to_fd == 0
    assert watch.current == {}
    # the same names without a dir_fd are read against the current folder, which is the checkout here
    muted("src", os.O_RDONLY)
    noting("research")
    assert watch.stop() == {"engine": {"src"}, "docs_only": {"research"}}
    assert opened == ["docs", "src"]


def test_a_muted_call_that_fails_still_ends_its_muting() -> None:
    watch = recorder()

    def failing(path: object, flags: int = 0, *, dir_fd: int | None = None) -> int:
        raise FileNotFoundError(path)

    muted = repo_reads._muting(watch, failing)
    with pytest.raises(FileNotFoundError):
        muted("docs", dir_fd=5)
    assert watch.relative_to_fd == 0


def test_removing_a_tree_with_the_checkouts_folder_names_reads_nothing_of_the_checkout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # Run with the checkout as the current folder, as CI does. On Linux and macOS `rmtree` walks the tree with
    # dir_fd and bare names (`docs`, `research`, `src`), which the guard of this very run would fail at teardown if it
    # read them against the checkout; Windows removes by full path, so only there is this check empty.
    monkeypatch.chdir(repo_reads.REPO)
    tree = tmp_path / "copy"
    for folder in ("docs/progress", "research/functions", "src/core"):
        (tree / folder).mkdir(parents=True)
        (tree / folder / "a.txt").write_text("x", encoding="utf-8")
    shutil.rmtree(tree)
    assert not tree.exists()


def test_the_conftest_fails_an_unmarked_test_at_teardown_and_selects_by_marker(tmp_path: Path) -> None:
    # The conftest's hooks, run for real in a child pytest over a throwaway test file: the glue between the recorder and
    # pytest is what keeps a scoped run from skipping an unmarked test, and the helpers above do not cover it.
    here = Path(__file__).resolve().parent
    (tmp_path / "conftest.py").write_text((here / "conftest.py").read_text(encoding="utf-8"), encoding="utf-8")
    (tmp_path / "pytest.ini").write_text("[pytest]\n", encoding="utf-8")
    (tmp_path / "test_reader.py").write_text(
        "import pytest\n"
        "from pathlib import Path\n"
        f"DOC = Path({str(repo_reads.REPO / 'docs' / 'guides' / 'testing.md')!r})\n"
        "def test_unmarked():\n"
        "    DOC.read_bytes()\n"
        '@pytest.mark.reads("docs_only")\n'
        "def test_marked():\n"
        "    DOC.read_bytes()\n"
        "def test_unrelated():\n"
        "    pass\n",
        encoding="utf-8",
    )
    env = {**os.environ, "PYTHONPATH": os.pathsep.join([str(here), os.environ.get("PYTHONPATH", "")])}
    env.pop("CONEY_TEST_READS_LOG", None)

    def run(*args: str) -> tuple[int, str]:
        done = subprocess.run(
            [sys.executable, "-m", "pytest", "-p", "no:cacheprovider", "-q", *args],
            cwd=tmp_path,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        return done.returncode, done.stdout + done.stderr

    code, out = run()
    assert code != 0
    assert "test_unmarked" in out and "reads the checkout's docs_only" in out
    assert "3 passed, 1 error" in out  # the unmarked reader is an error of its teardown
    code, out = run("--reads", "docs_only")
    assert code == 0 and "1 passed, 2 deselected" in out  # only test_marked is kept
