# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools workspace init`: set up the local workspace of docs/guides/workspace.md on a new machine.

It creates the sibling folders, clones the repository when run outside one, writes `coney.local.toml` from the
example, checks the prerequisites and, only when asked, fetches the research tools at pinned versions
(workspace_fetch). Every step is skipped when its result is already there, so running it again is safe, and
`--dry-run` prints the steps without doing any. It never fetches or asks for game files or firmware: the player
supplies their own disc (LEGAL.md). See docs/guides/workspace.md#bootstrap.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from functools import partial
from pathlib import Path

from coney_tools import compiler_cache, workspace_fetch
from coney_tools.config import EXAMPLE_FILE, LOCAL_FILE, ConfigError, find_repo_root, load_config, main_checkout
from coney_tools.workspace_checks import Check, check_prerequisites, run_command

#: The folders of the workspace, relative to its root (docs/guides/workspace.md#the-layout).
WORKSPACE_DIRS: tuple[str, ...] = ("repos", "worktrees", "scratch", "cache", "game", "emulators", "tools", "ghidra")

#: Where the checkout sits in the workspace; coney.local.toml's `../../` defaults rely on this depth.
CHECKOUT = Path("repos") / "coney"

_EMPTY_KEY = re.compile(r'^(?P<head>(?P<key>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*)""', re.MULTILINE)


@dataclass(frozen=True)
class Layout:
    """Where the workspace and its main checkout are."""

    workspace: Path
    checkout: Path
    #: True when the command runs from inside the checkout (or one of its worktrees).
    inside: bool


@dataclass(frozen=True)
class Step:
    """One thing `init` does, or found already done (`action` None)."""

    verb: str
    target: str
    action: Callable[[], None] | None


def find_layout(cwd: Path, workspace: Path | None) -> Layout:
    """The workspace to set up.

    With `workspace` given, that folder. Inside a checkout or worktree, the workspace holding the main checkout,
    which must sit in its `repos/` folder. Anywhere else, `cwd` itself. Raises ConfigError for a checkout outside a
    workspace's `repos/`.
    """
    if workspace is not None:
        root = workspace.resolve()
        return Layout(root, root / CHECKOUT, (root / CHECKOUT / EXAMPLE_FILE).is_file())
    try:
        checkout = find_repo_root(cwd)
    except ConfigError:
        root = cwd.resolve()
        return Layout(root, root / CHECKOUT, False)
    main = main_checkout(checkout).resolve()
    if main.parent.name != "repos":
        raise ConfigError(
            f"{main} is not in a workspace's repos/ folder (docs/guides/workspace.md); "
            "pass --workspace DIR to set up a workspace elsewhere"
        )
    return Layout(main.parent.parent, main, True)


def fill_config(example: str, values: Mapping[str, str]) -> str:
    """`example` (the text of coney.local.example.toml) with each empty key that `values` names set to its value.

    Keys that already have a value, and the comments, are kept. Values are written as TOML literal strings, so
    Windows paths need no escapes; one a literal string cannot hold (a `'`) becomes an escaped basic string.
    """

    def replace(match: re.Match[str]) -> str:
        value = values.get(match.group("key"))
        return f"{match.group('head')}{_toml_string(value)}" if value else match.group(0)

    return _EMPTY_KEY.sub(replace, example)


def _toml_string(value: str) -> str:
    """`value` as a TOML string: literal when it can be, else basic with `\\` and `"` escaped."""
    if "'" not in value and not any(ord(char) < 0x20 or char == "\x7f" for char in value):
        return f"'{value}'"
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    escaped = "".join(f"\\u{ord(char):04x}" if ord(char) < 0x20 or char == "\x7f" else char for char in escaped)
    return f'"{escaped}"'


def gather_checks(jdk_home: Path | None) -> list[Check]:
    """The prerequisite checks for this machine; `jdk_home` is the JDK coney.local.toml names, if any."""
    return check_prerequisites(workspace_fetch.platform_name(), shutil.which, run_command, os.environ, jdk_home)


def ccache_check(checkout: Path) -> Check:
    """The optional check of the compiler cache `coney-tools build` uses (compiler_cache.py): the engine builds
    without it, only slower."""
    hint = "coney-tools workspace init --fetch ccache" if sys.platform == "win32" else "apt or brew install ccache"
    if not (checkout / EXAMPLE_FILE).is_file():
        return Check("ccache (compiler cache)", False, "no checkout yet", hint, False)
    cache = compiler_cache.find(checkout)
    detail = str(cache.program) if cache.program is not None else cache.missing
    return Check("ccache (compiler cache)", cache.program is not None, detail, hint, False)


def _relative(layout: Layout, path: Path) -> str:
    """`path` as shown to the user: relative to the workspace when inside it."""
    return path.relative_to(layout.workspace).as_posix() if path.is_relative_to(layout.workspace) else str(path)


def _clone(url: str, dest: Path) -> None:
    """Clone the repository at `url` into `dest`."""
    done = subprocess.run(["git", "clone", "--", url, str(dest)], capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise ConfigError(f"git clone {url}: {done.stderr.strip()}")


def _write_config(checkout: Path, values: Mapping[str, str]) -> None:
    """Write the checkout's coney.local.toml from its example, filled with `values`."""
    example = (checkout / EXAMPLE_FILE).read_text(encoding="utf-8")
    (checkout / LOCAL_FILE).write_text(fill_config(example, values), encoding="utf-8")


def plan(layout: Layout, repo_url: str | None, fetch: Sequence[str], platform: str, jdk: Path | None) -> list[Step]:
    """The steps of `init`, in order; each step already done has no action."""
    steps: list[Step] = []
    for name in WORKSPACE_DIRS:
        folder = layout.workspace / name
        steps.append(
            Step("exists", f"{name}/", None)
            if folder.is_dir()
            else Step("create", f"{name}/", partial(folder.mkdir, parents=True, exist_ok=True))
        )

    checkout = layout.checkout
    if (checkout / ".git").exists():
        steps.append(Step("exists", _relative(layout, checkout), None))
    elif repo_url is None:
        raise ConfigError(
            f"not inside a Coney checkout and {_relative(layout, checkout)} does not exist: pass --repo-url URL "
            "(the repository's address, as the README gives it) to clone it"
        )
    else:
        steps.append(Step("clone", f"{repo_url} -> {_relative(layout, checkout)}", lambda: _clone(repo_url, checkout)))

    cache = layout.workspace / "scratch" / "downloads"
    for tool in fetch:
        dest = workspace_fetch.tool_destination(tool, layout.workspace)
        if tool in workspace_fetch.WINDOWS_ONLY and platform != "windows":
            steps.append(Step("skip", f"{tool}: Windows only, no build of it for {platform}", None))
            continue
        if workspace_fetch.packaged(tool, platform):
            steps.append(Step("skip", f"{tool}: install it with your package manager (docs/guides/building.md)", None))
            continue
        if dest.exists():
            steps.append(Step("exists", _relative(layout, dest), None))
            continue
        action = partial(workspace_fetch.fetch_tool, tool, dest, platform, cache, workspace_fetch.download_url)
        steps.append(Step("fetch", f"{workspace_fetch.describe(tool, platform)} -> {_relative(layout, dest)}", action))

    # coney.local.toml's paths are relative to the checkout, two levels below the workspace.
    values: dict[str, str] = {}
    if jdk is not None:
        values["jdk_home"] = jdk.as_posix()
    for key, tool in (("ghidra_mcp_repo", "ghidra-mcp"), ("pcsx2_mcp_dir", "pcsx2-mcp")):
        if tool in fetch or workspace_fetch.tool_destination(tool, layout.workspace).exists():
            values[key] = "../../" + workspace_fetch.tool_destination(tool, Path()).as_posix()
    # The fork's downloaded release, else (winning over it) the newest build made from the clone: either is both the
    # copies' PCSX2 and the MCP server.
    downloaded = workspace_fetch.tool_destination("pcsx2-mcp-release", layout.workspace)
    if ("pcsx2-mcp-release" in fetch and platform == "windows") or downloaded.exists():
        values["pcsx2_mcp_dir"] = "../../" + downloaded.relative_to(layout.workspace).as_posix()
    release = workspace_fetch.newest_release(workspace_fetch.tool_destination("pcsx2-mcp", layout.workspace))
    if release is not None:
        values["pcsx2_mcp_dir"] = "../../" + release.relative_to(layout.workspace).as_posix()
    config = checkout / LOCAL_FILE
    if config.is_file():
        steps.append(Step("keep", f"{_relative(layout, config)} (already there)", None))
    else:
        steps.append(Step("write", _relative(layout, config), lambda: _write_config(checkout, values)))
    return steps


def _configured_jdk(layout: Layout) -> Path | None:
    """The JDK an existing coney.local.toml names, if it can be read."""
    if not (layout.checkout / LOCAL_FILE).is_file():
        return None
    try:
        return load_config(layout.checkout).paths["jdk_home"]
    except ConfigError:
        return None


def _print_checks(checks: Sequence[Check]) -> None:
    """The prerequisites as a list, with what to do for each missing one."""
    print("Prerequisites:")
    for check in checks:
        status = "ok" if check.ok else "missing" if check.required else "optional"
        line = f"  {status:<9}{check.name}: {check.detail}"
        print(line if check.ok else f"{line} -> {check.hint}")


def _print_next(layout: Layout, fetch: Sequence[str]) -> None:
    """What the user does by hand: the parts Coney never does for them."""
    print("Next:")
    print(f"  - Put your own disc image in {layout.workspace / 'game'}; Coney never downloads game files.")
    print(
        f"  - Build and test from {layout.checkout}: docs/guides/building.md. Install the hooks: uvx pre-commit install"
    )
    downloaded = workspace_fetch.tool_destination("pcsx2-mcp-release", layout.workspace)
    if ("pcsx2-mcp-release" in fetch and workspace_fetch.platform_name() == "windows") or downloaded.exists():
        print(
            "  - Run `coney-tools pcsx2 setup-copies` to make the PCSX2 copies from the fork's release; each copy "
            "needs a BIOS dumped from your own console, and Coney never downloads one "
            "(docs/guides/workspace.md#pcsx2-copies)."
        )
    if "pcsx2-mcp" in fetch:
        print(
            "  - Only to change the fork: build the PCSX2-MCP clone as its README says into its release/ folder, "
            "point pcsx2_mcp_dir at the build and run `coney-tools pcsx2 setup-copies` "
            "(docs/guides/workspace.md#pcsx2-copies)."
        )
    if "ghidra" in fetch:
        print(
            "  - Compile the Emotion Engine language and import SLUS_212.15 with our function names: "
            "uv run --project python coney-tools ghidra setup (docs/guides/ghidra.md#own-project)."
        )
    if "ghidra-mcp" in fetch:
        print(
            "  - Build ghidra-mcp's headless server (target/GhidraMCPHeadless.jar) with Maven 3.9+: "
            "docs/guides/ghidra.md#own-ghidra-mcp."
        )


def run_init(cwd: Path, workspace: Path | None, repo_url: str | None, fetch: Sequence[str], dry_run: bool) -> int:
    """Set up the workspace; 1 when a required prerequisite is missing (after doing the rest), else 0."""
    layout = find_layout(cwd, workspace)
    wanted = set(workspace_fetch.TOOLS) if "all" in fetch else set(fetch)
    tools = [tool for tool in workspace_fetch.TOOLS if tool in wanted]
    checks = gather_checks(_configured_jdk(layout))
    jdk = next((check.path for check in checks if check.name == "JDK 21" and check.ok), None)
    steps = plan(layout, repo_url, tools, workspace_fetch.platform_name(), jdk)

    print(f"Workspace: {layout.workspace}" + (" (dry run: nothing is changed)" if dry_run else ""))
    for step in steps:
        if step.action is None:
            print(f"  {step.verb:<8}{step.target}")
        elif dry_run:
            print(f"  would {step.verb} {step.target}")
        else:
            print(f"  {step.verb:<8}{step.target}", flush=True)
            step.action()
    checks = [*checks, ccache_check(layout.checkout)]  # after the steps, so a ccache this run fetched counts
    _print_checks(checks)
    _print_next(layout, tools)
    return 1 if any(check.required and not check.ok for check in checks) else 0
