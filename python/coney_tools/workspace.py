"""The workspace config: every machine path of the zones in one local file, outside the repository.

The file is JSON. Its `zones_file` (a path relative to the workspace file) lists the root folders, each with a
`role`; every other path in the workspace is written `<role>:<path under that root>` and resolved against them.
Where the file is: `--workspace PATH`, else the `CONEY_WORKSPACE` environment variable, else
`%ProgramData%\\Coney\\coney.workspace.json`.
"""

import json
import ntpath
import os
import re
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath

ENV = "CONEY_WORKSPACE"
DEFAULT_NAME = ("Coney", "coney.workspace.json")  # under %ProgramData%
VERSION = 1


class WorkspaceError(Exception):
    """The workspace or zones file is missing, unreadable or malformed (the command could not run: exit 2)."""


def default_location():
    base = os.environ.get("ProgramData")
    if not base:
        raise WorkspaceError("no workspace given: use --workspace, set CONEY_WORKSPACE, or set ProgramData")
    return Path(base, *DEFAULT_NAME)


def locate(arg=None):
    """The workspace file: the argument, else CONEY_WORKSPACE, else the default under %ProgramData%."""
    if arg:
        return Path(arg)
    env = os.environ.get(ENV)
    if env:
        return Path(env)
    return default_location()


def _read_json(path, what):
    try:
        text = Path(path).read_text(encoding="utf-8-sig")
    except OSError as e:
        raise WorkspaceError(f"cannot read the {what} {path}: {e.__class__.__name__}") from None
    try:
        return json.loads(text)
    except ValueError as e:
        raise WorkspaceError(f"the {what} {path} is not valid JSON (line {getattr(e, 'lineno', '?')})") from None


def under(base, rel):
    """`rel`, a relative path as the workspace writes it (Windows style, \\ or / between the names), joined to `base`
    with this system's separator, so a workspace means the same folders on any system."""
    return Path(base, *PureWindowsPath(rel).parts)


def expand(path):
    """A machine path from the workspace with its environment variables expanded, Windows style (%NAME%, and $NAME)
    on every system, since the workspace describes a Windows machine."""
    return ntpath.expandvars(path)


def _want(cond, msg):
    if not cond:
        raise WorkspaceError(msg)


def _names(value, where):
    _want(isinstance(value, list) and all(isinstance(x, str) and x for x in value), f"{where}: a list of names")
    return value


@dataclass
class Workspace:
    path: Path
    data: dict
    zones_path: Path
    zones: dict  # the zones file
    roots: dict = field(default_factory=dict)  # role -> root entry

    # -- resolution --------------------------------------------------------------------------------------------

    def split(self, spec, where="path"):
        """'<role>:<rel>' -> (role, rel), checked: a known role, and a relative path that stays under the root."""
        _want(isinstance(spec, str) and ":" in spec, f"{where}: '{spec}' is not '<role>:<path>'")
        # a tab or other control character is almost always a backslash that a script turned into one
        _want(not any(ord(c) < 32 for c in spec), f"{where}: '{spec!r}' holds a control character")
        role, rel = spec.split(":", 1)
        _want(role in self.roots, f"{where}: unknown role '{role}'")
        p = PureWindowsPath(rel)
        _want(not p.anchor and ".." not in p.parts, f"{where}: '{rel}' must be relative and stay under its root")
        return role, rel

    def resolve(self, spec, where="path"):
        role, rel = self.split(spec, where)
        return under(self.roots[role]["path"], rel)

    def where(self, key):
        """A path from the workspace's `paths` table, resolved."""
        paths = self.data["paths"]
        _want(key in paths, f"the workspace has no path '{key}'")
        return self.resolve(paths[key], f"paths.{key}")

    def has_path(self, key):
        return key in self.data["paths"]

    def root(self, role):
        _want(role in self.roots, f"the zones file has no root with role '{role}'")
        return Path(self.roots[role]["path"])

    # -- accounts and settings ------------------------------------------------------------------------------------

    def zone_account(self, zone):
        z = self.data["zones"].get(zone)
        _want(z is not None, f"the workspace has no zone '{zone}'")
        return z["account"]

    def analyst_side_accounts(self):
        """Accounts whose zone folder lies in the analyst root (the analyst and the reviewers): they may reach the
        analyst's local ports."""
        return {z["account"].lower() for z in self.data["zones"].values() if self.split(z["folder"])[0] == "analyst"}

    @property
    def accounts(self):
        return list(self.zones["accounts"])

    @property
    def maintainer(self):
        return self.zones.get("maintainer")

    @property
    def ports(self):
        return list(self.data["analyst_ports"])

    @property
    def listener_exceptions(self):
        """Ports an analyst-side account may listen on without the port wall: each named, with why it is harmless."""
        return list(self.data.get("listener_exceptions", []))

    @property
    def check(self):
        return self.data["check"]

    @property
    def install(self):
        return self.data.get("install") or {}

    @property
    def claude_settings(self):
        """What every zone's and container's managed Claude Code settings must hold (settings_problems)."""
        return {k: v for k, v in (self.data.get("claude_settings") or {}).items() if k != "comment"}

    def claude_program(self, account):
        """Where an account's Claude Code program is (claude_program.path, '{account}' filled in, environment
        variables expanded), or None when the workspace names none."""
        path = (self.data.get("claude_program") or {}).get("path")
        return expand(path.replace("{account}", account)) if path else None

    @property
    def legacy_scan(self):
        """The legacy-copy scan's table (checked by load), or None when the workspace has none."""
        return self.data.get("legacy_scan")

    @property
    def withdrawn(self):
        """Withdrawn research (checked by load): {"verdicts": verdict hashes, "pages": page hashes}, sets of
        lowercase hex, empty when the workspace lists none. No bundle holding either hash is imported."""
        items = self.data.get("withdrawn", [])
        return {
            "verdicts": {x["verdict_sha256"] for x in items if "verdict_sha256" in x},
            "pages": {x["page_sha256"] for x in items if "page_sha256" in x},
        }

    @property
    def containers(self):
        """The implementer containers' table (checked by load); WorkspaceError when the workspace has none."""
        c = self.data.get("containers")
        _want(c is not None, f"the workspace {self.path} has no 'containers' table")
        return c


SIZE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)([bkmg]?)$", re.IGNORECASE)  # Docker's sizes: 5g, 512m, 1073741824
SIZE_UNITS = {"": 1, "b": 1, "k": 1 << 10, "m": 1 << 20, "g": 1 << 30}
DISTRO = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
POSIX_DIR = re.compile(r"^(/[A-Za-z0-9._-]+)+$")
POOL_NAME = re.compile(r"^impl-[1-9][0-9]{0,2}$")
MODEL = re.compile(r"^claude-[a-z0-9]+(-[a-z0-9]+)*$")  # a model id as Claude Code's --model takes it
EFFORTS = ("low", "medium", "high", "xhigh", "max")  # Claude Code's --effort levels
SHA256 = re.compile(r"^[0-9a-f]{64}$")
WITHDRAWN_KEYS = ("verdict_sha256", "page_sha256")


def size_bytes(value):
    """A Docker size (an int of bytes, or text such as '5g') in bytes. WorkspaceError when it is not one."""
    if isinstance(value, int) and not isinstance(value, bool) and value > 0:
        return value
    m = SIZE.match(value.strip()) if isinstance(value, str) else None
    _want(m is not None, f"not a size: {value!r} (bytes, or a number with b, k, m or g)")
    n = int(float(m.group(1)) * SIZE_UNITS[m.group(2).lower()])
    _want(n > 0, f"not a size: {value!r}")
    return n


def settings_problems(text, want):
    """Each way the Claude Code settings text fails to hold what is wanted; empty when it holds. An object must hold
    each wanted key, a list each wanted item (others may be there too), anything else the same value."""
    try:
        got = json.loads(text)
    except ValueError:
        return ["the settings are not JSON"]
    if not isinstance(got, dict):
        return ["the settings are not a JSON object"]
    return _subset_problems(got, want, "")


def _subset_problems(got, want, where):
    if isinstance(want, dict):
        if not isinstance(got, dict):
            return [f"{where} is not an object"]
        out = []
        for k, v in want.items():
            path = f"{where}.{k}" if where else k
            out += _subset_problems(got[k], v, path) if k in got else [f"{path} is unset"]
        return out
    if isinstance(want, list):
        if not isinstance(got, list):
            return [f"{where} is not a list"]
        return [f"{where} lacks {v!r}" for v in want if v not in got]
    return [] if got == want else [f"{where} is {got!r}, not {want!r}"]


def _validate_containers(ws, c, w):
    """The implementer containers' table: the WSL2 distro, the folder inside it, the pool, caps, the auth file, and
    the model and effort every session runs at."""
    _want(isinstance(c, dict), f"{w}: 'containers' must be an object")
    _want(isinstance(c.get("distro"), str) and DISTRO.match(c["distro"]), f"{w}: containers.distro must be a name")
    root = c.get("wsl_root")
    ok = isinstance(root, str) and POSIX_DIR.match(root) and ".." not in root.split("/") and "." not in root.split("/")
    _want(ok, f"{w}: containers.wsl_root must be an absolute POSIX folder other than / (letters, digits, . _ -)")
    pool = c.get("pool")
    _want(
        isinstance(pool, list) and pool and all(isinstance(n, str) and POOL_NAME.match(n) for n in pool),
        f"{w}: containers.pool must be a non-empty list of names impl-<n>",
    )
    _want(len(set(pool)) == len(pool), f"{w}: containers.pool names a container twice")
    cpus = c.get("cpus")
    _want(
        isinstance(cpus, (int, float)) and not isinstance(cpus, bool) and 0 < cpus <= 1024,
        f"{w}: containers.cpus must be a number of CPUs above 0",
    )
    try:
        size_bytes(c.get("memory"))
    except WorkspaceError:
        raise WorkspaceError(f"{w}: containers.memory must be a size such as 5g") from None
    ws.split(c.get("auth_file"), f"{w}: containers.auth_file")
    model = c.get("model")
    _want(isinstance(model, str) and MODEL.match(model), f"{w}: containers.model must be a model id claude-...")
    _want(c.get("effort") in EFFORTS, f"{w}: containers.effort must be one of {', '.join(EFFORTS)}")


def _validate_legacy_scan(ws, c, w):
    """The legacy-copy scan: the folder scanned, the old-file hash list, the cache, the folders skipped (each with
    why) and the seconds one run may spend scanning."""
    _want(isinstance(c, dict), f"{w}: 'legacy_scan' must be an object")
    for k in ("root", "hashes", "cache"):
        ws.split(c.get(k), f"{w}: legacy_scan.{k}")
    skip = c.get("skip")
    _want(isinstance(skip, list), f"{w}: legacy_scan.skip must be a list")
    for i, s in enumerate(skip):
        ok = isinstance(s, dict) and isinstance(s.get("why"), str) and s["why"]
        _want(ok, f"{w}: legacy_scan.skip[{i}] needs a 'path' and a 'why'")
        ws.split(s.get("path"), f"{w}: legacy_scan.skip[{i}].path")
    # Optional: folders of the game's own material, whose files' hashes leave the list, and an allow list
    original = c.get("original", [])
    _want(isinstance(original, list), f"{w}: legacy_scan.original must be a list")
    for i, s in enumerate(original):
        ok = isinstance(s, dict) and isinstance(s.get("why"), str) and s["why"]
        _want(ok, f"{w}: legacy_scan.original[{i}] needs a 'path' and a 'why'")
        ws.split(s.get("path"), f"{w}: legacy_scan.original[{i}].path")
    if "allow" in c:
        ws.split(c.get("allow"), f"{w}: legacy_scan.allow")
    b = c.get("budget_seconds")
    _want(
        isinstance(b, int) and not isinstance(b, bool) and b > 0,
        f"{w}: legacy_scan.budget_seconds must be a whole number of seconds, 1 or more",
    )


def _validate_withdrawn(items, w):
    """Withdrawn research: each entry a verdict hash, a page hash or both (64 lowercase hex), and why."""
    _want(isinstance(items, list), f"{w}: 'withdrawn' must be a list")
    for i, x in enumerate(items):
        ok = isinstance(x, dict) and isinstance(x.get("why"), str) and x["why"]
        _want(ok and any(k in x for k in WITHDRAWN_KEYS), f"{w}: withdrawn[{i}] needs a hash and a 'why'")
        for k in WITHDRAWN_KEYS:
            if k in x:
                ok = isinstance(x[k], str) and SHA256.match(x[k])
                _want(ok, f"{w}: withdrawn[{i}].{k} must be 64 lowercase hex digits")


def _validate_zones(z, where):
    _want(isinstance(z, dict), f"{where}: must be a JSON object")
    roots = z.get("roots")
    _want(isinstance(roots, list) and roots, f"{where}: 'roots' must be a non-empty list")
    by_role = {}
    for i, r in enumerate(roots):
        w = f"{where}: roots[{i}]"
        _want(isinstance(r, dict), f"{w}: must be an object")
        for k in ("path", "role"):
            _want(isinstance(r.get(k), str) and r[k], f"{w}: missing '{k}'")
        _want(PureWindowsPath(r["path"]).anchor != "", f"{w}: the path must be absolute")
        _want(r["role"] not in by_role, f"{w}: role '{r['role']}' given twice")
        _want(isinstance(r.get("grants", {}), dict), f"{w}: 'grants' must be an object")
        for acct, right in r.get("grants", {}).items():
            _want(right in ("F", "M", "RX"), f"{w}: grant for '{acct}' must be F, M or RX")
        _names(r.get("read", []), f"{w}: 'read'")
        for j, s in enumerate(r.get("sub", [])):
            ws = f"{w}: sub[{j}]"
            _want(isinstance(s, dict) and isinstance(s.get("path"), str) and s["path"], f"{ws}: missing 'path'")
            sp = PureWindowsPath(s["path"])
            _want(not sp.anchor and ".." not in sp.parts, f"{ws}: the path must be relative and stay under its root")
            _want(isinstance(s.get("grants", {}), dict), f"{ws}: 'grants' must be an object")
            for acct, right in s.get("grants", {}).items():
                _want(right in ("F", "M", "RX"), f"{ws}: grant for '{acct}' must be F, M or RX")
            for k in ("read", "add_only", "add_only_inherit"):
                _names(s.get(k, []), f"{ws}: '{k}'")
            if "sealed" in s:
                # A sealed sub-folder: inheritance cut, one account modifies it, nobody else from the root gets in
                _want(isinstance(s["sealed"], str) and s["sealed"], f"{ws}: 'sealed' must name one account")
                others = [k for k in ("grants", "read", "add_only", "add_only_inherit", "when_exists") if s.get(k)]
                _want(not others, f"{ws}: a sealed sub-folder takes no {', '.join(others)}")
        by_role[r["role"]] = r
    _names(z.get("accounts"), f"{where}: 'accounts'")
    _want(z.get("maintainer") is None or isinstance(z["maintainer"], str), f"{where}: 'maintainer' must be a name")
    return by_role


def load(arg=None):
    """Loads and validates the workspace and its zones file. Raises WorkspaceError."""
    path = locate(arg)
    data = _read_json(path, "workspace file")
    w = f"workspace {path}"
    _want(isinstance(data, dict), f"{w}: must be a JSON object")
    _want(data.get("version") == VERSION, f"{w}: 'version' must be {VERSION}")
    for k, t in (("zones_file", str), ("paths", dict), ("zones", dict), ("analyst_ports", list), ("check", dict)):
        _want(isinstance(data.get(k), t), f"{w}: missing or malformed '{k}'")
    zp = PureWindowsPath(data["zones_file"])
    _want(not zp.anchor, f"{w}: 'zones_file' must be relative to the workspace file")
    zones_path = under(path.parent, data["zones_file"])
    zones = _read_json(zones_path, "zones file")
    ws = Workspace(path, data, zones_path, zones, _validate_zones(zones, f"zones file {zones_path}"))
    for k, v in data["paths"].items():
        ws.split(v, f"{w}: paths.{k}")
    for name, z in data["zones"].items():
        _want(isinstance(z, dict) and isinstance(z.get("account"), str) and z["account"], f"{w}: zones.{name}.account")
        ws.split(z.get("folder"), f"{w}: zones.{name}.folder")
    for i, p in enumerate(data["analyst_ports"]):
        ok = isinstance(p, dict) and isinstance(p.get("port"), int) and 0 < p["port"] < 65536
        _want(ok, f"{w}: analyst_ports[{i}] needs a 'port' from 1 to 65535")
    exceptions = data.get("listener_exceptions", [])
    _want(isinstance(exceptions, list), f"{w}: 'listener_exceptions' must be a list")
    for i, x in enumerate(exceptions):
        ok = isinstance(x, dict) and isinstance(x.get("port"), int) and 0 < x["port"] < 65536
        _want(
            ok and isinstance(x.get("why"), str) and x["why"],
            f"{w}: listener_exceptions[{i}] needs a 'port' and a 'why'",
        )
    for k in ("cache_minutes", "schedule_minutes"):
        v = data["check"].get(k)
        _want(isinstance(v, int) and v > 0, f"{w}: check.{k} must be a whole number of minutes, 1 or more")
    inst = data.get("install", {})
    _want(isinstance(inst, dict), f"{w}: 'install' must be an object")
    if "items" in inst:
        _names(inst["items"], f"{w}: install.items")
    _want(isinstance(data.get("claude_settings", {}), dict), f"{w}: 'claude_settings' must be an object")
    prog = data.get("claude_program", {})
    _want(
        isinstance(prog, dict)
        and ("path" not in prog or (isinstance(prog["path"], str) and "{account}" in prog["path"])),
        f"{w}: 'claude_program.path' must be a path naming '{{account}}'",
    )
    if "containers" in data:
        _validate_containers(ws, data["containers"], w)
    if "legacy_scan" in data:
        _validate_legacy_scan(ws, data["legacy_scan"], w)
    _validate_withdrawn(data.get("withdrawn", []), w)
    return ws
