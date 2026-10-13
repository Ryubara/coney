# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the tests that need conventions: the real file's data, with changes, and a made-up checkout holding it."""

import copy
from pathlib import Path
from typing import Any

import yaml

from coney_tools.conventions import CONVENTIONS_FILE, Conventions, parse_conventions

#: The repository root of this checkout.
REPO = Path(__file__).resolve().parents[2]
#: Passed to `changed` to remove a key.
REMOVE = object()


def real_data() -> dict[str, Any]:
    """A fresh copy of the YAML data of this checkout's `.github/conventions.yml`."""
    data: dict[str, Any] = yaml.safe_load((REPO / CONVENTIONS_FILE).read_text(encoding="utf-8"))
    return copy.deepcopy(data)


def changed(*changes: tuple[str, object], data: dict[str, Any] | None = None) -> dict[str, Any]:
    """The real data with each `("a.b.c", value)` applied; `REMOVE` as the value deletes the key.

    A path segment is a mapping key, or a list index when it is a number.
    """
    result = data if data is not None else real_data()
    for path, value in changes:
        *parents, last = path.split(".")
        node: Any = result
        for part in parents:
            node = node[int(part)] if isinstance(node, list) else node[part]
        key: Any = int(last) if isinstance(node, list) else last
        if value is REMOVE:
            del node[key]
        else:
            node[key] = value
    return result


def conventions_with(*changes: tuple[str, object]) -> Conventions:
    """The real conventions with the changes applied (validated like the file is)."""
    return parse_conventions(changed(*changes))


def write_checkout(root: Path, data: dict[str, Any] | None = None) -> Path:
    """Make `root` a checkout holding `data` (default: the real data) as its conventions file; returns `root`."""
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / ".github").mkdir(exist_ok=True)
    (root / CONVENTIONS_FILE).write_text(yaml.safe_dump(data if data is not None else real_data()), encoding="utf-8")
    return root


#: Paths of this repository and the CI scope class each falls in (.github/scripts/ci-scope.sh sorts them the same way:
#: test_audit_files.py runs the script on these and checks it agrees).
CI_CLASSES = [
    ("docs/guides/building.md", "docs_only"),
    ("research/traces/a.csv", "docs_only"),
    ("README.md", "docs_only"),
    (".github/pull_request_template.md", "docs_only"),
    (".github/conventions.yml", "docs_only"),
    (".github/workflows/docs.yml", "docs_only"),
    (".github/workflows/pr.yml", "docs_only"),
    (".github/workflows/hygiene.yml", "docs_only"),
    (".github/workflows/issue-status.yml", "docs_only"),
    ("mkdocs.yml", "docs_only"),
    (".markdownlint-cli2.yaml", "docs_only"),
    ("python/src/coney_tools/cli.py", "tools_only"),
    ("python/README.md", "tools_only"),
    ("python/uv.lock", "tools_only"),
    (".claude/hooks/build_guard.py", "tools_only"),
    (".claude/settings.json", "tools_only"),
    ("tools/start-ghidra-mcp.ps1", "tools_only"),
    ("bootstrap.sh", "tools_only"),
    ("bootstrap.ps1", "tools_only"),
    ("coney.local.example.toml", "tools_only"),
    (".mcp.json", "tools_only"),
    (".pre-commit-config.yaml", "tools_only"),
    (".github/ISSUE_TEMPLATE/bug.yml", "tools_only"),
    (".github/workflows/python.yml", "tools_only"),
    ("src/core/memory.cpp", "engine"),
    ("src/README.md", "engine"),
    ("tests/core/memory_test.cpp", "engine"),
    ("tests/support/play_walk.txt", "engine"),
    ("assets/sandbox/License.txt", "engine"),
    ("cmake/deps.cmake", "engine"),
    ("CMakeLists.txt", "engine"),
    ("CMakePresets.json", "engine"),
    (".clang-format", "engine"),
    (".clang-tidy", "engine"),
    (".github/actions/ccache/action.yml", "engine"),
    (".github/workflows/build.yml", "engine"),
    (".github/scripts/ci-scope.sh", "unknown"),
    (".github/scripts/tidy-files.sh", "unknown"),
    (".github/workflows/new.yml", "unknown"),
    (".gitattributes", "unknown"),
    (".editorconfig", "unknown"),
    ("LICENSE", "unknown"),
    ("sub/README.md", "unknown"),
    ("docs.md/x", "unknown"),
    ("pythonic/a.py", "unknown"),
    ("mkdocs.yml.bak", "unknown"),
]
