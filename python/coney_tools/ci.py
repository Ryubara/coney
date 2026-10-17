"""Coney's CI jobs, run the same way in GitHub Actions and on a contributor's machine (`coney-tools ci`).

The jobs are listed here, once (JOBS). Every workflow job under .github/workflows/ that runs one calls
`coney-tools ci --job <name>` under the same name, so a workflow and a local run cannot drift; the `workflow` job
checks that it stays so, that every action is pinned to a full commit SHA and that every workflow sets its
`permissions:`, with a write scope only on a job that does nothing but deploy to GitHub Pages, or on a job that
writes to issues in a workflow no pull request or push can start.

Tool versions are read from the files that already pin them, one source each (`--versions` prints them for a
workflow): Python's from python/pyproject.toml (the lowest `requires-python` allows), Node's, uv's and Claude Code's
from containers/Dockerfile, markdownlint-cli2's from .pre-commit-config.yaml, the documentation site's packages and
PyYAML from hash-locked requirements files.

Exit status: 0 every job passed, 1 a job failed, 2 a job could not run.
"""

import hashlib
import io
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tomllib
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import codegate, lint
from coney_tools.cleanroom import scan

WORKFLOWS = (".github", "workflows")
GATHER = "build-ok"  # the required check of ci.yml: it needs every other job of its workflow
# The one kind of job that may get a write scope: it deploys what an earlier job built, and runs nothing else.
DEPLOY_ACTIONS = ("actions/deploy-pages",)
DEPLOY_WRITES = ("id-token", "pages")
# The other: a job that writes to issues only (Groundwork's hygiene sweep and issue-status), in a workflow that only a
# schedule, a maintainer's dispatch or an issue event starts, so no pull request's or push's code runs with the token.
ISSUE_WRITES = ("issues",)
ISSUE_TRIGGERS = ("schedule", "workflow_dispatch", "issues")
PYPROJECT = ("python", "pyproject.toml")
DOCKERFILE = ("containers", "Dockerfile")
DOCS_IN, DOCS_LOCK = ("requirements-docs.in",), ("requirements-docs.txt",)
GROUNDWORK = (".github", "scripts", "groundwork.py")
RUFF_HOOK = "https://github.com/astral-sh/ruff-pre-commit"
RUFF_PATHS = ("python",)  # what the ruff hook checks (its files: ^python/)
GROUNDWORK_LOCK = (".github", "scripts", "requirements.txt")
MODS = ("mods",)
PLUGIN_MARK = ".claude-plugin"  # a folder under mods/ holding this is a mod
WORK = ("build", "ci")  # scratch under the checkout (.gitignore keeps /build/ out)
CLAUDE_PACKAGE = "@anthropic-ai/claude-code"
NODE_URL = "https://nodejs.org/dist/v{v}/node-v{v}-linux-x64.tar.xz"
NODE_TIMEOUT = 300  # seconds for the download
STAMP = "coney-ci.stamp"  # in a job's virtual environment: the hash of what it was built from
# The compilers the engine must build and pass its tests with (the implementer image has both), by build folder name.
ENGINE_COMPILERS = {"gcc": "g++", "clang": "clang++"}
ENGINE_TOOLS = ("cmake", "ctest", "ninja")

VERSION = re.compile(r"^\d+\.\d+\.\d+$")
SHA40 = re.compile(r"^[0-9a-f]{40}$")
CALL = re.compile(r"coney-tools\.py\s+ci\s+--job\s+([A-Za-z0-9_-]+)")
USES = re.compile(r"^\s*(?:-\s+)?uses:\s*['\"]?([^'\"\s#]+)['\"]?")
LINTED = re.compile(r"^Linting: (\d+) files?\s*$", re.MULTILINE)  # markdownlint-cli2's count of what it read
PIN = re.compile(r"^([A-Za-z0-9_.-]+)==([^\s;\\]+)")


class CiError(Exception):
    """A job, or the command, could not run (exit 2)."""


@dataclass
class Context:
    tools: Path  # the checkout coney-tools runs from
    repo: Path  # the checkout the jobs check
    ref: str = "main"  # the git ref verify reads allowed_signers from; the base of the default commit range
    range: str | None = None  # the commits whose messages disclosure checks (default <ref>..HEAD)
    texts: list = field(default_factory=list)  # more text files for disclosure (a pull request's title and body)
    python: str = sys.executable
    site_dir: Path | None = None  # where the site job builds the site (default build/ci/site)

    def script(self):
        return str(self.tools / "python" / "coney-tools.py")

    def work(self):
        return self.repo.joinpath(*WORK)


@dataclass
class Step:
    what: str
    argv: list | None = None  # a program to run, or
    call: object = None  # an in-process check: call(ctx, run) -> (code, lines)
    cwd: Path | None = None
    env: dict = field(default_factory=dict)
    tool: bool = False  # its exit 2 means "could not run" (coney-tools, groundwork.py), not a failed check
    counted: object = None  # a pattern its output must match with a count above 0 (a run that read no file fails)


@dataclass(frozen=True)
class Job:
    name: str
    what: str
    steps: object  # steps(ctx) -> [Step]; raises CiError when the job cannot run here


# --- versions: each read from the one file that pins it ---------------------------------------------------------


def read(repo, parts):
    path = Path(repo).joinpath(*parts)
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as e:
        raise CiError(f"cannot read {'/'.join(parts)}: {e.__class__.__name__}") from None


def python_version(repo):
    """The lowest Python python/pyproject.toml's requires-python allows, as major.minor."""
    try:
        spec = tomllib.loads(read(repo, PYPROJECT))["project"]["requires-python"]
    except (tomllib.TOMLDecodeError, KeyError, TypeError):
        raise CiError(f"no project.requires-python in {'/'.join(PYPROJECT)}") from None
    found = [m.groups() for m in re.finditer(r">=\s*(\d+)\.(\d+)(?:\.\d+)?", str(spec))]
    if len(found) != 1:
        raise CiError(f"requires-python {spec!r} names no single lowest version (>=X.Y)")
    return ".".join(found[0])


def dockerfile_arg(repo, name):
    """The default value of `ARG <name>=...` in containers/Dockerfile (exactly one)."""
    values = re.findall(rf"^ARG\s+{re.escape(name)}=(\S+)\s*$", read(repo, DOCKERFILE), re.MULTILINE)
    if len(values) != 1:
        raise CiError(f"no single ARG {name}=... in {'/'.join(DOCKERFILE)}")
    return values[0]


def release(value, what):
    if not VERSION.match(value):
        raise CiError(f"{what} {value!r} is not a release version (X.Y.Z)")
    return value


def uv_version(repo):
    """uv's version from the image reference containers/Dockerfile pins it by (…/uv:X.Y.Z@sha256:…)."""
    m = re.match(r"^[^:@\s]+/uv:([^@\s]+)@sha256:[0-9a-f]{64}$", dockerfile_arg(repo, "UV"))
    if not m:
        raise CiError(f"ARG UV in {'/'.join(DOCKERFILE)} is not a uv image pinned by tag and digest")
    return release(m.group(1), "uv")


def markdownlint_version(repo):
    try:
        return lint.pinned_version(read(repo, (lint.HOOKS,)), "the working tree")
    except lint.LintError as e:
        raise CiError(str(e)) from None


def ruff_version(repo):
    """ruff's version: the rev of the ruff hook in .pre-commit-config.yaml, so CI and the commit hook run the same."""
    try:
        return lint.pinned_version(read(repo, (lint.HOOKS,)), "the working tree", RUFF_HOOK)
    except lint.LintError as e:
        raise CiError(str(e)) from None


def versions(repo):
    """Every pinned tool version a workflow needs, by output name."""
    return {
        "python": python_version(repo),
        "node": release(dockerfile_arg(repo, "NODE_VERSION"), "Node"),
        "uv": uv_version(repo),
        "claude_code": release(dockerfile_arg(repo, "CLAUDE_CODE_VERSION"), "Claude Code"),
        "markdownlint_cli2": markdownlint_version(repo),
        "ruff": ruff_version(repo),
    }


# --- Node for a Linux runner, checked against the Dockerfile's checksum -----------------------------------------


def install_node(repo, dest, opener=urllib.request.urlopen, system=None):
    """Downloads the Node release containers/Dockerfile pins, checks it against the SHA-256 the Dockerfile gives,
    unpacks it into dest (which must not exist yet) and returns dest/bin. Linux x64 only: that is the build the
    checksum is for."""
    system = system or (sys.platform, platform.machine())
    if not (system[0].startswith("linux") and system[1] in ("x86_64", "AMD64")):
        raise CiError(f"--install-node is for Linux x64 (the Dockerfile's checksum is for that build), not {system}")
    version = release(dockerfile_arg(repo, "NODE_VERSION"), "Node")
    want = dockerfile_arg(repo, "NODE_SHA256").lower()
    if not re.fullmatch(r"[0-9a-f]{64}", want):
        raise CiError(f"ARG NODE_SHA256 in {'/'.join(DOCKERFILE)} is not a SHA-256")
    if not hasattr(tarfile, "data_filter"):
        raise CiError("--install-node needs Python 3.11.4 or later (tarfile's data filter)")
    dest = Path(dest)
    if dest.exists():
        raise CiError(f"{dest} exists already")
    url = NODE_URL.format(v=version)
    try:
        with opener(url, timeout=NODE_TIMEOUT) as r:
            data = r.read()
    except OSError as e:
        raise CiError(f"cannot download {url}: {e.__class__.__name__}") from None
    if hashlib.sha256(data).hexdigest() != want:
        raise CiError(f"{url} does not match the SHA-256 in {'/'.join(DOCKERFILE)}")
    try:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:xz") as tar:
            members = []
            for m in tar.getmembers():
                _, _, rest = m.name.partition("/")
                if rest:  # the archive's top folder is dropped (tar --strip-components=1)
                    m.name = rest
                    members.append(m)
            dest.mkdir(parents=True)
            tar.extractall(dest, members=members, filter="data")
    except (tarfile.TarError, OSError) as e:
        raise CiError(f"cannot unpack Node into {dest}: {e.__class__.__name__}") from None
    if not (dest / "bin" / "node").is_file():
        raise CiError("the Node archive has no bin/node")
    return dest / "bin"


# --- in-process checks ------------------------------------------------------------------------------------------


def grants(value):
    """{scope: access} of an inline `permissions:` value (read-all, write-all, {} or a flow map), or None when the
    value is empty and a block of `scope: access` lines follows."""
    value = value.split(" #", 1)[0].strip()
    if not value:
        return None
    if value in ("read-all", "write-all"):
        return {"*": value.split("-")[0]}
    inner = value[1:-1] if value.startswith("{") and value.endswith("}") else value
    out = {}
    for item in inner.split(","):
        scope, _, access = item.partition(":")
        if scope.strip():
            out[scope.strip()] = access.strip()
    return out


def parse_workflow(text):
    """What the workflow check needs from a workflow file, read line by line (the standard library has no YAML):
    (top-level permissions as {scope: access} or None, {job id: job}, [(line, action ref)]). A job is {"calls": [ci
    job names], "needs": [ids] or None, "permissions": {scope: access} or None, "uses": [action refs], "runs": how
    many `run:` steps it has}."""
    permissions, jobs, uses = None, {}, []
    in_jobs, current, block = False, None, None  # block: the permissions map indented lines are read into
    for n, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("#") or not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        if block is not None:
            if indent > (4 if current else 0):
                m = re.match(r"^\s*([A-Za-z0-9_-]+):\s*(.*?)\s*$", line.split(" #", 1)[0])
                if m:
                    block[m.group(1)] = m.group(2)
                continue
            block = None
        if re.match(r"^\S", line):
            in_jobs, current = bool(re.match(r"^jobs:\s*$", line)), None
            m = re.match(r"^permissions:(.*)$", line)
            if m:
                permissions = grants(m.group(1))
                if permissions is None:
                    permissions = block = {}
            continue
        m = USES.match(line)
        if m:
            uses.append((n, m.group(1)))
        if not in_jobs:
            continue
        m = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if m:
            current = m.group(1)
            jobs[current] = {"calls": [], "needs": None, "permissions": None, "uses": [], "runs": 0}
            continue
        if current is None:
            continue
        job = jobs[current]
        m = re.match(r"^    permissions:(.*)$", line)
        if m:
            job["permissions"] = grants(m.group(1))
            if job["permissions"] is None:
                job["permissions"] = block = {}
            continue
        m = re.match(r"^    needs:\s*(.*?)\s*$", line)
        if m:
            value = m.group(1)
            inner = value[1:-1] if value.startswith("[") and value.endswith("]") else value
            job["needs"] = [x.strip() for x in inner.split(",") if x.strip()]
        m = USES.match(line)
        if m:
            job["uses"].append(m.group(1))
        job["runs"] += bool(re.match(r"^\s*(?:-\s+)?run:", line))
        job["calls"] += CALL.findall(line.split(" #", 1)[0])
    return permissions, jobs, uses


def writes(granted):
    """The scopes a permissions map grants write access to ("*" for write-all)."""
    return sorted(scope for scope, access in (granted or {}).items() if access == "write")


def triggers(text):
    """The events a workflow file's top-level `on:` names (inline, as a list, or as a block of keys)."""
    out, block = set(), False
    for line in text.splitlines():
        if line.lstrip().startswith("#") or not line.strip():
            continue
        if block:
            if not line.startswith(" "):
                break
            m = re.match(r"^  ([A-Za-z_]+):", line)
            if m:
                out.add(m.group(1))
            continue
        m = re.match(r"^[\"']?on[\"']?:\s*(.*?)\s*$", line.split(" #", 1)[0])
        if m:
            value = m.group(1).strip("[]")
            if not value:
                block = True
                continue
            return {x.strip() for x in value.split(",") if x.strip()}
    return out


def write_problems(fname, job, info, events):
    """A job granted a write scope is a deploy job (DEPLOY_WRITES at most, no script, DEPLOY_ACTIONS only), or an
    issue job (ISSUE_WRITES only) in a workflow that only ISSUE_TRIGGERS start."""
    granted = writes(info["permissions"])
    if not granted:
        return []
    actions = {ref.partition("@")[0] for ref in info["uses"]}
    if set(granted) <= set(DEPLOY_WRITES) and not info["runs"] and actions <= set(DEPLOY_ACTIONS):
        return []
    if set(granted) <= set(ISSUE_WRITES) and events and events <= set(ISSUE_TRIGGERS):
        return []
    message = (
        f"{fname}: job {job} gets {', '.join(f'{s}: write' for s in granted)}; only a job that runs nothing but "
        f"{', '.join(DEPLOY_ACTIONS)} may get a write scope ({', '.join(DEPLOY_WRITES)} at most), or one that gets "
        f"{', '.join(ISSUE_WRITES)} only in a workflow started by {', '.join(ISSUE_TRIGGERS)} alone"
    )
    return [message]


def workflow_problems(files, names):
    """Problems with the workflows ({file name: text}) against the job names: each workflow sets top-level
    permissions that grant no write and pins every action to a commit SHA; only a deploy or issue job gets a write
    scope (write_problems); a job that runs `ci --job X` is named X and X is a job; every job runs in some workflow; the
    GATHER job needs every other job of its workflow."""
    problems, called, gathered = [], set(), False
    for fname, text in sorted(files.items()):
        permissions, jobs, uses = parse_workflow(text)
        if permissions is None:
            problems.append(f"{fname}: no top-level permissions: (give each workflow the least it needs)")
        for scope in writes(permissions):
            problems.append(f"{fname}: the top-level permissions grant {scope}: write; grant it on the job needing it")
        for n, ref in uses:
            if ref.startswith("./"):
                continue
            action, _, pin = ref.partition("@")
            if not SHA40.match(pin):
                problems.append(f"{fname}:{n}: {action} is not pinned to a full commit SHA")
        events = triggers(text)
        for job, info in jobs.items():
            problems += write_problems(fname, job, info, events)
            for name in info["calls"]:
                called.add(name)
                if name not in names:
                    problems.append(f"{fname}: job {job} runs ci --job {name}, which is not a job")
                elif name != job:
                    problems.append(f"{fname}: job {job} runs ci --job {name}; name the workflow job {name}")
        if GATHER in jobs:
            gathered = True
            want = set(jobs) - {GATHER}
            have = set(jobs[GATHER]["needs"] or [])
            for job in sorted(want - have):
                problems.append(f"{fname}: {GATHER} does not need {job}, so a failure there would not block a merge")
            for job in sorted(have - want):
                problems.append(f"{fname}: {GATHER} needs {job}, which this workflow lacks")
    for name in sorted(set(names) - called):
        problems.append(f"no workflow runs ci --job {name}")
    if not gathered:
        problems.append(f"no workflow has the {GATHER} job")
    return problems


def check_workflows(ctx, run):
    folder = ctx.repo.joinpath(*WORKFLOWS)
    try:
        files = {p.name: p.read_text(encoding="utf-8") for p in sorted(folder.glob("*.yml"))}
    except (OSError, UnicodeDecodeError) as e:
        raise CiError(f"cannot read the workflows: {e.__class__.__name__}") from None
    if not files:
        raise CiError(f"no workflows in {'/'.join(WORKFLOWS)}")
    problems = workflow_problems(files, list(JOBS))
    return (1 if problems else 0), problems or [f"{len(files)} workflows run every job, pinned"]


def pins(text):
    """{normalised name: version} of every `name==version` line of a requirements file."""
    out = {}
    for line in text.splitlines():
        m = PIN.match(line.strip())
        if m:
            out[re.sub(r"[-_.]+", "-", m.group(1)).lower()] = m.group(2)
    return out


def lock_problems(direct, lock):
    """A direct pin the lock does not hold at the same version (the lock was not regenerated after a change)."""
    have = pins(lock)
    return [
        f"{name}=={version} is {have.get(name, 'missing')} in the lock; regenerate it"
        for name, version in sorted(pins(direct).items())
        if have.get(name) != version
    ]


def check_docs_lock(ctx, run):
    problems = lock_problems(read(ctx.repo, DOCS_IN), read(ctx.repo, DOCS_LOCK))
    if not pins(read(ctx.repo, DOCS_IN)):
        problems.append(f"{DOCS_IN[0]} pins nothing")
    return (1 if problems else 0), problems or [f"{DOCS_LOCK[0]} holds every pin of {DOCS_IN[0]}"]


def venv_python(venv):
    return Path(venv) / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def uv_program():
    """uv's path when it is on PATH (CI's setup-uv caches its downloads), else None."""
    return scan.find_program("uv")


def environment(name, lock_parts):
    """An in-process step: a virtual environment under build/ci/ with exactly the hash-locked requirements, made
    again only when the lock or the Python running it changes. uv makes it when uv is on PATH (its cache is CI's
    package cache), else the standard library's venv and pip."""

    def call(ctx, run):
        lock = ctx.repo.joinpath(*lock_parts)
        venv = ctx.work() / f"{name}-env"
        try:
            want = hashlib.sha256(lock.read_bytes() + f"\0{ctx.python}\0{sys.version}".encode()).hexdigest()
        except OSError as e:
            raise CiError(f"cannot read {'/'.join(lock_parts)}: {e.__class__.__name__}") from None
        stamp = venv / STAMP
        try:
            if stamp.read_text(encoding="ascii").strip() == want and venv_python(venv).is_file():
                return 0, [f"{venv.name}: up to date"]
        except (OSError, UnicodeDecodeError):
            pass
        shutil.rmtree(venv, ignore_errors=True)
        uv = uv_program()
        if uv:
            commands = (
                [uv, "venv", "--quiet", "--python", ctx.python, str(venv)],
                [uv, "pip", "install", "--python", str(venv_python(venv)), "--require-hashes", "--no-deps", "-r",
                 str(lock)],
            )  # fmt: skip
        else:
            commands = (
                [ctx.python, "-I", "-m", "venv", str(venv)],
                [str(venv_python(venv)), "-I", "-m", "pip", "install", "--disable-pip-version-check", "--no-input",
                 "--require-hashes", "--no-deps", "-r", str(lock)],
            )  # fmt: skip
        for argv in commands:
            code = run_program(argv, ctx.repo, {}, run)
            if code:
                what = " ".join(a for a in argv[1:4] if not a.startswith("-"))
                return 2, [f"{venv.name}: {Path(argv[0]).name} {what} exited {code}"]
        if not venv_python(venv).is_file():
            return 2, [f"{venv.name}: no Python in the environment made"]
        stamp.write_text(want + "\n", encoding="ascii")
        return 0, [f"{venv.name}: made from {'/'.join(lock_parts)}"]

    return call


# --- the jobs ---------------------------------------------------------------------------------------------------


def tests_steps(ctx):
    return [
        Step(
            "coney-tools unit tests",
            [ctx.python, "-I", "-B", "-m", "unittest", "discover", "-s", "tests"],
            cwd=ctx.repo / "python",
        )
    ]


def verify_steps(ctx):
    argv = [ctx.python, "-I", ctx.script(), "cleanroom", "verify", "--repo", str(ctx.repo), "--ref", ctx.ref]
    return [Step(f"research pages, allowed_signers from {ctx.ref}", argv, cwd=ctx.repo, tool=True)]


def commits(ctx):
    """The commits a job checks: --range, else <ref>..HEAD."""
    rng = ctx.range or f"{ctx.ref}..HEAD"
    if rng.startswith("-"):
        raise CiError("a revision range cannot start with -")
    return rng


def orig_check_steps(ctx):
    argv = [ctx.python, "-I", ctx.script(), "cleanroom", "orig-check", "--repo", str(ctx.repo), "--ref", ctx.ref]
    return [Step(f"@orig tags, with pages and settings from {ctx.ref}", argv, cwd=ctx.repo, tool=True)]


def commit_sigs_steps(ctx):
    rng = commits(ctx)
    argv = [ctx.python, "-I", ctx.script(), "cleanroom", "commit-sigs", "--repo", str(ctx.repo), "--ref", ctx.ref, rng]
    return [Step(f"engine commits in {rng}, allowed_signers from {ctx.ref}", argv, cwd=ctx.repo, tool=True)]


def code_verdict_steps(ctx):
    rng = commits(ctx)
    argv = [ctx.python, "-I", ctx.script(), "cleanroom", "code-verdict", "--repo", str(ctx.repo), "--ref", ctx.ref, rng]
    return [
        Step(f"engine changes in {rng} covered by signed verdicts, keys from {ctx.ref}", argv, cwd=ctx.repo, tool=True)
    ]


def disclosure_steps(ctx):
    rng = commits(ctx)
    base = [ctx.python, "-I", ctx.script(), "cleanroom", "disclosure-check"]
    steps = [Step(f"commit messages in {rng}", [*base, "--commits", rng], cwd=ctx.repo, tool=True)]
    if ctx.texts:
        steps.append(Step("pull request text", [*base, *map(str, ctx.texts)], cwd=ctx.repo, tool=True))
    return steps


def npx():
    try:
        return lint.npx_program()
    except lint.LintError as e:
        raise CiError(str(e)) from None


NODE_ENV = {"NO_COLOR": "1", "NPM_CONFIG_UPDATE_NOTIFIER": "false", "NPM_CONFIG_FUND": "false"}


def markdownlint_steps(ctx):
    version = markdownlint_version(ctx.repo)
    # Which files: .markdownlint-cli2.yaml (it skips what .gitignore keeps out); the rules: .markdownlint.yaml.
    argv = [npx(), "--yes", f"{lint.PACKAGE}@{version}", "**/*.md"]
    return [Step(f"{lint.PACKAGE} {version}", argv, cwd=ctx.repo, env=NODE_ENV, counted=LINTED)]


def ruff_steps(ctx):
    version = ruff_version(ctx.repo)
    uv = uv_program()
    if not uv:
        raise CiError("the ruff job needs uv on PATH (it runs the pinned ruff with `uv tool run`)")
    ruff = [uv, "tool", "run", f"ruff@{version}"]
    return [
        Step(f"ruff {version} check", [*ruff, "check", *RUFF_PATHS], cwd=ctx.repo, tool=True),
        Step(f"ruff {version} format --check", [*ruff, "format", "--check", *RUFF_PATHS], cwd=ctx.repo, tool=True),
    ]


def mod_steps(ctx):
    mods = sorted(p for p in ctx.repo.joinpath(*MODS).glob("*") if (p / PLUGIN_MARK).is_dir())
    if not mods:
        raise CiError(f"no mod (a folder holding {PLUGIN_MARK}) under {'/'.join(MODS)}/")
    version = release(dockerfile_arg(ctx.repo, "CLAUDE_CODE_VERSION"), "Claude Code")
    claude = [npx(), "--yes", f"{CLAUDE_PACKAGE}@{version}", "plugin"]
    config = ctx.work() / "claude-config"  # an empty config: no login, nothing of the account running it
    config.mkdir(parents=True, exist_ok=True)
    env = dict(NODE_ENV, CLAUDE_CONFIG_DIR=str(config), DISABLE_AUTOUPDATER="1")
    steps = []
    for mod in mods:
        rel = mod.relative_to(ctx.repo).as_posix()
        steps.append(Step(f"validate {rel}", [*claude, "validate", rel], cwd=ctx.repo, env=env))
        steps.append(Step(f"test {rel} (Claude Code {version})", [*claude, "test", rel], cwd=ctx.repo, env=env))
    return steps


def site_steps(ctx):
    py = str(venv_python(ctx.work() / "site-env"))
    out = ctx.site_dir or ctx.work() / "site"
    build = [py, "-I", "-m", "mkdocs", "build", "--strict", "--site-dir", str(out)]
    return [
        Step(f"{DOCS_LOCK[0]} holds {DOCS_IN[0]}", call=check_docs_lock),
        Step("site environment", call=environment("site", DOCS_LOCK)),
        Step("mkdocs build --strict", build, cwd=ctx.repo),
    ]


def conventions_steps(ctx):
    py = str(venv_python(ctx.work() / "groundwork-env"))
    script = str(ctx.repo.joinpath(*GROUNDWORK))
    return [
        Step("groundwork environment", call=environment("groundwork", GROUNDWORK_LOCK)),
        Step("conventions.yml is well formed", [py, "-I", script, "validate"], cwd=ctx.repo, tool=True),
        Step("templates and forms match it", [py, "-I", script, "render", "--check"], cwd=ctx.repo, tool=True),
        Step("every tracked YAML file parses", [py, "-I", "-c", YAML_PARSE], cwd=ctx.repo, tool=True),
    ]


# Run with the groundwork environment's Python (it has PyYAML): every tracked .yml/.yaml file must load. Tags such as
# mkdocs.yml's Python names are accepted as plain values; only the YAML's structure is checked.
YAML_PARSE = """
import subprocess, sys, yaml
class Loader(yaml.SafeLoader):
    pass
Loader.add_multi_constructor("", lambda loader, tag, node: None)
names = subprocess.run(["git", "ls-files", "-z", "*.yml", "*.yaml"], capture_output=True, check=True).stdout
names, bad = [n for n in names.decode("utf-8").split("\\0") if n], 0
for name in names:
    try:
        with open(name, encoding="utf-8") as f:
            list(yaml.load_all(f, Loader=Loader))
    except yaml.YAMLError as e:
        bad += 1
        print(f"{name}: {str(e).splitlines()[0]} (line {getattr(getattr(e, 'problem_mark', None), 'line', -1) + 1})")
print(f"{bad} of {len(names)} YAML files do not parse" if bad else f"{len(names)} YAML files: ok")
sys.exit(1 if bad else 0)
"""


def engine_root(repo):
    """The engine's folder: engine_root of the code gate's settings in the working tree's python/pyproject.toml."""
    try:
        doc = tomllib.loads(read(repo, PYPROJECT))
    except tomllib.TOMLDecodeError:
        raise CiError(f"{'/'.join(PYPROJECT)} is not valid TOML") from None
    for key in codegate.SECTION:
        doc = doc.get(key) if isinstance(doc, dict) else None
    value = doc.get("engine_root") if isinstance(doc, dict) else None
    try:
        return codegate.rel_path(value, "engine_root")
    except codegate.CodeGateError as e:
        raise CiError(str(e)) from None


def engine_steps(ctx):
    """Configures, builds and tests the engine once per compiler of ENGINE_COMPILERS, each in its own build folder
    made fresh. Passes with a note while the engine has no CMakeLists.txt."""
    root = engine_root(ctx.repo)
    source = ctx.repo.joinpath(*root.split("/"))
    if not (source / "CMakeLists.txt").is_file():
        note = f"no {root}/CMakeLists.txt yet: nothing to build"
        return [Step(f"{root}/CMakeLists.txt", call=lambda ctx, run: (0, [note]))]
    found = {name: scan.find_program(name) for name in (*ENGINE_TOOLS, *ENGINE_COMPILERS.values())}
    missing = [name for name, path in found.items() if not path]
    if missing:
        raise CiError(f"the engine job needs {', '.join(missing)} on PATH")
    cmake, ctest = found["cmake"], found["ctest"]
    steps = []
    for name, cxx in ENGINE_COMPILERS.items():
        build = str(ctx.work() / f"engine-{name}")
        configure = [cmake, "--fresh", "-S", str(source), "-B", build, "-G", "Ninja"]
        steps += [
            Step(f"configure with {cxx}", configure, cwd=ctx.repo, env={"CXX": found[cxx]}),
            Step(f"build with {cxx}", [cmake, "--build", build], cwd=ctx.repo),
            Step(f"tests built with {cxx}", [ctest, "--test-dir", build, "--output-on-failure", "--no-tests=error"],
                 cwd=ctx.repo),
        ]  # fmt: skip
    return steps


def workflow_steps(ctx):
    return [Step("workflows run every job, pinned", call=check_workflows)]


JOBS = {
    j.name: j
    for j in (
        Job("workflow", "the workflows run each job under its name, pin every action, set permissions", workflow_steps),
        Job("tests", "the coney-tools unit tests", tests_steps),
        Job("disclosure", "commit messages (and given texts) in neutral words", disclosure_steps),
        Job("markdownlint", "every Markdown file, with the commit hook's version and rules", markdownlint_steps),
        Job("ruff", "the Python lints and formatting, with the commit hook's ruff version", ruff_steps),
        Job("conventions", "conventions.yml is well formed; the templates and issue forms match it", conventions_steps),
        Job("site", "the documentation site builds with --strict, from hash-locked requirements", site_steps),
        Job("mod", "every mod under mods/ validates and passes its tests on the pinned Claude Code", mod_steps),
        Job("verify", "every research page's verdict, two signatures and hashes", verify_steps),
        Job("orig-check", "every @orig tag names an address and job on a gated page", orig_check_steps),
        Job("commit-sigs", "every engine commit is signed by an allowed implementer key", commit_sigs_steps),
        Job(
            "code-verdict",
            "every changed engine source file is covered by a PR analyst's signed verdict",
            code_verdict_steps,
        ),
        Job("engine", "the engine builds and passes its tests with each compiler", engine_steps),
    )
}


# --- running ----------------------------------------------------------------------------------------------------


def run_program(argv, cwd, env, run=subprocess.run, capture=False):
    """Runs a program with its output on ours; its exit code (with capture, (exit code, its output), the output
    printed too). A program that cannot start raises CiError."""
    sys.stdout.flush()
    extra = {"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT} if capture else {}
    try:
        r = run(argv, cwd=cwd, env=dict(os.environ, **env), stdin=subprocess.DEVNULL, check=False, **extra)
    except OSError as e:
        raise CiError(f"cannot run {Path(argv[0]).name}: {e.__class__.__name__}") from None
    if not capture:
        return r.returncode
    out = (getattr(r, "stdout", None) or b"").decode("utf-8", "replace")
    print(out, end="" if not out or out.endswith("\n") else "\n", flush=True)
    return r.returncode, out


def run_step(ctx, step, run=subprocess.run):
    """0 passed, 1 failed, 2 could not run."""
    if step.call is not None:
        code, lines = step.call(ctx, run)
        for line in lines:
            print(f"  {line}")
        return code
    if step.counted is not None:
        code, out = run_program(step.argv, step.cwd or ctx.repo, step.env, run, capture=True)
        if code == 0 and not any(int(n) for n in step.counted.findall(out)):
            print("  it read no file, so it checked nothing", flush=True)
            return 1
    else:
        code = run_program(step.argv, step.cwd or ctx.repo, step.env, run)
    if code == 0:
        return 0
    return 2 if step.tool and code == 2 else 1


def run_job(ctx, job, run=subprocess.run):
    """Every step of the job, stopping at the first that does not pass. 0, 1 or 2."""
    print(f"== {job.name}: {job.what}", flush=True)
    try:
        for step in job.steps(ctx):
            print(f"-- {job.name}: {step.what}", flush=True)
            code = run_step(ctx, step, run)
            if code:
                return code
    except CiError as e:
        print(f"  could not run: {e}", flush=True)
        return 2
    return 0


def run_jobs(ctx, names, run=subprocess.run):
    """Runs the named jobs in order; (exit code, summary lines). Exit 2 when any job could not run, else 1 when any
    failed, else 0."""
    results = [(name, run_job(ctx, JOBS[name], run)) for name in names]
    words = {0: "PASS", 1: "FAIL", 2: "COULD NOT RUN"}
    lines = [f"{words[code]:13} {name}" for name, code in results]
    codes = {code for _, code in results}
    return (2 if 2 in codes else 1 if 1 in codes else 0), lines


def python_note(want):
    have = f"{sys.version_info[0]}.{sys.version_info[1]}"
    return None if have == want else f"note: CI runs Python {want}; this is Python {have}"
