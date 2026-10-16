"""`zones check`: verifies that the zones' walls stand, from the zones file in the workspace.

Two modes:

- **public** (any account): what this account can open. Every zone root and sub-folder is listed and must open or be
  refused as the zones file grants this account; other accounts' home folders and the maintainer's `.claude` must
  be refused; this account must not get a connection to any analyst port (unless it is on the analyst side): on
  a free port it listens itself and connects to its own listener, which only the port wall can stop. Installed
  config files that differ from their source are warnings.
- **full** (elevated or SYSTEM): every root's and sub-folder's security descriptor is compared with what the zones
  setup applies: owner Administrators, inheritance cut on roots, and the explicit entries as a set; and the port
  wall's filters (wfp.py) are compared with what `zones ports --apply` writes; and every TCP listener of an
  analyst-side account must be on a port the workspace lists, since only listed ports have the wall.

Full mode also scans the analysis zone for exact copies of older Coney files outside the research sweep's quarantine
(legacy.py). A copy found is a failed check of its own, a legacy copy, named by folder: not a breach.

A failed check that gives an account more access than the zones file grants is a breach. Paths and verdicts are
reported, never file contents.
"""

import ctypes
import fnmatch
import json
import os
import re
import socket
import subprocess
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, replace
from pathlib import Path, PureWindowsPath

from coney_tools import legacy, wfp, workspace

USERS = "*S-1-5-32-545"  # BUILTIN\Users, as the zones file writes it: every account
ADMINS = "S-1-5-32-544"
SYSTEM = "S-1-5-18"
OWNER_RIGHTS = "S-1-3-4"
PORT_TIMEOUT = 0.5
LOOPBACKS = ("127.0.0.1", "::1")

# The access masks the zones setup writes (.NET FileSystemRights; Windows adds Synchronize to every allow entry).
SYNCHRONIZE = 0x100000
FULL = 0x1F01FF
MODIFY = 0x0301BF | SYNCHRONIZE
READ_EXECUTE = 0x0200A9 | SYNCHRONIZE
ADD_ONLY = 0x2 | 0x4 | READ_EXECUTE  # create files, create folders, read and execute
RIGHTS = {"F": FULL, "M": MODIFY, "RX": READ_EXECUTE}
INHERIT_ALL = frozenset({"OI", "CI"})
INHERIT_FOLDERS = frozenset({"CI"})
INHERIT_NONE = frozenset()
MANUAL = "maintainer"  # roots with this 'apply' are locked by hand: checked only for what widens access


@dataclass
class Check:
    what: str
    path: str
    want: str
    got: str
    ok: bool
    widens: bool = False  # a failure that gives more access than granted: a breach
    legacy: bool = False  # a failure that is an older Coney file outside the sweep's quarantine: a legacy copy

    def as_json(self):
        doc = {"what": self.what, "path": self.path, "want": self.want, "got": self.got, "ok": self.ok}
        return {**doc, "legacy": True} if self.legacy else doc  # a legacy copy, not a wall: install and pane tell apart


# -- the current account -------------------------------------------------------------------------------------------


def current_account():
    """This process's account name, from its token where possible (an environment variable can be changed)."""
    if os.name == "nt":
        buf = ctypes.create_unicode_buffer(257)
        n = ctypes.c_ulong(len(buf))
        if ctypes.WinDLL("advapi32").GetUserNameW(buf, ctypes.byref(n)):
            return buf.value
    import getpass

    return getpass.getuser()


def is_elevated():
    if os.name != "nt":
        return False
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except (AttributeError, OSError):
        return False


# -- public mode -----------------------------------------------------------------------------------------------------


def probe(path):
    """'open', 'refused' or 'missing', by listing the folder; anything else 'error: <kind>'."""
    try:
        with os.scandir(path) as it:
            next(it, None)
        return "open"
    except PermissionError:
        return "refused"
    except (FileNotFoundError, NotADirectoryError):
        return "missing"
    except OSError as e:
        if getattr(e, "winerror", None) in (3, 21, 53, 67):  # path not found, device not ready, no network path
            return "missing"
        return f"error: {e.__class__.__name__}"


def _low(names):
    return {n.lower() for n in names or ()}


def _has(names, me):
    n = _low(names)
    return me in n or USERS.lower() in n


def _granted(node, me):
    return me in _low((node.get("grants") or {}).keys())


def wall_checks(ws, me, probe=probe, home_base=None):
    """The listing checks for account `me`: which folders it must open, which it must be refused."""
    me = me.lower()
    out = []

    def check(what, path, want):
        got = probe(path)
        ok = got == want or (got == "missing" and want == "refused")
        out.append(Check(what, str(path), want, got, ok, widens=want == "refused" and got == "open"))

    for r in ws.zones["roots"]:
        root_open = _granted(r, me) or _has(r.get("read"), me)
        check("zone root", r["path"], "open" if root_open else "refused")
        for s in r.get("sub") or ():
            p = str(workspace.under(r["path"], s["path"]))
            if s.get("sealed"):
                check("sealed sub-folder", p, "open" if s["sealed"].lower() == me else "refused")
                continue
            if root_open or _granted(s, me) or _has(s.get("read"), me) or _has(s.get("add_only_inherit"), me):
                want = "open"
            elif _has(s.get("add_only"), me):
                continue  # may add files, may not list: nothing to check by listing
            else:
                want = "refused"
            if want == "open" and s.get("when_exists") and probe(p) == "missing":
                continue
            check("zone sub-folder", p, want)

    base = Path(home_base) if home_base else Path.home().parent
    for a in [*ws.accounts, *([ws.maintainer] if ws.maintainer else [])]:
        if a.lower() == me:
            check("own home", base / a, "open")
        else:
            check("other home", base / a, "refused")
    if ws.maintainer:
        check("maintainer .claude", base / ws.maintainer / ".claude", "refused")
    return out


def _connect(host, port, timeout):
    """'connected', 'blocked' (refused by a filter: access denied), 'refused', 'timed out' or 'failed (...)'."""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return "connected"
    except PermissionError:
        return "blocked"
    except ConnectionRefusedError:
        return "refused"
    except TimeoutError:
        return "timed out"
    except OSError as e:
        return f"failed ({getattr(e, 'winerror', None) or e.errno or e.__class__.__name__})"


def _listen(host, port):
    """A listening socket on host:port for this account, or the reason there is none. Exclusive, so that it never
    shares a port an analyst tool already holds."""
    fam = socket.AF_INET6 if ":" in host else socket.AF_INET
    s = socket.socket(fam, socket.SOCK_STREAM)
    try:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            s.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        s.bind((host, port))
        s.listen(1)
        return s, None
    except OSError as e:
        s.close()
        code = getattr(e, "winerror", None) or e.errno
        return None, "port in use" if code in (10048, 10013, 98, 13) else f"cannot listen ({code})"


def port_probe(host, port, timeout=PORT_TIMEOUT):
    """How this account fares against `port` on `host`: '<case>: <outcome>'. When the port is free, this account
    briefly listens on it itself and connects to its own listener: only a filter on the port can stop that. When
    the port is taken (an analyst tool runs), the connection is tried as it is. Either way it must not connect."""
    srv, why = _listen(host, port)
    try:
        return f"{'own listener' if srv else why}: {_connect(host, port, timeout)}"
    finally:
        if srv:
            srv.close()


def port_state(port, probe=port_probe):
    """'<address> <case>: <outcome>' for both loopback addresses, tried at once (on Windows a refused connection
    takes the whole timeout: it is retried)."""
    with ThreadPoolExecutor(len(LOOPBACKS)) as pool:
        got = list(pool.map(lambda h: probe(h, port), LOOPBACKS))
    return "; ".join(f"{h} {g}" for h, g in zip(LOOPBACKS, got, strict=True))


def port_checks(ws, me, state=None):
    """Every analyst port must turn this account away, unless it is on the analyst side."""
    state = state or port_state
    exempt = me.lower() in ws.analyst_side_accounts()
    out, todo = [], []
    want = "no connection"
    for p in ws.ports:
        where = f"localhost:{p['port']} ({p.get('name', 'analyst port')})"
        if exempt:
            out.append(Check("analyst port", where, want, "not checked: analyst-side account", True))
        else:
            todo.append((where, p["port"]))
    if todo:
        with ThreadPoolExecutor(len(todo)) as pool:
            states = list(pool.map(state, [port for _, port in todo]))
        for (where, _), got in zip(todo, states, strict=True):
            bad = "connected" in got
            out.append(Check("analyst port", where, want, got, not bad, widens=bad))
    return out


def _same(a, b):
    try:
        return a.read_bytes() == b.read_bytes()
    except OSError:
        return None


def drift_warnings(ws):
    """Installed copies that differ from the source folder (warnings: the maintainer installs them)."""
    items = ws.install.get("items")
    if not items or not ws.has_path("machine_source") or "machine" not in ws.roots:
        return []
    src, dst = ws.where("machine_source"), ws.root("machine")
    try:
        if not src.is_dir() or not dst.is_dir() or src.resolve() == dst.resolve():
            return []
    except OSError:
        return []
    skip = ws.install.get("skip") or []
    pairs = []
    for item in items:
        s = src / item
        if s.is_file():
            pairs.append((s, dst / item))
        elif s.is_dir():
            for f in sorted(s.rglob("*")):
                if f.is_file() and not any(fnmatch.fnmatch(f.name, pat) for pat in skip):
                    pairs.append((f, dst / f.relative_to(src)))
    managed = ws.install.get("managed_settings")
    if managed and (src / "managed-settings.json").is_file():
        pairs.append((src / "managed-settings.json", Path(workspace.expand(managed))))
    out = []
    for s, d in pairs:
        if not d.exists():
            out.append(f"drift: {d} is not installed (source {s})")
        elif _same(s, d) is False:
            out.append(f"drift: {d} differs from its source {s}")
    return out


def tcp_listeners():
    """[(address, port, pid)] of every TCP listener on this machine, from `netstat -ano` (a listener's foreign address
    is 0.0.0.0:0 or [::]:0, whatever language the state column is in). Needs no elevation."""
    out = []
    for proto in ("TCP", "TCPv6"):
        r = subprocess.run(["netstat", "-ano", "-p", proto], capture_output=True, text=True, timeout=20, check=False)
        out += parse_listeners(r.stdout)
    return out


def parse_listeners(text):
    found = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 4 or parts[0].upper() != "TCP" or parts[2] not in ("0.0.0.0:0", "[::]:0"):
            continue
        addr, _, port = parts[1].rpartition(":")
        pid = parts[4] if len(parts) > 4 else ""
        if port.isdigit():
            found.append((addr, int(port), int(pid) if pid.isdigit() else None))
    return found


def is_loopback(addr):
    return addr.startswith("127.") or addr in ("[::1]", "::1")


def listener_checks(ws, read=None):
    """Every analyst port must listen on loopback only, or not at all: a listener on any other address is reachable
    from the network, or from this machine through that address, past what loopback-only rules cover."""
    try:
        listening = (read or tcp_listeners)()
    except (OSError, subprocess.SubprocessError) as e:
        return [Check("analyst listener", "netstat", "listeners readable", f"error: {e.__class__.__name__}", False)]
    out = []
    for p in ws.ports:
        where = f"port {p['port']} ({p.get('name', 'analyst port')})"
        addrs = sorted({a for a, port, _ in listening if port == p["port"]})
        wide = [a for a in addrs if not is_loopback(a)]
        got = "not listening" if not addrs else "listening on " + ", ".join(addrs)
        out.append(
            Check("analyst listener", where, "loopback only, or not listening", got, not wide, widens=bool(wide))
        )
    return out


def process_owner(pid):
    """(owner SID, program file name) of a running process, or (None, None) when it cannot be opened. Other
    accounts' processes open only when elevated or as SYSTEM (Windows only)."""
    from ctypes import wintypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    adv = ctypes.WinDLL("advapi32", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    k32.LocalFree.argtypes = [ctypes.c_void_p]
    k32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, wintypes.LPDWORD]
    adv.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    adv.GetTokenInformation.argtypes = [
        wintypes.HANDLE,
        ctypes.c_int,
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.LPDWORD,
    ]
    adv.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.LPWSTR)]
    if not pid:
        return None, None
    proc = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not proc:
        return None, None
    sid = program = None
    try:
        name = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(len(name))
        if k32.QueryFullProcessImageNameW(proc, 0, name, ctypes.byref(size)):
            program = name.value
        token = wintypes.HANDLE()
        if adv.OpenProcessToken(proc, 0x8, ctypes.byref(token)):  # TOKEN_QUERY
            try:
                buf = ctypes.create_string_buffer(256)
                got = wintypes.DWORD()
                if adv.GetTokenInformation(token, 1, buf, len(buf), ctypes.byref(got)):  # TokenUser
                    text = wintypes.LPWSTR()
                    psid = ctypes.c_void_p.from_buffer(buf).value  # TOKEN_USER starts with the SID's address
                    if adv.ConvertSidToStringSidW(psid, ctypes.byref(text)):
                        sid = text.value.upper()
                        k32.LocalFree(ctypes.cast(text, ctypes.c_void_p))
            finally:
                k32.CloseHandle(token)
    finally:
        k32.CloseHandle(proc)
    return sid, program


def unlisted_listener_checks(ws, sid_of, read=None, owner_of=None, walled=None):
    """Every TCP listener of an analyst-side account must be on a listed port: one in analyst_ports, which the port
    wall covers, or a named listener exception. Any other is open to every account on this machine, so it is a
    breach until its port is listed and installed. The one other case: a loopback listener of the account's own Claude
    Code program while the wall's program filters for that account stand (`walled`: {SID: program path}), since then
    it accepts no loopback connection at all. Needs elevation to see other accounts' processes."""
    try:
        listening = (read or tcp_listeners)()
        sides = {sid_of(a).upper(): a for a in ws.analyst_side_accounts()}
    except (OSError, subprocess.SubprocessError) as e:
        return [Check("unlisted listener", "analyst-side accounts", "listeners readable", f"error: {e}", False)]
    walled = {s.upper(): p.lower() for s, p in (walled or {}).items()}
    listed = {p["port"] for p in ws.ports} | {x["port"] for x in ws.listener_exceptions}
    owners, found, fenced = {}, {}, {}
    for addr, port, pid in listening:
        if port in listed:
            continue
        if pid not in owners:
            owners[pid] = (owner_of or process_owner)(pid)
        sid, program = owners[pid]
        if sid not in sides:
            continue
        name = PureWindowsPath(program).name if program else f"process {pid}"
        if program and walled.get(sid) == program.lower() and is_loopback(addr):
            fenced.setdefault((port, sides[sid], name), set()).add(addr)
        else:
            found.setdefault((port, sides[sid], name), set()).add(addr)
    out = [
        Check(
            "walled listener",
            f"port {port} ({acct}, {prog})",
            "its program's filters stand",
            "listening on " + ", ".join(sorted(addrs)) + "; accepts no loopback connection",
            True,
        )
        for (port, acct, prog), addrs in sorted(fenced.items())
    ]
    if not found:
        return [*out, Check("unlisted listener", "analyst-side accounts", "every port listed", "none unlisted", True)]
    return out + [
        Check(
            "unlisted listener",
            f"port {port} ({acct}, {prog})",
            "listed in analyst_ports",
            "listening on " + ", ".join(sorted(addrs)),
            False,
            widens=True,
        )
        for (port, acct, prog), addrs in sorted(found.items())
    ]


def program_specs(ws, sid_of, app_of):
    """The wall's program filters (wfp.program_blocks) for every analyst-side account whose Claude Code program is
    there, as [(account, SID, program path, [FilterSpec])]. An account without the program has nothing to wall; once
    it gets one, the full check finds its filters missing until the wall is applied again."""
    out = []
    for a in sorted(ws.analyst_side_accounts()):
        path = ws.claude_program(a)
        if not path:
            continue
        app = app_of(path)
        if app is not None:
            sid = sid_of(a).upper()
            out.append((a, sid, path, wfp.program_blocks(sid, app)))
    return out


def walled_programs(programs, have):
    """{SID: program path} for each account whose program filters all stand in `have`."""
    return {sid: path for _, sid, path, specs in programs if all(s in have for s in specs)}


def settings_env_checks(ws, read=None):
    """The installed machine-wide Claude Code settings must hold claude_settings, for every zone."""
    managed = ws.install.get("managed_settings")
    if not ws.claude_settings or not managed:
        return []
    path = Path(workspace.expand(managed))
    try:
        text = (read or (lambda p: p.read_text(encoding="utf-8")))(path)
    except OSError as e:
        return [Check("claude settings", str(path), "claude_settings", f"unreadable: {e.__class__.__name__}", False)]
    bad = workspace.settings_problems(text, ws.claude_settings)
    return [Check("claude settings", str(path), "claude_settings", "; ".join(bad) or "all held", not bad)]


def public_run(ws, me, probe=probe, home_base=None, port_state=None, listeners=None, settings=None):
    checks = wall_checks(ws, me, probe, home_base) + port_checks(ws, me, port_state) + listener_checks(ws, listeners)
    checks += settings_env_checks(ws, settings)
    return checks, drift_warnings(ws)


# -- full mode: security descriptors ---------------------------------------------------------------------------------

SID_ALIASES = {
    "AN": "S-1-5-7",
    "AO": "S-1-5-32-548",
    "AU": "S-1-5-11",
    "BA": ADMINS,
    "BG": "S-1-5-32-546",
    "BO": "S-1-5-32-551",
    "BU": "S-1-5-32-545",
    "CA": None,  # domain-relative: never expected here
    "CG": "S-1-3-1",
    "CO": "S-1-3-0",
    "IU": "S-1-5-4",
    "LS": "S-1-5-19",
    "NS": "S-1-5-20",
    "NU": "S-1-5-2",
    "OW": OWNER_RIGHTS,
    "PU": "S-1-5-32-547",
    "RD": "S-1-5-32-555",
    "SO": "S-1-5-32-549",
    "SU": "S-1-5-6",
    "SY": SYSTEM,
    "WD": "S-1-1-0",
    "AC": "S-1-15-2-1",
    "RC": "S-1-5-12",
    "WR": "S-1-5-33",
}

RIGHT_ALIASES = {
    "GA": 0x10000000,
    "GR": 0x80000000,
    "GW": 0x40000000,
    "GX": 0x20000000,
    "RC": 0x20000,
    "SD": 0x10000,
    "WD": 0x40000,
    "WO": 0x80000,
    "CC": 0x1,
    "DC": 0x2,
    "LC": 0x4,
    "SW": 0x8,
    "RP": 0x10,
    "WP": 0x20,
    "DT": 0x40,
    "LO": 0x80,
    "CR": 0x100,
    "FA": 0x1F01FF,
    "FR": 0x120089,
    "FW": 0x120116,
    "FX": 0x1200A0,
    "KA": 0xF003F,
    "KR": 0x20019,
    "KW": 0x20006,
    "KX": 0x20019,
}

ACE_TYPES = {"A": "allow", "D": "deny"}


class SddlError(ValueError):
    pass


@dataclass(frozen=True)
class Ace:
    sid: str
    kind: str  # allow, deny, or the SDDL type for anything else
    mask: int
    flags: frozenset  # inheritance flags (OI, CI, NP, IO), without ID

    def text(self, names=None):
        who = self.sid if not names or self.sid not in names else f"{names[self.sid]} ({self.sid})"
        return f"{self.kind} {who} 0x{self.mask:x} {''.join(sorted(self.flags)) or '-'}"


@dataclass
class Descriptor:
    owner: str | None
    protected: bool
    explicit: list
    inherited: list


def _sid(text):
    t = text.strip()
    if t.upper().startswith("S-"):
        return t.upper()
    if SID_ALIASES.get(t):
        return SID_ALIASES[t]
    raise SddlError(f"unknown account alias '{t}'")


def _mask(text):
    t = text.strip()
    if t.lower().startswith("0x"):
        return int(t, 16)
    if t.isdigit():
        return int(t)
    if len(t) % 2:
        raise SddlError(f"bad rights '{t}'")
    mask = 0
    for i in range(0, len(t), 2):
        code = t[i : i + 2]
        if code not in RIGHT_ALIASES:
            raise SddlError(f"unknown right '{code}'")
        mask |= RIGHT_ALIASES[code]
    return mask


def parse_sddl(sddl):
    """Owner, DACL protection and the DACL's entries from an SDDL string (owner and DACL parts)."""
    s = sddl.strip()
    om = re.search(r"O:(.*?)(?=[GDS]:|$)", s)
    owner = _sid(om.group(1)) if om and om.group(1) else None
    dm = re.search(r"D:([A-Z_]*)((?:\([^()]*\))*)", s)
    if not dm:
        raise SddlError("no DACL")
    dflags = dm.group(1)
    explicit, inherited = [], []
    for body in re.findall(r"\(([^()]*)\)", dm.group(2)):
        parts = body.split(";")
        if len(parts) < 6:
            raise SddlError("short entry")
        t, fl, rights, sid = parts[0], parts[1], parts[2], parts[5]
        if len(fl) % 2:
            raise SddlError(f"bad entry flags '{fl}'")
        flags = {fl[i : i + 2] for i in range(0, len(fl), 2)}
        ace = Ace(_sid(sid), ACE_TYPES.get(t, t), _mask(rights), frozenset(flags - {"ID"}))
        (inherited if "ID" in flags else explicit).append(ace)
    return Descriptor(owner, "P" in dflags.replace("NO_ACCESS_CONTROL", ""), explicit, inherited)


def merge(aces):
    """Entries for one account, kind and inheritance merged into one, as Windows' rule lists merge them."""
    out = {}
    for a in aces:
        key = (a.sid, a.kind, a.flags)
        out[key] = out.get(key, 0) | a.mask
    return {Ace(sid, kind, mask, flags) for (sid, kind, flags), mask in out.items()}


def expected_root(root, sid_of):
    """The explicit entries the zones setup writes on a root."""
    aces = [Ace(SYSTEM, "allow", FULL, INHERIT_ALL), Ace(ADMINS, "allow", FULL, INHERIT_ALL)]
    aces += [Ace(sid_of(n), "allow", RIGHTS[r], INHERIT_ALL) for n, r in (root.get("grants") or {}).items()]
    aces += [Ace(sid_of(n), "allow", READ_EXECUTE, INHERIT_ALL) for n in root.get("read") or ()]
    if not root.get("no_owner_rights"):
        aces.append(Ace(OWNER_RIGHTS, "allow", MODIFY, INHERIT_ALL))
    return merge(aces)


def expected_sub(sub, sid_of, maintainer=None):
    """The explicit entries the zones setup adds on a sub-folder (everything else it inherits from the root). A sealed
    one inherits nothing: SYSTEM, Administrators and the maintainer Full, its one account and OWNER RIGHTS Modify."""
    if sub.get("sealed"):
        aces = [Ace(SYSTEM, "allow", FULL, INHERIT_ALL), Ace(ADMINS, "allow", FULL, INHERIT_ALL)]
        if maintainer:
            aces.append(Ace(sid_of(maintainer), "allow", FULL, INHERIT_ALL))
        aces += [
            Ace(sid_of(sub["sealed"]), "allow", MODIFY, INHERIT_ALL),
            Ace(OWNER_RIGHTS, "allow", MODIFY, INHERIT_ALL),
        ]
        return merge(aces)
    aces = [Ace(sid_of(n), "allow", RIGHTS[r], INHERIT_ALL) for n, r in (sub.get("grants") or {}).items()]
    aces += [Ace(sid_of(n), "allow", READ_EXECUTE, INHERIT_ALL) for n in sub.get("read") or ()]
    aces += [Ace(sid_of(n), "allow", ADD_ONLY, INHERIT_NONE) for n in sub.get("add_only") or ()]
    aces += [Ace(sid_of(n), "allow", ADD_ONLY, INHERIT_FOLDERS) for n in sub.get("add_only_inherit") or ()]
    return merge(aces)


def _widens_if_missing(ace):
    # Without the OWNER RIGHTS entry a file's owner may change its permissions; a missing deny opens access too.
    return ace.sid == OWNER_RIGHTS or ace.kind != "allow"


def compare(path, sd, want_aces, *, root, names=None):
    """Checks for one folder: owner, inheritance, and the explicit entries as a set."""
    p = str(path)
    out = [
        Check("owner", p, f"Administrators ({ADMINS})", str(sd.owner), sd.owner == ADMINS, sd.owner != ADMINS),
    ]
    if root:
        got = "cut" if sd.protected else "inherited"
        out.append(Check("inheritance", p, "cut", got, sd.protected, widens=not sd.protected))
    else:
        got = "cut" if sd.protected else "inherited"
        out.append(Check("inheritance", p, "inherited", got, not sd.protected))
    have = merge(sd.explicit)
    missing, extra = want_aces - have, have - want_aces
    for a in sorted(missing, key=lambda x: x.text()):
        out.append(Check("entry", p, a.text(names), "missing", False, widens=_widens_if_missing(a)))
    for a in sorted(extra, key=lambda x: x.text()):
        out.append(Check("entry", p, "absent", a.text(names), False, widens=a.kind == "allow"))
    if not missing and not extra:
        n = len(want_aces)
        out.append(Check("entries", p, f"{n} explicit entries", f"{n} explicit entries", True))
    return out


def compare_manual(path, sd, allowed_sids, names=None):
    """A root locked by hand: inheritance cut and no allow entry for anyone outside its grants, SYSTEM and
    Administrators. Masks and owner are the maintainer's."""
    p = str(path)
    out = [Check("inheritance", p, "cut", "cut" if sd.protected else "inherited", sd.protected, not sd.protected)]
    others = {a.text(names) for a in sd.explicit + sd.inherited if a.kind == "allow" and a.sid not in allowed_sids}
    for t in sorted(others):
        out.append(Check("entry", p, "absent", t, False, widens=True))
    if not others:
        out.append(Check("entries", p, "grants, SYSTEM and Administrators only", "as wanted", True))
    return out


def drive_exists(path):
    drive = PureWindowsPath(path).drive
    return not drive or os.path.exists(drive + "\\")


def full_run(ws, read_sd, sid_of, drive_exists=drive_exists):
    """Every root and sub-folder of the zones file against what the zones setup applies."""
    checks, warnings = [], []
    names = {}

    def sid(name):
        s = name[1:].upper() if name.startswith("*") else sid_of(name)
        names[s] = name
        return s

    names.update({ADMINS: "Administrators", SYSTEM: "SYSTEM", OWNER_RIGHTS: "OWNER RIGHTS"})

    def read(path):
        try:
            return parse_sddl(read_sd(str(path))), None
        except FileNotFoundError:
            return None, "missing"
        except PermissionError:
            return None, "refused"
        except SddlError as e:
            return None, f"unreadable descriptor: {e}"
        except OSError as e:
            return None, f"error: {getattr(e, 'winerror', None) or e.__class__.__name__}"

    for r in ws.zones["roots"]:
        path = r["path"]
        if not drive_exists(path):
            warnings.append(f"{path}: drive {PureWindowsPath(path).drive} is missing; not checked")
            checks.append(Check("zone root", path, "present", "drive missing", True))
            continue
        try:
            want = expected_root(r, sid)
            allowed = (
                {SYSTEM, ADMINS} | {sid(n) for n in (r.get("grants") or {})} | {sid(n) for n in r.get("read") or ()}
            )
            subs = [(s, expected_sub(s, sid, ws.maintainer)) for s in r.get("sub") or ()]
        except OSError as e:
            checks.append(Check("accounts", path, "every account resolves", f"error: {e}", False))
            continue
        sd, err = read(path)
        if err:
            checks.append(Check("zone root", path, "present", err, False))
            continue
        if r.get("apply") == MANUAL:
            checks += compare_manual(path, sd, allowed, names)
            continue
        checks += compare(path, sd, want, root=True, names=names)
        for s, swant in subs:
            sp = str(workspace.under(path, s["path"]))
            ssd, err = read(sp)
            if err == "missing" and s.get("when_exists"):
                checks.append(Check("zone sub-folder", sp, "present when it exists", "missing", True))
                continue
            if err:
                checks.append(Check("zone sub-folder", sp, "present", err, False))
                continue
            checks += compare(sp, ssd, swant, root=bool(s.get("sealed")), names=names)
    return checks, warnings


def wall_filter_checks(ws, sid_of, read_state, read_others=list, read_overriding=lambda weight: [], programs=()):
    """Coney's port-wall filters (see wfp.py) against what `zones ports --apply` writes for the workspace: every
    expected filter present and persistent, nothing else under Coney's provider, and nothing in a sublayer at or
    above Coney's that could overrule its blocks (`read_others` gives every other sublayer as (key, name, weight);
    `read_overriding(weight)` the filters that could, as (filter, sublayer, layer, port)). `programs` is
    program_specs: the program filters expected besides the ports'."""
    where = "Windows Filtering Platform"
    try:
        allowed = {sid_of(a) for a in sorted(ws.analyst_side_accounts())}
    except OSError as e:
        return [Check("port wall", where, "every analyst-side account resolves", f"error: {e}", False)]
    want = wfp.expected([p["port"] for p in ws.ports], allowed)
    want += [s for *_, specs in programs for s in specs]
    try:
        weight, have = read_state()
    except OSError as e:
        return [Check("port wall", where, "filters readable", f"error: {e}", False)]
    out = []
    try:
        above, level = wfp.outranking(weight, read_others())
        risky = read_overriding(weight) if weight is not None else []
    except OSError as e:
        return [Check("port wall", where, "sublayers readable", f"error: {e}", False)]
    w = "Coney's sublayer present, nothing at or above it that could overrule its blocks"
    anywhere = []
    if weight is None:
        got = "missing"
    else:
        got = f"weight 0x{weight:x}"
        if above:
            got += "; above it: " + ", ".join(f"{n or '(unnamed)'} 0x{x:x}" for n, x in above)
        if level:
            got += "; level with it: " + ", ".join(f"{n or '(unnamed)'} 0x{x:x}" for n, x in level)
        anywhere = [r for r in risky if r[5:6] != ("off-loopback",)]
        off = [r for r in risky if r[5:6] == ("off-loopback",)]
        if anywhere:
            got += "; could overrule it: " + ", ".join(f"{r[0]} in {r[1]} ({r[2]}, port {r[3]})" for r in anywhere)
        if off:
            names = sorted({f"{r[0]} in {r[1]}" for r in off})
            got += (
                "; limited to a non-loopback interface, so harmless while the analyst ports listen on loopback only "
                "(the listener check): " + ", ".join(names)
            )
        if not risky and (above or level):
            got += "; none of their filters could overrule it"
    bad = weight is None or bool(anywhere)  # a hard permit or callout at or above Coney's could let a connection in
    out.append(Check("port wall", f"{where} sublayer", w, got, not bad, widens=bad))
    rest = list(have)
    for s in want:
        if s in rest:
            rest.remove(s)
            out.append(Check("port filter", f"{where} {s.target}", s.text(), "present", True))
            continue
        close = [h for h in rest if h.key == s.key]
        got = close[0].text() if close else "missing"
        if close:
            rest.remove(close[0])
        out.append(Check("port filter", f"{where} {s.target}", s.text(), got, False, _widens(s, close)))
    for h in rest:
        out.append(Check("port filter", f"{where} {h.target}", "absent", h.text(), False, h.action != "block"))
    return out


def _widens(want, close):
    """Whether a missing or changed filter lets more through. A missing or changed block does, unless it only lost
    its persistence (it stands until the next restart); a missing permit only shuts the analyst side out; a changed
    permit does when it no longer limits the accounts to the expected ones."""
    have = close[0] if close else None
    if want.action == "block":
        return have is None or replace(have, persistent=True) != want
    return have is not None and (have.users is None or not have.users <= want.users)


def read_wall():
    with wfp.Engine() as engine:
        return wfp.read_state(engine)


def read_other_sublayers():
    with wfp.Engine() as engine:
        return wfp.read_others(engine)


def overriding_filters(ws):
    """read_overriding for wall_filter_checks, over the workspace's analyst ports."""

    def read(weight):
        with wfp.Engine() as engine:
            return wfp.overriding(engine, weight, [p["port"] for p in ws.ports])

    return read


# -- full mode: legacy copies ---------------------------------------------------------------------------------------

LEGACY_WANT = "no older Coney file"
GAP_FOLDERS = 10  # folders named in the warning about files not fully opened


def _many(n, one, more=None):
    return f"{n:,} {one if n == 1 else more or one + 's'}"


def legacy_checks(ws, jobs=None, run=None):
    """(checks, warnings) of the legacy-copy scan: one failed check per folder holding an exact copy of an older Coney
    file, else one passing check; both with what was scanned, cached and skipped. A workspace without a legacy_scan
    table, or a hash list that cannot be used, fails without being a legacy copy."""
    cfg = ws.legacy_scan
    if cfg is None:
        return [Check("legacy copy", "workspace legacy_scan", "configured", "missing", False)], []
    try:
        res = (run or legacy.run)(cfg, lambda spec: ws.resolve(spec, "legacy_scan"), jobs)
    except (legacy.LegacyError, workspace.WorkspaceError) as e:
        return [Check("legacy copy", "legacy_scan.hashes", "hash list readable", f"error: {e}", False)], []
    except OSError as e:
        return [Check("legacy copy", "legacy_scan", "scan runs", f"error: {e.__class__.__name__}", False)], []
    skipped = ", ".join(f"{p} ({why})" for p, why in res.skipped) or "nothing"
    done = (
        f"{_many(res.files, 'file')} ({res.scanned:,} scanned, {res.cached:,} cached"
        + (f", {res.left:,} left for the next run" if res.left else "")
        + f") in {res.seconds:.1f} s; skipped: {skipped}"
    )
    if not os.path.isdir(res.root):
        got = "root missing: nothing to scan"
    elif res.hits:
        got = f"{_many(sum(res.hits.values()), 'exact copy', 'exact copies')} in {_many(len(res.hits), 'folder')}"
    else:
        got = "none"
    checks = [Check("legacy scan", res.root, LEGACY_WANT + " outside the skipped folders", f"{got}; {done}", True)]
    checks += [
        Check("legacy copy", folder, LEGACY_WANT, f"{_many(n, 'exact copy', 'exact copies')}", False, legacy=True)
        for folder, n in sorted(res.hits.items())
    ]
    warnings = []
    if res.left:
        warnings.append(f"legacy scan: {_many(res.left, 'file')} left for the next run (budget spent)")
    if res.gaps:
        names = sorted(res.gaps)
        more = f" and {len(names) - GAP_FOLDERS} more folders" if len(names) > GAP_FOLDERS else ""
        warnings.append(
            f"legacy scan: {_many(sum(res.gaps.values()), 'file')} not fully opened (hash checked, contents partly): "
            + ", ".join(names[:GAP_FOLDERS])
            + more
        )
    if res.unreadable:
        warnings.append(f"legacy scan: {_many(len(res.unreadable), 'path')} could not be listed or read")
    if res.links:
        warnings.append(f"legacy scan: {_many(res.links, 'link')} not followed")
    return checks, warnings


# -- Windows calls ---------------------------------------------------------------------------------------------------

SE_FILE_OBJECT = 1
OWNER_SECURITY_INFORMATION = 0x1
DACL_SECURITY_INFORMATION = 0x4
SDDL_REVISION_1 = 1


def read_sd(path):
    """The owner and DACL of a file or folder as SDDL (Windows only). Raises OSError with the Windows error."""
    from ctypes import wintypes

    adv = ctypes.WinDLL("advapi32", use_last_error=True)
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LocalFree.argtypes = [ctypes.c_void_p]
    k32.LocalFree.restype = ctypes.c_void_p
    adv.GetNamedSecurityInfoW.argtypes = [
        wintypes.LPCWSTR,
        ctypes.c_int,
        wintypes.DWORD,
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ctypes.c_void_p),
    ]
    adv.GetNamedSecurityInfoW.restype = wintypes.DWORD
    adv.ConvertSecurityDescriptorToStringSecurityDescriptorW.argtypes = [
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.LPWSTR),
        ctypes.POINTER(wintypes.ULONG),
    ]
    adv.ConvertSecurityDescriptorToStringSecurityDescriptorW.restype = wintypes.BOOL
    info = OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION
    sd = ctypes.c_void_p()
    rc = adv.GetNamedSecurityInfoW(path, SE_FILE_OBJECT, info, None, None, None, None, ctypes.byref(sd))
    if rc:
        raise _win_error(rc, path)
    try:
        text = wintypes.LPWSTR()
        if not adv.ConvertSecurityDescriptorToStringSecurityDescriptorW(
            sd, SDDL_REVISION_1, info, ctypes.byref(text), None
        ):
            raise _win_error(ctypes.get_last_error(), path)
        try:
            return text.value
        finally:
            k32.LocalFree(ctypes.cast(text, ctypes.c_void_p))
    finally:
        k32.LocalFree(sd)


def _win_error(code, path):
    if code in (2, 3):
        return FileNotFoundError(2, "not found", path)
    if code == 5:
        return PermissionError(13, "access denied", path)
    return OSError(None, f"Windows error {code}", path, code)


def sid_of(name):
    """An account name's SID string (Windows only). Raises OSError when the account does not resolve."""
    from ctypes import wintypes

    adv = ctypes.WinDLL("advapi32", use_last_error=True)
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LocalFree.argtypes = [ctypes.c_void_p]
    adv.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.LPWSTR)]
    sid = ctypes.create_string_buffer(256)
    cb = wintypes.DWORD(len(sid))
    dom = ctypes.create_unicode_buffer(256)
    cch = wintypes.DWORD(len(dom))
    use = wintypes.DWORD()
    if not adv.LookupAccountNameW(None, name, sid, ctypes.byref(cb), dom, ctypes.byref(cch), ctypes.byref(use)):
        raise OSError(None, f"account '{name}' does not resolve (Windows error {ctypes.get_last_error()})")
    text = wintypes.LPWSTR()
    if not adv.ConvertSidToStringSidW(sid, ctypes.byref(text)):
        raise OSError(None, f"account '{name}': SID not convertible")
    try:
        return text.value.upper()
    finally:
        k32.LocalFree(ctypes.cast(text, ctypes.c_void_p))


# -- result, state and report ----------------------------------------------------------------------------------------


def result(mode, account, checks, warnings, now=None):
    return {
        "checks": [c.as_json() for c in checks],
        "warnings": list(warnings),
        "breach": any(not c.ok and c.widens for c in checks),
        "legacy": legacy_folders(checks),
        "mode": mode,
        "account": account,
        "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now)),
    }


def breach_folders(checks):
    seen = []
    for c in checks:
        if not c.ok and c.widens and c.path not in seen:
            seen.append(c.path)
    return seen


def legacy_folders(checks):
    return [c.path for c in checks if not c.ok and c.legacy]


def write_atomic(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def write_state(ws, doc, checks, may_clear):
    """The result to `check_result`; on a breach the flag to `breach_flag`. Only an elevated full run (may_clear)
    removes the flag."""
    write_atomic(ws.where("check_result"), json.dumps(doc, indent=2) + "\n")
    flag = ws.where("breach_flag")
    if doc["breach"]:
        body = {
            "time": doc["time"],
            "mode": doc["mode"],
            "account": doc["account"],
            "folders": breach_folders(checks),
            "checks": [{"what": c.what, "path": c.path} for c in checks if not c.ok and c.widens],
        }
        write_atomic(flag, json.dumps(body, indent=2) + "\n")
    elif may_clear and flag.exists():
        flag.unlink()


def text_report(doc):
    lines = [f"zones check ({doc['mode']}) as {doc['account']} at {doc['time']}"]
    for c in doc["checks"]:
        tag = "ok  " if c["ok"] else "FAIL"
        lines.append(f"{tag}  {c['what']:<18} {c['path']:<50} want {c['want']:<10} got {c['got']}")
    for w in doc["warnings"]:
        lines.append(f"warning: {w}")
    bad = sum(not c["ok"] for c in doc["checks"])
    n = len(doc["checks"])
    if bad:
        lines.append(
            f"FAIL: {bad} of {n} checks failed"
            + (" (breach: access wider than granted)" if doc["breach"] else "")
            + (" (legacy copy: older Coney files outside the sweep's quarantine)" if doc.get("legacy") else "")
        )
    else:
        lines.append(f"PASS: {n} checks" + (f", {len(doc['warnings'])} warnings" if doc["warnings"] else ""))
    return "\n".join(lines) + "\n"


def run(ws, mode, now=None):
    """(exit code, result document, checks) for the mode. Raises PermissionError when full mode is not elevated."""
    account = current_account()
    if mode == "full":
        if os.name != "nt":
            raise PermissionError("full mode reads every zone's Windows permissions: run it elevated on Windows")
        if not is_elevated():
            raise PermissionError("full mode reads every zone's permissions: run it elevated or as SYSTEM")
        checks, warnings = full_run(ws, read_sd, sid_of)
        try:
            programs = program_specs(ws, sid_of, wfp.app_id)
            walled = walled_programs(programs, read_wall()[1]) if programs else {}
        except OSError as e:
            programs, walled = [], {}
            checks.append(
                Check("port wall", "Claude Code programs", "app ids and filters readable", f"error: {e}", False)
            )
        checks += wall_filter_checks(ws, sid_of, read_wall, read_other_sublayers, overriding_filters(ws), programs)
        checks += listener_checks(ws)
        checks += unlisted_listener_checks(ws, sid_of, walled=walled)
        checks += settings_env_checks(ws)
        more, notes = legacy_checks(ws)
        checks += more
        warnings += notes
    else:
        checks, warnings = public_run(ws, account)
    doc = result(mode, account, checks, warnings, now)
    return (0 if all(c.ok for c in checks) else 1), doc, checks
