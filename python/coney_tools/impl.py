"""The implementer containers (playbook 7; clean-room page, section 4): build, start, check, run and collect.

Docker Engine runs in a WSL2 distro of its own (the workspace's `containers.distro`) with no Windows drive inside and
no Windows program reachable from it, so every Docker command goes through `wsl.exe -d <distro> -u root --exec ...`
and every file crosses as a byte stream on standard input or output: the build context and task files in, bundles,
artifacts and transcripts out. Nothing under C:\\ is ever mounted.

The pool is defined in the checkout's `containers/compose.yml`, which coney-tools passes to `docker compose -f -`.
Before starting anything, `up` renders it (`docker compose config`) and checks every service against the hardening
rules below; `check` proves the walls on the running containers from inside.

Each implementer is two containers: `impl-N-net` holds the network namespace, sets the egress wall and alone has
NET_ADMIN and NET_RAW; `impl-N` joins that namespace once the wall is checked and runs as the user impl with no
capability at all.
"""

import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from coney_tools import implog, signing, workspace
from coney_tools.cleanroom import scan

COMPOSE = "compose.yml"
NET = "-net"  # impl-N-net: the network sidecar of impl-N
UID_LABEL = "org.coney.impl-uid"  # set by the Dockerfile from its IMPL_UID
NET_CAPS = frozenset({"NET_ADMIN", "NET_RAW"})  # all the firewall needs, and only the sidecar has them
DENIED_PROBE = "example.com"  # a host off the allowlist: reaching it means the wall is down
ALLOWED_FILE = "/etc/coney/allowed-domains"  # in the image; the firewall and the probe both read it
KEY = "/home/impl/.ssh/id_ed25519"  # the container's commit signing key
GIT_KEYS = {"gpg.format": "ssh", "user.signingkey": KEY, "commit.gpgsign": "true"}  # /etc/gitconfig in the image
MANAGED = "/etc/claude-code/managed-settings.json"  # containers/managed-settings.json in the image
BUNDLE = "/tmp/coney-main.bundle"  # main of the collecting repo, streamed in before a session
SESSION = "coney-session"  # containers/session.sh in the image
TASK_FILE = "task.md"
MAX_TASK = 1 << 20
AUTH_KEYS = ("CLAUDE_CODE_OAUTH_TOKEN",)  # the subscription token only: implementers never bill to an API key
BRANCH = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}(/[A-Za-z0-9][A-Za-z0-9._-]{0,63}){0,3}$")
COLLECTING = ("outbox", "coney.git")  # under the workspace's containers folder
TIMEOUT = 600  # seconds for one ordinary command in the distro
# WSL stops a distro, Docker and its containers with it, soon after its last wsl.exe client ends. While containers
# run, a detached `wsl.exe ... sleep` holds it open; its Linux side carries this name so it can be found and ended.
KEEPALIVE = "coney-keepalive"
KEEPALIVE_MATCH = f"^{KEEPALIVE} infinity$"
SECURITY_OFF = ("seccomp", "apparmor", "label", "systempaths")  # security_opt that may only keep their default
HOST_NAMESPACES = ("pid", "ipc", "uts", "userns_mode", "cgroup")

# The only mounts an implementer container has: target -> (type, read-only). The sources come from compose.yml;
# a bind source must lie under the workspace's wsl_root.
MOUNTS = {
    "/work/coney": ("volume", False),  # its own clone
    "/home/impl/.ssh": ("volume", False),  # its signing key, on a volume no other container mounts
    "/work/task": ("bind", True),  # the task file
    "/work/artifacts": ("bind", False),
    "/home/impl/.claude": ("bind", False),  # Claude Code's transcripts
    "/outbox": ("bind", False),  # its own bare repo: work leaves only as signed commits pushed here
}
TASK_DIR, OUTBOX, ARTIFACTS, ARCHIVE = "/work/task", "/outbox", "/work/artifacts", "/home/impl/.claude"
COPIED_OUT = {ARTIFACTS: "artifacts", ARCHIVE: "archive"}  # mount target -> folder under the public containers path

AUTH_HELP = (
    "make it as one line CLAUDE_CODE_OAUTH_TOKEN=<token>, the token `claude setup-token` prints (an API key is "
    "refused). It stays on this machine: never in the repository"
)

# Runs in the distro as root: the folders one implementer's mounts need. The task folder belongs to root (read-only
# inside); the others to impl; the outbox starts as an empty bare repo made by impl.
PREPARE = r"""set -eu
uid=$1 task=$2 outbox=$3
shift 3
mkdir -p "$task"
chown root:root "$task"
chmod 0755 "$task"
for d in "$@"; do
    mkdir -p "$d"
    chown "$uid:$uid" "$d"
    chmod 0750 "$d"
done
if [ ! -f "$outbox/HEAD" ]; then
    mkdir -p "$outbox"
    chown "$uid:$uid" "$outbox"
    chmod 0750 "$outbox"
    env -i PATH=/usr/bin:/bin HOME=/nonexistent setpriv --reuid="$uid" --regid="$uid" --clear-groups \
        git init -q --bare --initial-branch=main "$outbox"
fi
"""

# Runs in the distro as root: writes standard input to a file atomically, with a mode and owner.
PUT = r"""set -eu
mkdir -p "$(dirname "$1")"
umask 077
cat >"$1.part"
chmod "$2" "$1.part"
chown "$3" "$1.part"
mv -f "$1.part" "$1"
"""

# Runs in the distro as root: reads the auth lines (KEY=value) from standard input into the environment, so the
# secret is never in a file in the distro nor on a command line, then starts the session.
WITH_AUTH = r"""while IFS= read -r line && [ -n "$line" ]; do
    export "$line"
done
exec "$@"
"""

# Runs in the container as impl: makes the signing key on the first start; prints whether it is new, then the key.
KEYGEN = r"""set -eu
if [ -s "$1" ]; then
    echo old
else
    ssh-keygen -q -t ed25519 -N "" -C "$CONEY_IMPL" -f "$1" >/dev/null
    echo new
fi
cat "$1.pub"
"""


class ImplError(Exception):
    """The command could not run, or refused to (exit 2)."""


# --- settings ------------------------------------------------------------------------------------------------------


@dataclass
class Settings:
    distro: str
    root: str  # wsl_root, a POSIX path inside the distro
    pool: list
    cpus: float
    memory: int  # bytes
    memory_text: str
    auth_file: Path
    model: str  # every session's model and effort (containers.model, containers.effort)
    effort: str
    public: Path  # the workspace's containers folder, in the public zone
    repo: Path  # the public repository, whose main the sessions start from

    @property
    def collecting(self):
        return self.public.joinpath(*COLLECTING)

    def env(self, uid):
        """The variables compose.yml takes from the workspace."""
        cpus = int(self.cpus) if float(self.cpus).is_integer() else self.cpus
        return {"CONEY_ROOT": self.root, "CONEY_CPUS": str(cpus), "CONEY_MEMORY": self.memory_text, "CONEY_UID": uid}


def settings(ws):
    c = ws.containers
    return Settings(
        distro=c["distro"],
        root=c["wsl_root"].rstrip("/"),
        pool=list(c["pool"]),
        cpus=c["cpus"],
        memory=workspace.size_bytes(c["memory"]),
        memory_text=str(c["memory"]),
        auth_file=ws.resolve(c["auth_file"], "containers.auth_file"),
        model=c["model"],
        effort=c["effort"],
        public=ws.where("containers"),
        repo=ws.where("repo"),
    )


def containers_dir():
    """The checkout's containers folder (compose.yml, the Dockerfile and what it copies)."""
    d = Path(__file__).resolve().parents[2] / "containers"
    if not (d / COMPOSE).is_file():
        raise ImplError(f"no containers/{COMPOSE} beside coney-tools: run it from a checkout of the repository")
    return d


def names_arg(names, s):
    """The containers a command acts on: the ones named, else the whole pool."""
    for n in names:
        if n not in s.pool:
            raise ImplError(f"{n} is not in the pool ({', '.join(s.pool)})")
    return list(dict.fromkeys(names)) or list(s.pool)


# --- the auth file -------------------------------------------------------------------------------------------------


def read_auth(path):
    """The auth file's KEY=value pairs, checked. Messages name line numbers and keys, never a value."""
    try:
        text = Path(path).read_text(encoding="utf-8-sig")
    except FileNotFoundError:
        raise ImplError(f"no Claude Code auth file at {path}: {AUTH_HELP}") from None
    except (OSError, UnicodeDecodeError) as e:
        raise ImplError(f"cannot read the auth file {path}: {e.__class__.__name__}") from None
    pairs = {}
    for i, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        key = key.strip()
        if not sep or key not in AUTH_KEYS:
            raise ImplError(f"the auth file {path}, line {i}: not KEY=value with KEY one of {', '.join(AUTH_KEYS)}")
        if not value or any(ch in value for ch in "\0\r\n"):
            raise ImplError(f"the auth file {path}, line {i}: {key} has no value or a control character")
        if key in pairs:
            raise ImplError(f"the auth file {path}, line {i}: {key} given twice")
        pairs[key] = value
    if not pairs:
        raise ImplError(f"the auth file {path} holds no key: {AUTH_HELP}")
    return pairs


# --- the distro ----------------------------------------------------------------------------------------------------


def size_text(n):
    for unit, mult in (("g", 1 << 30), ("m", 1 << 20), ("k", 1 << 10)):
        if n and n % mult == 0:
            return f"{n // mult}{unit}"
    return f"{n}b"


def label(cmd):
    """A command's first words for a message: an inline script or the environment before it is left out."""
    words = [w for w in cmd if "=" not in w and w not in ("env", "-i")]
    return " ".join(w if "\n" not in w else "(script)" for w in words[:3])


class Distro:
    """Commands in the WSL2 distro, as root, without a shell between the arguments and the program."""

    def __init__(self, name, wsl=None):
        self.name = name
        if os.name != "nt":
            raise ImplError("the containers run in a WSL2 distro driven through wsl.exe: this needs Windows")
        self.wsl = wsl or scan.find_program("wsl")
        if not self.wsl:
            raise ImplError("wsl.exe not found on PATH")

    def argv(self, cmd):
        return [self.wsl, "-d", self.name, "-u", "root", "--exec", *cmd]

    def run(self, cmd, data=None, ok=(0,), timeout=TIMEOUT):
        """Runs and captures; ImplError naming the command's first words when its exit status is not in ok."""
        try:
            r = subprocess.run(
                self.argv(cmd),
                input=data,
                capture_output=True,
                timeout=timeout,
                check=False,
                creationflags=scan.NO_WINDOW,
            )
        except subprocess.TimeoutExpired:
            raise ImplError(f"{label(cmd)} took over {timeout} s and was stopped") from None
        except OSError as e:
            raise ImplError(f"cannot run wsl.exe: {e.__class__.__name__}") from None
        if r.returncode not in ok:
            detail = r.stderr.decode("utf-8", "replace").strip().splitlines()
            raise ImplError(f"{label(cmd)} failed (exit {r.returncode})" + (f": {detail[-1]}" if detail else ""))
        return r

    def stream(self, cmd, data=None):
        """Runs with its output going straight to this console; the exit status. data goes to its standard input."""
        sys.stdout.flush()
        try:
            p = subprocess.Popen(self.argv(cmd), stdin=subprocess.PIPE if data is not None else subprocess.DEVNULL)
        except OSError as e:
            raise ImplError(f"cannot run wsl.exe: {e.__class__.__name__}") from None
        if data is not None:
            try:
                p.stdin.write(data)
                p.stdin.close()
            except OSError:
                pass
        return p.wait()

    def lines(self, cmd, data=None, each=None):
        """Runs with each line of its standard output handed to `each` as it comes (its standard error goes to this
        console); the exit status. data goes to its standard input."""
        sys.stdout.flush()
        try:
            p = subprocess.Popen(
                self.argv(cmd),
                stdin=subprocess.PIPE if data is not None else subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                creationflags=scan.NO_WINDOW,
            )
        except OSError as e:
            raise ImplError(f"cannot run wsl.exe: {e.__class__.__name__}") from None
        if data is not None:
            try:
                p.stdin.write(data)
                p.stdin.close()
            except OSError:
                pass
        for raw in p.stdout:
            each(raw.rstrip(b"\r\n"))
        return p.wait()

    def reader(self, cmd):
        """A running command whose standard output is read as a stream."""
        try:
            return subprocess.Popen(
                self.argv(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=scan.NO_WINDOW
            )
        except OSError as e:
            raise ImplError(f"cannot run wsl.exe: {e.__class__.__name__}") from None

    def detach(self, cmd):
        """Starts a command that outlives coney-tools: no console, its own process group, out of this job when
        Windows allows it."""
        flags = scan.NO_WINDOW | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        breakaway = getattr(subprocess, "CREATE_BREAKAWAY_FROM_JOB", 0)
        quiet = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
        for extra in (breakaway, 0):
            try:
                subprocess.Popen(self.argv(cmd), creationflags=flags | extra, close_fds=True, **quiet)
                return
            except OSError:
                continue  # a job that refuses breakaway: start it inside the job
        raise ImplError("cannot start wsl.exe in the background")


class Pool:
    """One command's view of the pool: the settings, the distro, compose.yml and the image."""

    def __init__(self, ws, distro=None, compose=None):
        self.s = settings(ws)
        self.d = distro or Distro(self.s.distro)
        self.compose_text = compose if compose is not None else (containers_dir() / COMPOSE).read_bytes()
        self._cfg = None
        self._uid = None

    # docker and compose

    def docker(self, *args, **kw):
        return self.d.run(["docker", *args], **kw)

    def compose_argv(self, *args, uid=None):
        env = [f"{k}={v}" for k, v in self.s.env(uid or self.uid(placeholder=True)).items()]
        return ["env", *env, "docker", "compose", "--project-directory", self.s.root, "-f", "-", *args]

    def compose(self, *args, ok=(0,), stream=False):
        self.d.run(["mkdir", "-p", self.s.root])
        argv = self.compose_argv(*args)
        if stream:
            return self.d.stream(argv, self.compose_text)
        return self.d.run(argv, self.compose_text, ok)

    def config(self):
        if self._cfg is None:
            r = self.compose("config", "--format", "json")
            try:
                self._cfg = json.loads(r.stdout)
            except ValueError:
                raise ImplError("docker compose config gave no JSON") from None
        return self._cfg

    def image(self):
        images = {svc.get("image") for svc in self.config().get("services", {}).values()}
        if len(images) != 1 or not all(images):
            raise ImplError(f"the services in {COMPOSE} do not all name one image")
        return images.pop()

    def uid(self, placeholder=False):
        """The implementer's user id, from the image's label. With placeholder, a stand-in when there is no image
        yet (compose.yml needs a value to render; only up, which needs the real one, refuses)."""
        if self._uid is None:
            r = self.d.run(
                [
                    "docker",
                    "image",
                    "inspect",
                    "--format",
                    f'{{{{index .Config.Labels "{UID_LABEL}"}}}}',
                    self.image_name(),
                ],
                ok=(0, 1),
            )
            text = r.stdout.decode().strip()
            if r.returncode or not text.isdigit():
                if placeholder:
                    return "65534"
                raise ImplError("no implementer image (or it has no user id label): run `coney-tools impl build`")
            self._uid = text
        return self._uid

    def image_name(self):
        """The image name, read from compose.yml's text (rendering it needs the user id, which comes from the
        image)."""
        names = set(re.findall(rb"(?m)^\s*image:\s*['\"]?([^\s'\"#]+)", self.compose_text))
        if len(names) != 1:
            raise ImplError(f"{COMPOSE} must name exactly one image")
        return names.pop().decode()

    def inspect(self, name):
        """docker inspect of one container, or None when there is none."""
        r = self.docker("inspect", "--type", "container", name, ok=(0, 1))
        if r.returncode:
            return None
        try:
            return json.loads(r.stdout)[0]
        except (ValueError, IndexError):
            raise ImplError(f"docker inspect {name} gave no JSON") from None

    def running(self, name):
        info = self.inspect(name)
        return bool(info and info["State"].get("Running"))

    def exec(self, name, *cmd, data=None, ok=(0,), user="impl", timeout=TIMEOUT):
        flags = ["-i"] if data is not None else []
        return self.docker("exec", *flags, "-u", user, name, *cmd, data=data, ok=ok, timeout=timeout)

    def as_impl(self, *cmd):
        """A command run in the distro as the implementer's user id, with no configuration of root's."""
        uid = self.uid()
        return [
            "env",
            "-i",
            "PATH=/usr/bin:/bin",
            "HOME=/nonexistent",
            "GIT_CONFIG_NOSYSTEM=1",
            "setpriv",
            f"--reuid={uid}",
            f"--regid={uid}",
            "--clear-groups",
            *cmd,
        ]

    def put(self, path, data, mode="0444", owner="root:root"):
        self.d.run(["sh", "-c", PUT, "sh", path, mode, owner], data=data)

    # mounts

    def binds(self, name):
        """Mount target -> source in the distro, for one implementer's bind mounts."""
        svc = self.config()["services"].get(name, {})
        return {v["target"]: v["source"] for v in svc.get("volumes", []) if v.get("type") == "bind"}

    def volumes(self, name):
        """Docker's names of one implementer's named volumes."""
        cfg = self.config()
        svc = cfg["services"].get(name, {})
        named = cfg.get("volumes", {})
        return [
            named.get(v["source"], {}).get("name", v["source"])
            for v in svc.get("volumes", [])
            if v.get("type") == "volume"
        ]

    def state_file(self, name):
        return f"{self.s.root}/state/{name}.json"

    def outbox_refs(self, name):
        """The outbox's branches: {ref: commit}."""
        path = self.binds(name).get(OUTBOX)
        r = self.d.run(["test", "-f", f"{path}/HEAD"], ok=(0, 1))
        if r.returncode:
            return {}
        r = self.d.run(
            self.as_impl("git", "-C", path, "for-each-ref", "--format=%(objectname) %(refname)", "refs/heads")
        )
        return dict(reversed(line.split(" ", 1)) for line in r.stdout.decode().splitlines() if " " in line)


# --- hardening of the compose definition ---------------------------------------------------------------------------


def _list(v):
    if v is None:
        return []
    return list(v) if isinstance(v, (list, tuple)) else [v]


def _tmpfs_targets(svc):
    return {str(t).split(":", 1)[0] for t in _list(svc.get("tmpfs"))}


def _common(svc):
    """What every service of the pool must hold to."""
    out = []
    if svc.get("privileged"):
        out.append("privileged")
    if "ALL" not in {str(c).upper() for c in _list(svc.get("cap_drop"))}:
        out.append("does not drop every capability (cap_drop: [ALL])")
    opts = [str(o).replace("=", ":", 1).lower() for o in _list(svc.get("security_opt"))]
    if not any(o in ("no-new-privileges", "no-new-privileges:true") for o in opts) or any(
        o.startswith("no-new-privileges:") and o != "no-new-privileges:true" for o in opts
    ):
        out.append("no-new-privileges is not set")
    for o in opts:
        if o.split(":", 1)[0] in SECURITY_OFF:
            out.append(f"security_opt {o.split(':', 1)[0]} is changed (only the default may apply)")
    if svc.get("read_only") is not True:
        out.append("the root filesystem is not read-only")
    if "/tmp" not in _tmpfs_targets(svc):
        out.append("no tmpfs on /tmp")
    for k in HOST_NAMESPACES:
        if str(svc.get(k, "")).lower() == "host":
            out.append(f"shares the host's {k}")
    if str(svc.get("network_mode", "")).lower() == "host":
        out.append("uses the host's network")
    for k in ("devices", "device_cgroup_rules", "volumes_from", "ports", "sysctls", "group_add"):
        if svc.get(k):
            out.append(f"sets {k}")
    for v in _list(svc.get("volumes")):
        if "docker.sock" in json.dumps(v):
            out.append("mounts the Docker socket")
    if not svc.get("cpus") or not svc.get("mem_limit"):
        out.append("has no CPU or memory cap")
    return out


def _worker(svc, name, s):
    out = []
    user = str(svc.get("user", "")).split(":", 1)[0]
    if user in ("", "root", "0"):
        out.append("runs as root (user: impl)")
    if _list(svc.get("cap_add")):
        out.append(f"adds capabilities ({', '.join(map(str, _list(svc.get('cap_add'))))}): it must have none")
    if svc.get("network_mode") != f"service:{name}{NET}":
        out.append(f"does not join its firewall's network namespace (network_mode: service:{name}{NET})")
    dep = (svc.get("depends_on") or {}).get(name + NET) or {}
    if dep.get("condition") != "service_healthy":
        out.append(f"does not wait for {name}{NET} to be healthy (its wall checked)")
    mounts = {}
    for v in _list(svc.get("volumes")):
        if not isinstance(v, dict) or "target" not in v:
            out.append("a volume in short form (compose config gives the long form)")
            continue
        if v["target"] in mounts:
            out.append(f"mounts {v['target']} twice")
        mounts[v["target"]] = v
    for t in sorted(set(mounts) - set(MOUNTS)):
        out.append(f"mounts {t}, which is not one of its mounts")
    for t in sorted(set(MOUNTS) - set(mounts)):
        out.append(f"does not mount {t}")
    root = PurePosixPath(s.root)
    for t, v in sorted(mounts.items()):
        if t not in MOUNTS:
            continue
        kind, ro = MOUNTS[t]
        if v.get("type") != kind:
            out.append(f"{t} is a {v.get('type')} mount, not a {kind}")
        if ro and not v.get("read_only"):
            out.append(f"{t} is not read-only")
        src = str(v.get("source", ""))
        if kind == "bind":
            p = PurePosixPath(src)
            if not p.is_absolute() or ".." in p.parts or root not in p.parents:
                out.append(f"{t} comes from {src}, outside {s.root}")
        elif not src or "/" in src:
            out.append(f"{t} is not a named volume")
    try:
        cpus = float(svc.get("cpus", 0))
    except (TypeError, ValueError):
        cpus = 0.0
    if cpus != float(s.cpus):
        out.append(f"cpus {svc.get('cpus')}, the workspace says {s.cpus}")
    try:
        mem = workspace.size_bytes(svc.get("mem_limit"))
    except workspace.WorkspaceError:
        mem = None
    if mem != s.memory:
        out.append(f"mem_limit {svc.get('mem_limit')}, the workspace says {s.memory_text}")
    return out


def _net(svc):
    out = []
    extra = {str(c).upper() for c in _list(svc.get("cap_add"))} - NET_CAPS
    if extra:
        out.append(f"adds capabilities beyond NET_ADMIN and NET_RAW: {', '.join(sorted(extra))}")
    if _list(svc.get("volumes")):
        out.append("mounts volumes (the firewall needs none)")
    mode = str(svc.get("network_mode", ""))
    if mode.startswith(("service:", "container:")):
        out.append("shares another container's network namespace")
    hc = svc.get("healthcheck") or {}
    if hc.get("disable") or not hc.get("test") or _list(hc.get("test"))[:1] == ["NONE"]:
        out.append("has no health check (its implementer would start before the wall is checked)")
    if len(svc.get("networks") or {}) != 1:
        out.append("is not on exactly one network")
    return out


def hardening_problems(cfg, s):
    """Every way the rendered compose definition breaks the hardening rules; empty when it holds."""
    out = []
    services = cfg.get("services") or {}
    want = set(s.pool) | {n + NET for n in s.pool}
    out += [f"{n}: a service outside the pool" for n in sorted(set(services) - want)]
    out += [f"{n}: missing" for n in sorted(want - set(services))]
    seen = {}
    for name in s.pool:
        net = services.get(name + NET)
        if net is not None:
            out += [f"{name}{NET}: {p}" for p in _common(net) + _net(net)]
            for n in net.get("networks") or {}:
                if n in seen:
                    out.append(f"{name}{NET}: shares the network {n} with {seen[n]}")
                seen[n] = name + NET
        if name in services:
            out += [f"{name}: {p}" for p in _common(services[name]) + _worker(services[name], name, s)]
    for n, net in (cfg.get("networks") or {}).items():
        if (net or {}).get("external"):
            out.append(f"network {n} is external")
        if (net or {}).get("driver") not in (None, "bridge"):
            out.append(f"network {n} does not use the bridge driver")
    return out


def inspect_problems(worker, net):
    """The same rules as Docker applied them to the running pair (docker inspect)."""
    out = []
    for label, info in (("implementer", worker), ("firewall", net)):
        hc = info["HostConfig"]
        if hc.get("Privileged"):
            out.append(f"{label}: privileged")
        if "ALL" not in {c.upper() for c in hc.get("CapDrop") or []}:
            out.append(f"{label}: does not drop every capability")
        if not any(
            o.replace("=", ":") in ("no-new-privileges", "no-new-privileges:true") for o in hc.get("SecurityOpt") or []
        ):
            out.append(f"{label}: no-new-privileges is not set")
        if not hc.get("ReadonlyRootfs"):
            out.append(f"{label}: the root filesystem is not read-only")
        for m in info.get("Mounts") or []:
            if "docker.sock" in str(m.get("Source", "")) + str(m.get("Destination", "")):
                out.append(f"{label}: mounts the Docker socket")
    if worker["HostConfig"].get("CapAdd"):
        out.append("implementer: adds capabilities")
    extra = {c.upper().removeprefix("CAP_") for c in net["HostConfig"].get("CapAdd") or []} - NET_CAPS
    if extra:
        out.append(f"firewall: adds {', '.join(sorted(extra))}")
    if worker["HostConfig"].get("NetworkMode") != f"container:{net['Id']}":
        out.append("implementer: not in its firewall's network namespace")
    dests = {m.get("Destination") for m in worker.get("Mounts") or []}
    if dests != set(MOUNTS):
        out.append(f"implementer: mounts {', '.join(sorted(dests ^ set(MOUNTS)))} differ from its mounts")
    return out


# --- build ---------------------------------------------------------------------------------------------------------


def _files(folder):
    """(path under folder, file) for every plain file of the folder; symbolic links never cross."""
    folder = Path(folder)
    return [(p.relative_to(folder).as_posix(), p) for p in folder.rglob("*") if p.is_file() and not p.is_symlink()]


def context_tar(folder, extra=()):
    """The build context as a tar: every file of the folder, plus each (name, file or folder) of extra under that
    name, in name order, with LF line ends (every file there is text read in Linux, whatever the checkout's line
    ends), fixed owners, modes and times: the same files give the same bytes."""
    entries = _files(folder)
    for name, src in extra:
        src = Path(src)
        entries += [(f"{name}/{rel}", p) for rel, p in _files(src)] if src.is_dir() else [(name, src)]
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as t:
        for name, p in sorted(entries):
            info = tarfile.TarInfo(name)
            data = signing.lf(p.read_bytes())
            info.size, info.mode, info.mtime = len(data), 0o644, 0
            t.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def tether_context(ws):
    """Tether for the image (coney-tools tether): the installed build and the containers' locked config. Every
    container carries it, so a build without it is refused."""
    mod, cfg = ws.where("tether_mod"), ws.where("tether_configs") / "container.json"
    if not (mod / ".claude-plugin" / "plugin.json").is_file() or not cfg.is_file():
        raise ImplError(f"Tether is not installed ({mod}, {cfg}): run the installer; every container carries it")
    return [("tether", mod), ("tether.json", cfg)]


def build(ws, distro=None):
    pool = Pool(ws, distro)
    folder = containers_dir()
    extra = tether_context(ws)
    ctx = f"{pool.s.root}/build"
    pool.d.run(["rm", "-rf", ctx])
    pool.d.run(["mkdir", "-p", ctx])
    pool.d.run(["tar", "-x", "--no-same-owner", "-C", ctx], data=context_tar(folder, extra))
    image = pool.image_name()
    code = pool.d.stream(["docker", "build", "--progress=plain", "-t", image, ctx])
    if code:
        return 1, [f"the image {image} did not build (exit {code})"]
    return 0, [f"built {image} in the distro {pool.s.distro}"]


# --- up, down, status, reset ---------------------------------------------------------------------------------------


def prepare(pool, name):
    """The distro folders one implementer's bind mounts need."""
    b = pool.binds(name)
    others = [b[t] for t in (ARTIFACTS, ARCHIVE)]
    pool.d.run(["sh", "-c", PREPARE, "sh", pool.uid(), b[TASK_DIR], b[OUTBOX], *others])


def signing_key(pool, name):
    """Makes the container's key on its first start. (new, the allowed_signers line)."""
    r = pool.exec(name, "sh", "-c", KEYGEN, "sh", KEY)
    lines = r.stdout.decode().split("\n")
    fresh = lines[0].strip() == "new"
    parts = lines[1].split() if len(lines) > 1 else []
    if len(parts) < 2 or parts[0] != "ssh-ed25519":
        raise ImplError(f"{name}: the signing key's public half is not an Ed25519 key")
    return fresh, f'{name} namespaces="git" {parts[0]} {parts[1]}'


def git_signing_problems(pool, name):
    out = []
    for k, want in GIT_KEYS.items():
        r = pool.exec(name, "git", "config", "--system", "--get", k, ok=(0, 1))
        got = r.stdout.decode().strip()
        if got != want:
            out.append(f"{name}: git {k} is {got or 'unset'}, not {want}")
    return out


def settings_problems(pool, name, want):
    """The container's managed Claude Code settings must hold the workspace's claude_settings."""
    if not want:
        return []
    r = pool.exec(name, "cat", MANAGED, ok=(0, 1))
    if r.returncode:
        return [f"{name}: {MANAGED} is missing"]
    return [f"{name}: {p}" for p in workspace.settings_problems(r.stdout.decode("utf-8", "replace"), want)]


def up(ws, names=(), distro=None):
    pool = Pool(ws, distro)
    names = names_arg(names, pool.s)
    pool.uid()  # refuses without the image
    problems = hardening_problems(pool.config(), pool.s)
    if problems:
        return 1, [f"REFUSED {p}" for p in problems] + [f"{len(problems)} hardening problems: nothing started"]
    lines = ["hardening of every service in compose.yml: PASS"]
    keep_alive(pool)
    # The ipset match the firewall uses needs these kernel modules; a container cannot load them.
    pool.d.run(["modprobe", "-a", "ip_set", "ip_set_hash_net", "xt_set"], ok=(0, 1))
    for n in names:
        prepare(pool, n)
    code = pool.compose("up", "--detach", "--no-build", "--wait", "--wait-timeout", "180", *names, stream=True)
    if code:
        for n in names:
            r = pool.docker("logs", "--tail", "15", n + NET, ok=(0, 1))
            tail = (r.stdout + r.stderr).decode("utf-8", "replace").strip()
            lines += [f"{n}{NET} log: {x}" for x in tail.splitlines() if x.strip()]
        return 1, [*lines, f"docker compose up failed (exit {code}): see above"]
    failed = False
    for n in names:
        fresh, line = signing_key(pool, n)
        problems = git_signing_problems(pool, n)
        failed |= bool(problems)
        lines += problems
        if fresh:
            lines += [f"{n}: up, with a new signing key. Add this line to allowed_signers (a maintainer):", f"  {line}"]
        else:
            lines.append(f"{n}: up; its signing key: {line}")
    return (1 if failed else 0), lines


def held_open(pool):
    return pool.d.run(["pgrep", "-f", KEEPALIVE_MATCH], ok=(0, 1)).returncode == 0


def keep_alive(pool):
    """Holds the distro open (see KEEPALIVE) unless something does already."""
    if held_open(pool):
        return
    pool.d.detach(["bash", "-c", f"exec -a {KEEPALIVE} sleep infinity"])
    for _ in range(30):
        if held_open(pool):
            return
        time.sleep(0.5)
    raise ImplError("the distro's keep-alive did not start: WSL would stop Docker once coney-tools ends")


def release(pool):
    """Lets the distro stop once no container of the pool runs any more."""
    if any(pool.running(n) or pool.running(n + NET) for n in pool.s.pool):
        return []
    pool.d.run(["pkill", "-f", KEEPALIVE_MATCH], ok=(0, 1))
    return ["no container runs: the distro may stop"]


def down(ws, names=(), distro=None):
    pool = Pool(ws, distro)
    if names:
        names = names_arg(names, pool.s)
        pool.compose("rm", "--stop", "--force", *[x for n in names for x in (n, n + NET)])
        return 0, [f"{n}: stopped and removed (its volumes stay)" for n in names] + release(pool)
    pool.compose("down")
    return 0, ["the pool is down (volumes stay)", *release(pool)]


def fingerprint(pool, name):
    r = pool.exec(name, "ssh-keygen", "-l", "-f", KEY + ".pub", ok=(0, 1, 255))
    parts = r.stdout.decode().split()
    return parts[1] if r.returncode == 0 and len(parts) > 1 else "no key"


def read_state(pool, name):
    r = pool.d.run(["cat", pool.state_file(name)], ok=(0, 1))
    try:
        return json.loads(r.stdout) if r.returncode == 0 else {}
    except ValueError:
        return {}


def status(ws, distro=None):
    pool = Pool(ws, distro)
    lines = [f"distro {pool.s.distro}: " + ("held open" if held_open(pool) else "not held open (no container runs)")]
    for n in pool.s.pool:
        worker, net = pool.inspect(n), pool.inspect(n + NET)
        if worker is None:
            lines.append(f"{n}: no container")
            continue
        state = worker["State"]["Status"]
        health = ((net or {}).get("State", {}).get("Health") or {}).get("Status", "no firewall")
        key = fingerprint(pool, n) if worker["State"].get("Running") else "-"
        hc = worker["HostConfig"]
        caps = f"cpus {hc.get('NanoCpus', 0) / 1e9:g}, memory {size_text(hc.get('Memory', 0))}"
        st = read_state(pool, n)
        task = "none"
        if st:
            task = f"{st.get('task')} on {st.get('branch')}, started {st.get('started')}"
            if "ended" in st:
                task += f", ended {st['ended']} (exit {st.get('exit')})"
        lines.append(f"{n}: {state}, firewall {health}, key {key}, {caps}, task: {task}")
    return 0, lines


def uncollected(pool, name):
    """Outbox branches the collecting repo does not hold at the same commit."""
    refs = pool.outbox_refs(name)
    if not refs:
        return []
    git = signing.program("git")
    have = {}
    if (pool.s.collecting / "HEAD").is_file():
        r = signing.git(
            git, pool.s.collecting, "for-each-ref", "--format=%(objectname) %(refname)", f"refs/heads/{name}/"
        )
        for line in r.stdout.decode().splitlines():
            sha, ref = line.split(" ", 1)
            have["refs/heads/" + ref.removeprefix(f"refs/heads/{name}/")] = sha
    return sorted(ref for ref, sha in refs.items() if have.get(ref) != sha)


def reset(ws, name, force=False, distro=None):
    pool = Pool(ws, distro)
    names_arg([name], pool.s)
    left = uncollected(pool, name)
    if left and not force:
        return 1, [
            f"{name}: its outbox holds work not yet collected ({', '.join(left)}): run "
            + f"`coney-tools impl collect {name}` first, or reset with --force"
        ]
    old = pool.inspect(name)
    key = fingerprint(pool, name) if old and old["State"].get("Running") else None
    pool.compose("rm", "--stop", "--force", name, name + NET)
    vols = pool.volumes(name)
    if vols:
        pool.docker("volume", "rm", "--force", *vols)
    dirs = list(pool.binds(name).values())
    pool.d.run(["rm", "-rf", "--", *dirs, pool.state_file(name)])
    gone = f"its signing key ({key}) is gone" if key else "its signing key is gone"
    return 0, [
        f"{name}: container, volumes ({', '.join(vols)}) and its folders in the distro removed",
        f"{gone}: remove the {name} line from allowed_signers (a maintainer); the next `impl up` makes a new key",
        *release(pool),
    ]


# --- run -----------------------------------------------------------------------------------------------------------


def ensure_collecting(git, path):
    """The collecting repo must exist already. It is a zone sub-folder with its own permissions (the PR analyst may
    read it), which this account cannot set: one it created would fail the zones check as a breach."""
    if not (Path(path) / "HEAD").is_file():
        raise ImplError(
            f"no collecting repo at {path}: a maintainer creates it as a bare repo and applies its zone permissions "
            "(zones.json), then runs the zones check"
        )


def main_bundle(pool, git, tmp):
    """main of the public repository, through the collecting repo, as a bundle file."""
    ensure_collecting(git, pool.s.collecting)
    signing.git(
        git, pool.s.collecting, "fetch", "--quiet", "--no-tags", str(pool.s.repo), "+refs/heads/main:refs/heads/main"
    )
    out = Path(tmp) / "main.bundle"
    signing.git(git, pool.s.collecting, "bundle", "create", "--quiet", str(out), "refs/heads/main")
    return out.read_bytes()


def session_argv(name, branch, auth, model, effort):
    """The command that starts a session at the workspace's model and effort: the auth variables come from standard
    input, never the command line."""
    env = [x for k in sorted(auth) for x in ("-e", k)]
    return ["sh", "-c", WITH_AUTH, "sh", "docker", "exec", "-u", "impl", *env, name, SESSION, branch, model, effort]


def auth_input(auth):
    return "".join(f"{k}={v}\n" for k, v in sorted(auth.items())).encode() + b"\n"


def now():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def run(ws, name, task, branch, distro=None):
    pool = Pool(ws, distro)
    names_arg([name], pool.s)
    if not BRANCH.match(branch) or ".." in branch or branch.endswith((".lock", ".")):
        raise ImplError(f"not a branch name coney-tools takes: {branch!r} (letters, digits, . _ - and up to 3 /)")
    auth = read_auth(pool.s.auth_file)
    try:
        data = Path(task).read_bytes()
    except OSError as e:
        raise ImplError(f"cannot read the task file {task}: {e.__class__.__name__}") from None
    if not data or len(data) > MAX_TASK:
        raise ImplError(f"the task file {task} is empty or over {MAX_TASK} bytes")
    worker, net = pool.inspect(name), pool.inspect(name + NET)
    healthy = ((net or {}).get("State", {}).get("Health") or {}).get("Status") == "healthy"
    if not (worker and worker["State"].get("Running") and healthy):
        raise ImplError(f"{name} is not running behind a healthy firewall: start it with `coney-tools impl up {name}`")
    git = signing.program("git")
    with tempfile.TemporaryDirectory(prefix="coney-impl-") as tmp:
        bundle = main_bundle(pool, git, tmp)
    pool.exec(name, "sh", "-c", 'cat >"$1"', "sh", BUNDLE, data=bundle)
    pool.put(f"{pool.binds(name)[TASK_DIR]}/{TASK_FILE}", data)
    st = {"task": Path(task).name, "branch": branch, "model": pool.s.model, "effort": pool.s.effort, "started": now()}
    pool.put(pool.state_file(name), json.dumps(st).encode(), mode="0644")
    live = implog.LiveLog(pool.s.public, name, st)
    print(f"{name}: session on branch {branch} started; live log in {live.f.name}", flush=True)
    try:
        code = pool.d.lines(session_argv(name, branch, auth, pool.s.model, pool.s.effort), auth_input(auth), live.feed)
    finally:
        live.close(ended=now())
    st.update(ended=now(), exit=code, served=live.served())
    live.state.update(exit=code, served=live.served())
    live.write_state()
    pool.put(pool.state_file(name), json.dumps(st).encode(), mode="0644")
    pushed = pool.outbox_refs(name).get(f"refs/heads/{branch}")
    lines = [live.final.rstrip()] if live.final else []
    lines.append(f"{name}: the session ended (exit {code})")
    lines.append(
        f"{name}: {branch} pushed to its outbox at {pushed[:12]}" if pushed else f"{name}: {branch} not in its outbox"
    )
    if pushed:
        lines.append(f"collect it with `coney-tools impl collect {name}`")
    return (0 if code == 0 and pushed else 1), lines


def log(ws, name=None, tail=implog.TAIL, as_json=False):
    """(code, lines): the implementers' live states and the last lines of one's live log."""
    if name is not None and not re.match(r"^impl-\d+$", name):
        raise ImplError(f"not an implementer's name: {name!r}")
    doc = implog.read_log(ws.where("containers"), name, max(1, tail))
    if as_json:
        return 0, [json.dumps(doc, indent=2)]
    lines = []
    for s in doc["implementers"]:
        state = f"exit {s['exit']}" if "exit" in s else ("ended" if s.get("ended") else "running")
        lines.append(
            f"{s['name']}: {state}, {s.get('task', '?')} on {s.get('branch', '?')}, started {s.get('started')}"
        )
    if not doc["readable"]:
        lines.append("this account may not read the implementers' live logs")
    elif not doc["implementers"]:
        lines.append("no implementer has a live log yet")
    if doc["shown"]:
        lines += [f"== {doc['shown']}", *doc["lines"]]
    return 0, lines


# --- collect -------------------------------------------------------------------------------------------------------


def copy_out(pool, src, dest):
    """Copies a distro folder into a public folder as a tar stream: regular files and folders only, each kept
    inside dest (tarfile's data filter). (files copied, members skipped)."""
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    p = pool.d.reader(["tar", "-C", src, "-cf", "-", "."])
    copied = skipped = 0
    try:
        with tarfile.open(fileobj=p.stdout, mode="r|") as t:
            for m in t:
                if not (m.isfile() or m.isdir()):
                    skipped += 1
                    continue
                try:
                    t.extract(m, dest, filter="data")
                    copied += m.isfile()
                except (OSError, tarfile.TarError):
                    skipped += 1
    except tarfile.TarError as e:
        raise ImplError(f"the copy of {src} is not a tar stream: {e.__class__.__name__}") from None
    finally:
        p.stdout.close()
        err = p.stderr.read()
        code = p.wait()
    if code not in (0, 1):  # 1: a file changed while it was read
        raise ImplError(f"tar of {src} failed (exit {code}): {err.decode('utf-8', 'replace').strip()[-200:]}")
    return copied, skipped


def collect_outbox(pool, git, name):
    refs = pool.outbox_refs(name)
    if not refs:
        return f"{name}: outbox empty"
    path = pool.binds(name)[OUTBOX]
    r = pool.d.run(pool.as_impl("git", "-C", path, "bundle", "create", "-", "--branches"))
    ensure_collecting(git, pool.s.collecting)
    with tempfile.TemporaryDirectory(prefix="coney-collect-") as tmp:
        b = Path(tmp) / f"{name}.bundle"
        b.write_bytes(r.stdout)
        signing.git(
            git,
            pool.s.collecting,
            "-c",
            "fetch.fsckObjects=true",
            "fetch",
            "--quiet",
            "--no-tags",
            str(b),
            f"+refs/heads/*:refs/heads/{name}/*",
        )
    return f"{name}: {len(refs)} branches fetched into {pool.s.collecting} as {name}/<branch>"


def collect(ws, names=(), distro=None):
    pool = Pool(ws, distro)
    names = names_arg(names, pool.s)
    git = signing.program("git")
    lines = []
    for n in names:
        lines.append(collect_outbox(pool, git, n))
        b = pool.binds(n)
        for target, folder in COPIED_OUT.items():
            r = pool.d.run(["test", "-d", b[target]], ok=(0, 1))
            if r.returncode:
                lines.append(f"{n}: no {folder} yet")
                continue
            copied, skipped = copy_out(pool, b[target], pool.s.public / folder / n)
            extra = f", {skipped} skipped (not a plain file, or a name Windows refuses)" if skipped else ""
            lines.append(f"{n}: {copied} {folder} files copied to {pool.s.public / folder / n}{extra}")
            if target == ARCHIVE:
                made = implog.write_transcripts(pool.s.public / folder / n)
                lines.append(f"{n}: {made} readable transcripts in {pool.s.public / folder / n / implog.TRANSCRIPTS}")
    return 0, lines


# --- check ---------------------------------------------------------------------------------------------------------


def gateway(pool):
    """The distro's default gateway: the Windows host's address on the WSL network."""
    r = pool.d.run(["ip", "route", "show", "default"])
    m = re.search(r"\bvia\s+([0-9.]+)", r.stdout.decode())
    return m.group(1) if m else None


def probe(pool, name, args):
    src = (Path(__file__).with_name("impl_probe.py")).read_bytes()
    r = pool.exec(name, "python3", "-", json.dumps(args), data=src, timeout=120)
    try:
        return json.loads(r.stdout)
    except ValueError:
        raise ImplError(f"{name}: the probe gave no JSON") from None


def check(ws, names=(), distro=None):
    pool = Pool(ws, distro)
    names = names_arg(names, pool.s) if names else [n for n in pool.s.pool if pool.running(n)]
    if not names:
        raise ImplError("no implementer container is running: start one with `coney-tools impl up`")
    args = {
        "denied": DENIED_PROBE,
        "allowed_file": ALLOWED_FILE,
        "gateway": gateway(pool),
        "ports": [p["port"] for p in ws.ports],
    }
    lines, failed = [], False
    for n in names:
        worker, net = pool.inspect(n), pool.inspect(n + NET)
        if not (worker and worker["State"].get("Running") and net and net["State"].get("Running")):
            lines.append(f"FAIL {n}: not running")
            failed = True
            continue
        health = (net["State"].get("Health") or {}).get("Status")
        results = [
            {
                "what": "the firewall is healthy (its wall checked at start)",
                "ok": health == "healthy",
                "got": health or "no health check",
            }
        ]
        results += [{"what": p, "ok": False, "got": "docker inspect"} for p in inspect_problems(worker, net)]
        results += [{"what": p, "ok": False, "got": "git config"} for p in git_signing_problems(pool, n)]
        results += [{"what": p, "ok": False, "got": MANAGED} for p in settings_problems(pool, n, ws.claude_settings)]
        results += probe(pool, n, args)
        for c in results:
            verdict = "PASS" if c["ok"] else ("WARN" if c.get("warn") else "FAIL")
            lines.append(f"{verdict} {n}: {c['what']} ({c['got']})")
            failed |= verdict == "FAIL"
    return (1 if failed else 0), lines
