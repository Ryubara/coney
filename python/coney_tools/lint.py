"""markdownlint in the page gate: a page must pass the repository's markdownlint before the first reviewer signs it,
since a signed page can never be reformatted (its hashes would change).

The version is the `rev:` of the markdownlint-cli2 hook in `.pre-commit-config.yaml` and the rules are
`.markdownlint.yaml`, the same files the commit hook and CI use. Both are read from a git ref (`main`) of the checkout
coney-tools runs from, never its working tree; an installed copy, which has no checkout (a reviewer's account cannot
read the repository), reads the snapshot the installer copies from `main` into `lint-config/` beside the package.

markdownlint runs through npx (Node.js) on a copy of the page in a private temp folder. Its output can carry page
text (`[Context: "..."]`), so each finding is reduced to its line and rule; nothing else of it is ever shown.
"""

import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from coney_tools import signing
from coney_tools.cleanroom import scan

RULES = ".markdownlint.yaml"
HOOKS = ".pre-commit-config.yaml"
HOOK_REPO = "https://github.com/DavidAnson/markdownlint-cli2"
PACKAGE = "markdownlint-cli2"
SNAPSHOT = "lint-config"  # <tools>/lint-config/, holding RULES and HOOKS as they stand on main
TIMEOUT = 300  # seconds: the first run in an account downloads the package
VERSION = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?$")
NAME = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")  # a page id: the copy's file name
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
UNSAFE_FOR_CMD = set('%&|<>^"')  # cmd.exe reads these in a .cmd program's path


class LintError(Exception):
    """markdownlint could not run, or its version or rules could not be read (the command could not run: exit 2)."""


@dataclass(frozen=True, order=True)
class Finding:
    line: int
    rule: str  # MDxxx/name

    def __str__(self):
        return f"line {self.line}: {self.rule}"


def checkout_root():
    """The git checkout coney-tools runs from, or None for an installed copy."""
    for root in Path(__file__).resolve().parents:
        if (root / ".git").exists():
            return root
    return None


def snapshot_dir():
    """The installed snapshot: lint-config/ beside the package, or beside the folder that holds it."""
    package = Path(__file__).resolve().parent
    for base in (package.parent, package.parent.parent):
        if (base / SNAPSHOT).is_dir():
            return base / SNAPSHOT
    return package.parent.parent / SNAPSHOT


def pinned_version(hooks, where, repo=None):
    """A hook's version from the hooks file's text: the rev of its repo entry (markdownlint-cli2's unless another
    repo URL is given), without a leading v."""
    repo = repo or HOOK_REPO
    revs, inside = [], False
    for line in hooks.splitlines():
        m = re.match(r"^\s*-\s*repo:\s*['\"]?([^'\"\s#]+)['\"]?\s*(?:#.*)?$", line)
        if m:
            inside = m.group(1) == repo
            continue
        m = re.match(r"^\s*rev:\s*['\"]?v?([^'\"\s#]+)['\"]?\s*(?:#.*)?$", line)
        if inside and m:
            revs.append(m.group(1))
            inside = False
    if len(revs) != 1 or not VERSION.match(revs[0]):
        raise LintError(f"no single release rev for {repo} in {HOOKS} at {where}")
    return revs[0]


def source(ref, checkout=None, snapshot=None, git=None):
    """(markdownlint-cli2 version, rules bytes). From the ref of the checkout when one is given (with git cat-file,
    never the working tree), else from the snapshot folder."""
    if checkout is not None:
        if not ref or ref.startswith("-"):
            raise LintError(f"not a git ref: {ref!r}")
        exe = git or signing.program("git")
        where = f"{ref} in {checkout}"
        files = {}
        for name in (HOOKS, RULES):
            r = signing.git(exe, checkout, "cat-file", "blob", f"{ref}:{name}", ok=(0, 128))
            if r.returncode:
                raise LintError(f"no {name} at {where}")
            files[name] = r.stdout
    else:
        folder = Path(snapshot) if snapshot else snapshot_dir()
        where = str(folder)
        files = {}
        for name in (HOOKS, RULES):
            try:
                files[name] = (folder / name).read_bytes()
            except OSError as e:
                raise LintError(
                    f"cannot read {name} in {folder} (the installed lint-config): {e.__class__.__name__}"
                ) from None
    try:
        hooks = signing.lf(files[HOOKS]).decode("utf-8")
    except UnicodeDecodeError:
        raise LintError(f"{HOOKS} at {where} is not UTF-8") from None
    rules = signing.lf(files[RULES])
    if not rules.strip():
        raise LintError(f"{RULES} at {where} is empty")
    return pinned_version(hooks, where), rules


def npx_program(given=None):
    """npx's path: the one given (--npx), else the first on PATH (npx.cmd on Windows)."""
    if given:
        if not Path(given).is_file():
            raise LintError(f"npx not found at {given}")
        found = str(given)
    else:
        found = scan.find_program("npx", windows=(".cmd", ".exe"))
        if not found:
            raise LintError("npx not found on PATH: install Node.js, or give --npx")
    if Path(found).suffix.lower() in (".cmd", ".bat") and UNSAFE_FOR_CMD & set(found):
        raise LintError(f"npx's path holds a character cmd.exe would read: {found}")
    return found


def run(cmd, cwd):
    """Runs markdownlint (through npx) without a console window; (exit code, standard output and error as text)."""
    env = dict(os.environ, NO_COLOR="1", NPM_CONFIG_UPDATE_NOTIFIER="false", NPM_CONFIG_FUND="false")
    try:
        r = subprocess.run(
            cmd,
            cwd=cwd,
            env=env,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            creationflags=scan.NO_WINDOW,
            timeout=TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise LintError(f"markdownlint took over {TIMEOUT} s and was stopped") from None
    except OSError as e:
        raise LintError(f"cannot run {cmd[0]}: {e.__class__.__name__}") from None
    out = r.stdout.decode("utf-8", "replace") + "\n" + r.stderr.decode("utf-8", "replace")
    return r.returncode, out


def findings(output, file_name):
    """Line and rule of every finding in markdownlint's output, and nothing else: a finding's line starts with the
    file's name, so the page text after the rule (or anywhere else) never decides what is kept."""
    pattern = re.compile(
        r"^(?:\./)?" + re.escape(file_name) + r":(\d+)(?::\d+)?\s+(?:(?:error|warning)\s+)?"
        r"(MD\d{3})/([a-z0-9]+(?:-[a-z0-9]+)*)(?:/[a-z0-9]+(?:-[a-z0-9]+)*)*(?=\s|$)"
    )
    out = set()
    for line in ANSI.sub("", output).splitlines():
        m = pattern.match(line.strip())
        if m:
            out.add(Finding(int(m.group(1)), f"{m.group(2)}/{m.group(3)}"))
    return sorted(out)


class Linter:
    """One per command run: the version and rules are read once, on first use, and the rules written into the
    private temp folder where each page's copy is linted."""

    def __init__(self, tmp, ref, npx=None):
        self.tmp = Path(tmp)
        self.ref = ref
        self.npx = npx
        self.version = None

    def load(self):
        if self.version is None:
            version, rules = source(self.ref, checkout_root())
            self.npx = npx_program(self.npx)
            (self.tmp / RULES).write_bytes(rules)
            self.version = version
        return self

    def check(self, page_id, data):
        """The findings on a page's bytes (linted with LF line ends), as Finding(line, rule)."""
        if not NAME.match(page_id):
            raise LintError(f"not a page id: {page_id}")
        self.load()
        name = f"{page_id}.md"
        copy = self.tmp / name
        copy.write_bytes(signing.lf(data))
        try:
            # Relative names (cwd is the temp folder): nothing in the arguments needs quoting for cmd.exe.
            code, output = run([self.npx, "--yes", f"{PACKAGE}@{self.version}", "--config", RULES, name], self.tmp)
        finally:
            copy.unlink(missing_ok=True)
        if code == 0:
            return []
        found = findings(output, name)
        if code != 1 or not found:
            raise LintError(f"markdownlint ({PACKAGE}@{self.version}) could not lint the page (exit {code})")
        return found
