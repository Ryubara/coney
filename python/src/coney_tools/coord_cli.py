# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools coord status`: what waits for the coordinator, in one read (docs/guides/coney-tools-setup.md#coord).

Anyone at a prompt reads the whole report; the coordinator's Tether watch runs `--ids --kind ...` as command pollers to
wake the session when new ids appear (docs/guides/coney-tools-setup.md#coord-wakes). The local parts are fast and never
touch the network: the PCSX2 copies (the rows of `pcsx2 status --json`), the build slots and queues
(`build status --json`), each worktree's last detached build (`build result`), the release queue (the `PR*.md` files in
the scratch folders) and the tracks (each scratch folder's `STATUS.md`). `--github` adds the open issues (ids and
labels, no titles), the open pull requests with the `pr list-stuck` report, the CI queue's counts and, for the
release queue, which units already have a pull request.

`action` lists what waits for the coordinator as stable ids: a new id is new work, and an id that leaves the list was
dealt with. The GitHub kinds (`GITHUB_KINDS`) appear only in a `--github` read. Each section is an object with `error`
(None when it was read); a section that could not be read says why and leaves the others standing, so a failure is never
taken for "nothing waits".
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import build_cli, pcsx2_claims_cli, pr_health
from coney_tools.audit_github import Runner, run_gh
from coney_tools.config import ConfigError, find_repo_root, main_checkout
from coney_tools.conventions import load_conventions
from coney_tools.workspace_clean import list_worktrees

#: Action kinds that only a `--github` read can tell.
GITHUB_KINDS = ("issue", "pr", "unit")
#: Every action kind, and the report sections its ids come from: `--kind` reads only those. The worktrees are always
#: read; the release queue and the build results are built from them, so their ids depend on that read too.
KIND_SECTIONS = {
    "issue": ("issues",),
    "pr": ("prs",),
    "unit": ("worktrees", "release"),
    "build": ("worktrees", "results"),
}
#: The issue status label that waits for the coordinator (triage is its job).
TRIAGE_LABEL = "S-needs-triage"
#: A finished detached build is news for this long after it ended.
RECENT_BUILD_SECONDS = 60 * 60
#: A scratch folder's STATUS.md counts as a track while its worktree exists, or for this long after its last change.
TRACK_DAYS = 3
#: The release queue's files: `PR.md`, or `PR-<n>.md` for several units of one branch.
UNIT_FILE = re.compile(r"^PR(-\d+)?\.md$")
#: How many issues and pull requests are read at most (more are not expected; a notice says when the limit is hit).
LIMIT = 500


def _now() -> datetime:
    """The current time, in UTC."""
    return datetime.now(UTC)


def _iso(moment: datetime) -> str:
    """A time as ISO 8601 UTC with seconds, `2026-10-20T12:00:00Z`."""
    return moment.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def _moment(value: object) -> datetime | None:
    """A GitHub timestamp as a datetime, or None when it is absent or malformed."""
    if not isinstance(value, str) or not value or value.startswith("0001"):
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def _mtime(path: Path) -> datetime:
    """A file's last change, in UTC."""
    return datetime.fromtimestamp(path.stat().st_mtime, UTC)


def _git_lines(folder: Path, *args: str) -> list[str]:
    """The output lines of a read-only git command in `folder`; OSError when it fails."""
    done = subprocess.run(
        ["git", "-C", str(folder), *args], capture_output=True, text=True, encoding="utf-8", check=False
    )
    if done.returncode != 0:
        raise OSError(f"git {' '.join(args[:2])} failed: {done.stderr.strip()}")
    return [line for line in done.stdout.splitlines() if line.strip()]


@dataclass
class Sources:
    """Where `collect` reads from; tests swap each one for a fake."""

    #: The main checkout (the coordinator's folder).
    main: Path
    #: The scratch directory that holds one folder per branch.
    scratch: Path
    #: The repository's default branch.
    default_branch: str = "main"
    now: Callable[[], datetime] = _now
    #: `gh` with arguments, returning its standard output (OSError on failure).
    gh: Runner = run_gh
    #: (path, branch) of each worktree other than the main checkout.
    worktrees: Callable[[], list[tuple[Path, str | None]]] = lambda: []
    #: The branch names `origin` has as far as the local clone knows (no fetch).
    remote_branches: Callable[[], set[str]] = lambda: set()
    #: The rows of `pcsx2 status --json`.
    pcsx2: Callable[[], list[dict[str, object]]] = pcsx2_claims_cli.status_rows
    #: The JSON of `build status --json`.
    builds: Callable[[], dict[str, Any]] = lambda: {}
    #: A worktree's last detached build (`build result --json`), or None.
    build_result: Callable[[Path], dict[str, Any] | None] = build_cli.result_record
    #: The judge of `pr list-stuck`: (open pull requests, required checks, now) to its JSON report.
    health: Callable[[list[dict[str, Any]], tuple[str, ...], datetime], dict[str, Any]] | None = None


def default_sources(cwd: Path) -> Sources:
    """The real sources for the checkout that holds `cwd`."""
    root = find_repo_root(cwd)
    main = main_checkout(root)
    conventions = load_conventions(root)

    def worktrees() -> list[tuple[Path, str | None]]:
        return [(t.path, t.branch) for t in list_worktrees(main) if t.path.resolve() != main.resolve()]

    def remote_branches() -> set[str]:
        lines = _git_lines(main, "for-each-ref", "--format=%(refname:short)", "refs/remotes/origin")
        return {line.removeprefix("origin/") for line in lines}

    def health(pulls: list[dict[str, Any]], required: tuple[str, ...], now: datetime) -> dict[str, Any]:
        return pr_health.judge(pulls, required, conventions, now, None).as_json()

    return Sources(
        main=main,
        scratch=build_cli.scratch_root(root),
        default_branch=conventions.repo.default_branch,
        worktrees=worktrees,
        remote_branches=remote_branches,
        builds=lambda: build_cli.status_json(root),
        health=health,
    )


def _section(read: Callable[[], dict[str, Any]]) -> dict[str, Any]:
    """One section: what `read` returns with `error` None, or only `error` when it could not be read."""
    try:
        return {"error": None, **read()}
    except (ConfigError, OSError, ValueError, KeyError, TypeError) as error:
        return {"error": str(error) or type(error).__name__}


def _gh_json(src: Sources, args: list[str]) -> Any:
    """A `gh` command's JSON output."""
    return json.loads(src.gh(args))


# --- the local sections ----------------------------------------------------------------------------------------


def _scratch_folders(src: Sources) -> list[Path]:
    """The scratch directory's folders, by name."""
    if not src.scratch.is_dir():
        raise OSError(f"no scratch directory at {src.scratch}")
    return sorted((p for p in src.scratch.iterdir() if p.is_dir()), key=lambda p: p.name)


def _tracks(src: Sources, trees: dict[str, Path], now: datetime) -> dict[str, Any]:
    """Each scratch folder with a STATUS.md: when it last changed, its worktree and whether that is busy."""
    tracks: list[dict[str, Any]] = []
    for folder in _scratch_folders(src):
        status = folder / "STATUS.md"
        if not status.is_file():
            continue
        changed = _mtime(status)
        tree = trees.get(folder.name)
        if tree is None and (now - changed).total_seconds() > TRACK_DAYS * 86400:
            continue
        tracks.append(
            {
                "branch": folder.name,
                "status": str(status),
                "changed": _iso(changed),
                "worktree": str(tree) if tree else None,
                "busy": bool(tree and (tree / ".coney-busy").exists()),
            }
        )
    tracks.sort(key=lambda t: t["changed"], reverse=True)
    return {"tracks": tracks}


def _units(src: Sources, trees: dict[str, Path], remote: set[str]) -> list[dict[str, Any]]:
    """The release queue's files of the branches that still have a worktree (the others are history), each with whether
    `origin` has the branch."""
    units: list[dict[str, Any]] = []
    for folder in _scratch_folders(src):
        tree = trees.get(folder.name)
        if tree is None:
            continue
        for file in sorted(folder.iterdir(), key=lambda p: p.name):
            if not (file.is_file() and UNIT_FILE.match(file.name)):
                continue
            units.append(
                {
                    "branch": folder.name,
                    "file": file.name,
                    "path": str(file),
                    "changed": _iso(_mtime(file)),
                    "worktree": str(tree) if tree else None,
                    "pushed": folder.name in remote,
                    "pr": None,
                    "pr_state": None,
                    "queued": None,
                }
            )
    return units


def _results(src: Sources, worktrees: list[tuple[Path, str | None]], now: datetime) -> dict[str, Any]:
    """Each worktree's last detached build: running, done (with its exit code) or lost; `recent` when it ended within
    RECENT_BUILD_SECONDS."""
    results: list[dict[str, Any]] = []
    for path, branch in worktrees:
        record = src.build_result(path)
        if record is None:
            continue
        ended = record.get("ended")
        ended_at = datetime.fromtimestamp(float(ended), UTC) if isinstance(ended, int | float) else None
        started = record.get("started")
        results.append(
            {
                "worktree": path.name,
                "branch": branch,
                "state": record.get("state"),
                "agent": record.get("agent"),
                "preset": record.get("preset"),
                "exit_code": record.get("exit_code"),
                "started": _iso(datetime.fromtimestamp(float(started), UTC))
                if isinstance(started, int | float)
                else None,
                "ended": _iso(ended_at) if ended_at else None,
                "recent": bool(ended_at and (now - ended_at).total_seconds() <= RECENT_BUILD_SECONDS),
            }
        )
    return {"results": results}


# --- the GitHub sections ---------------------------------------------------------------------------------------


def _label_names(item: dict[str, Any]) -> list[str]:
    """An issue's or pull request's label names."""
    return sorted(str(label.get("name", "")) for label in item.get("labels") or [] if isinstance(label, dict))


def _issues(src: Sources, now: datetime) -> dict[str, Any]:
    """The open issues: number, status, priority and area labels, the other labels, and their age. No titles."""
    raw = _gh_json(
        src, ["issue", "list", "--state", "open", "--limit", str(LIMIT), "--json", "number,labels,createdAt,updatedAt"]
    )
    issues: list[dict[str, Any]] = []
    for item in raw:
        labels = _label_names(item)

        def family(prefix: str, names: list[str] = labels) -> str | None:
            return next((name for name in names if name.startswith(prefix)), None)

        created = _moment(item.get("createdAt"))
        issues.append(
            {
                "number": int(item["number"]),
                "status": family("S-"),
                "priority": family("P-"),
                "area": family("A-"),
                "labels": [name for name in labels if not re.match(r"^[SPA]-", name)],
                "created": item.get("createdAt"),
                "updated": item.get("updatedAt"),
                "age_days": round((now - created).total_seconds() / 86400, 1) if created else None,
            }
        )
    issues.sort(key=lambda i: i["number"])
    counts: dict[str, int] = {}
    for issue in issues:
        key = issue["status"] or "no status"
        counts[key] = counts.get(key, 0) + 1
    notices = [f"{len(issues)} issues read: the limit, so more may be open"] if len(issues) >= LIMIT else []
    return {"issues": issues, "counts": counts, "notices": notices}


def _ci_state(pull: dict[str, Any], required: tuple[str, ...]) -> str:
    """The pull request's required checks in one word: fail, pending, pass, or none when it has no checks."""
    checks = pr_health.checks_of(pull.get("statusCheckRollup"))
    if not checks:
        return "none"
    wanted = [c for c in checks if c.name in required] if required else checks
    if any(c.state == "failed" for c in wanted):
        return "fail"
    if any(c.state == "pending" for c in wanted) or len({c.name for c in wanted}) < len(set(required)):
        return "pending"
    return "pass"


def _prs(src: Sources, now: datetime) -> dict[str, Any]:
    """The open pull requests (number, branch, draft, mergeable, checks, age) and the `pr list-stuck` report."""
    pulls, required = pr_health.fetch(src.default_branch, src.gh, f"{pr_health.PULL_FIELDS},updatedAt,headRefOid")
    health = src.health(pulls, required, now) if src.health else {"stuck": [], "other": [], "notices": []}
    causes: dict[int, list[str]] = {}
    for item in health.get("stuck", []):
        causes.setdefault(int(item["number"]), []).append(str(item["cause"]))
    prs: list[dict[str, Any]] = [
        {
            "number": int(pull["number"]),
            "branch": str(pull.get("headRefName", "")),
            "base": str(pull.get("baseRefName", "")),
            "draft": bool(pull.get("isDraft")),
            "mergeable": pull.get("mergeable"),
            "auto_merge": pull.get("autoMergeRequest") is not None,
            "ci": _ci_state(pull, required),
            "updated": pull.get("updatedAt"),
            "head": pull.get("headRefOid"),
            "causes": causes.get(int(pull["number"]), []),
        }
        for pull in pulls
    ]
    prs.sort(key=lambda p: p["number"])
    return {"prs": prs, "health": health}


def _ci(src: Sources) -> dict[str, Any]:
    """How many workflow runs are queued and how many run (the release queue's room: docs/guides/how-we-work.md)."""
    counts = {}
    for status in ("queued", "in_progress"):
        runs = _gh_json(src, ["run", "list", "--status", status, "--limit", "200", "--json", "databaseId"])
        counts[status] = len(runs)
    return counts


def _released(src: Sources, units: list[dict[str, Any]]) -> None:
    """Mark each unit with its branch's pull request: an open one, or a merged or closed one newer than the file, means
    it was released; otherwise it is queued while its worktree exists."""
    raw = _gh_json(
        src,
        ["pr", "list", "--state", "all", "--limit", str(LIMIT), "--json", "number,headRefName,state,closedAt"],
    )
    by_branch: dict[str, list[dict[str, Any]]] = {}
    for pull in raw:
        by_branch.setdefault(str(pull.get("headRefName", "")), []).append(pull)
    for unit in units:
        pulls = sorted(by_branch.get(unit["branch"], []), key=lambda p: int(p["number"]), reverse=True)
        opened = next((p for p in pulls if p.get("state") == "OPEN"), None)
        changed = datetime.fromisoformat(unit["changed"].replace("Z", "+00:00"))
        ended = next(
            (p for p in pulls if (_moment(p.get("closedAt")) or datetime.min.replace(tzinfo=UTC)) >= changed),
            None,
        )
        pull = opened or ended
        unit["pr"] = int(pull["number"]) if pull else None
        unit["pr_state"] = str(pull.get("state")) if pull else None
        unit["queued"] = pull is None


# --- the whole report --------------------------------------------------------------------------------------------


def actions(report: dict[str, Any]) -> list[str]:
    """The ids of what waits for the coordinator, sorted: issues to triage, pull-request problems, units to release
    and detached builds that ended recently."""
    out: set[str] = set()
    for issue in (report.get("issues") or {}).get("issues", []):
        if issue["status"] == TRIAGE_LABEL:
            out.add(f"issue:{issue['number']}:{TRIAGE_LABEL}")
    for item in ((report.get("prs") or {}).get("health") or {}).get("stuck", []):
        out.add(f"pr:{item['number']}:{item['cause']}")
    for unit in (report.get("release") or {}).get("units", []):
        if unit.get("queued"):
            out.add(f"unit:{unit['branch']}:{unit['file']}")
    for result in (report.get("results") or {}).get("results", []):
        if result["state"] == "done" and result["recent"]:
            out.add(f"build:done:{result['worktree']}")
    return sorted(out)


def collect(src: Sources, github: bool, kinds: list[str] | None = None) -> dict[str, Any]:
    """The whole report; the GitHub sections are None without `github`. With `kinds`, only the sections those action
    kinds come from are read (the others are None), so a poller that asks for one kind pays for one read."""
    now = src.now()
    wanted = {name for kind in kinds for name in KIND_SECTIONS[kind]} if kinds else None

    def read(name: str) -> bool:
        return wanted is None or name in wanted

    report: dict[str, Any] = {"time": _iso(now), "checkout": str(src.main), "github": github}
    listed: list[tuple[Path, str | None]] = []
    trees: dict[str, Path] = {}
    try:
        listed = src.worktrees()
        trees = {branch: path for path, branch in listed if branch}
        report["worktrees"] = {"error": None, "count": len(listed)}
    except (ConfigError, OSError) as error:
        report["worktrees"] = {"error": str(error)}
    report["pcsx2"] = _section(lambda: {"copies": src.pcsx2()}) if read("pcsx2") else None
    report["builds"] = _section(lambda: {"lanes": src.builds()}) if read("builds") else None
    report["results"] = _section(lambda: _results(src, listed, now)) if read("results") else None
    report["tracks"] = _section(lambda: _tracks(src, trees, now)) if read("tracks") else None

    def release() -> dict[str, Any]:
        try:
            remote = src.remote_branches()
        except OSError:
            remote = set()
        units = _units(src, trees, remote)
        if github:
            _released(src, units)
        return {"units": units}

    report["release"] = _section(release) if read("release") else None
    report["issues"] = _section(lambda: _issues(src, now)) if github and read("issues") else None
    report["prs"] = _section(lambda: _prs(src, now)) if github and read("prs") else None
    report["ci"] = _section(lambda: _ci(src)) if github and read("ci") else None
    report["action"] = actions(report)
    return report


def format_text(report: dict[str, Any]) -> list[str]:
    """A short text form: one line per section, then the actions."""
    lines = [f"coord status {report['time']}{' (with GitHub)' if report['github'] else ''}"]
    for name in ("worktrees", "pcsx2", "builds", "results", "tracks", "release", "issues", "prs", "ci"):
        section = report.get(name)
        if section is None:
            continue
        if section.get("error"):
            lines.append(f"{name}: not read ({section['error']})")
            continue
        sizes = {key: len(value) for key, value in section.items() if isinstance(value, list) and key != "notices"}
        counts = {key: value for key, value in section.items() if isinstance(value, int)}
        lines.append(f"{name}: " + ", ".join(f"{n} {k}" for k, n in {**sizes, **counts}.items()))
    lines.append(f"action: {' '.join(report['action']) or 'none'}")
    return lines


def add_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Register `coney-tools coord status`."""
    group = groups.add_parser("coord", help="the coordinator's view: what waits for it, in one read")
    commands = group.add_subparsers(dest="command", required=True)
    status = commands.add_parser(
        "status",
        help="issues, pull requests, CI, PCSX2 copies, build slots, release queue and tracks, with action ids",
        description=(
            "Prints what waits for the coordinator: the local parts always (PCSX2 copies, build slots and results, "
            "the release queue's PR*.md files, the tracks' STATUS.md), and with --github the open issues, pull "
            "requests (with the pr-health report) and the CI queue. `action` lists stable ids of the work waiting. "
            "Exit code 0; a section that cannot be read carries its error."
        ),
    )
    status.add_argument("--json", action="store_true", help="print the report as JSON")
    status.add_argument("--github", action="store_true", help="also read GitHub through gh (slower)")
    status.add_argument(
        "--ids",
        action="store_true",
        help="print only the action ids, one per line; exit 1 when a section they come from could not be read",
    )
    status.add_argument(
        "--kind",
        action="append",
        choices=sorted(KIND_SECTIONS),
        help="read only what these action kinds need, and print only their ids (repeatable)",
    )


def _stderr(line: str) -> None:
    """Print one line to stderr."""
    print(line, file=sys.stderr)


def _ids(
    report: dict[str, Any], kinds: list[str] | None, out: Callable[[str], None], err: Callable[[str], None]
) -> int:
    """`--ids`: the action ids of `kinds` (every kind read when None), one per line. A section they come from that
    could not be read fails the run (exit 1, nothing printed), since a short list would read as "nothing waits"."""
    asked = kinds or [kind for kind in KIND_SECTIONS if report["github"] or kind not in GITHUB_KINDS]
    failed = [
        f"{name} not read: {report[name]['error']}"
        for kind in asked
        for name in KIND_SECTIONS[kind]
        if report.get(name) and report[name].get("error")
    ]
    if failed:
        for reason in dict.fromkeys(failed):
            err(f"coord status: {reason}")
        return 1
    for action in report["action"]:
        if action.split(":", 1)[0] in asked:
            out(action)
    return 0


def run(
    args: argparse.Namespace,
    sources: Sources | None = None,
    out: Callable[[str], None] = print,
    err: Callable[[str], None] = _stderr,
) -> int:
    """Run `coord status`."""
    kinds: list[str] | None = getattr(args, "kind", None)
    if getattr(args, "ids", False) and args.json:
        err("coord status: --ids and --json print different things; give one")
        return 2
    for kind in kinds or []:
        if kind in GITHUB_KINDS and not args.github:
            err(f"coord status: --kind {kind} needs --github")
            return 2
    report = collect(sources or default_sources(Path.cwd()), args.github, kinds)
    if getattr(args, "ids", False):
        return _ids(report, kinds, out, err)
    if args.json:
        out(json.dumps(report, indent=1, ensure_ascii=False))
    else:
        for line in format_text(report):
            out(line)
    return 0
