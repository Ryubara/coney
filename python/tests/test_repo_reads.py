# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the reads watch (repo_reads.py): which reads of the checkout count, and what a test without its marker is
told. The recorder is driven by hand with audit events, so no real file of the checkout is read here."""

import os
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
