"""Tests for `coney-tools impl` and the workspace's containers table, on made-up workspaces, compose definitions and
docker output. Nothing here needs Docker or WSL: the distro is a fake that answers each command and records it.

The integration test at the end builds the real image, starts one container and runs `impl check`; it runs only
with CONEY_TEST_DOCKER=1 (and the workspace from CONEY_WORKSPACE or the default place)."""

import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import types
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import impl, workspace
from coney_tools.cli import main

SECRET = "sk-test-" + "0123456789abcdef" * 2  # a made-up token: it must never show
ROOT = "/srv/test-coney"
# A made-up session's stream-json output, as the container's Claude Code prints it.
SESSION_EVENTS = [
    b'{"type":"system","subtype":"init","model":"m","cwd":"/work/coney/.worktrees/task-one"}',
    (
        b'{"type":"assistant","message":{"model":"claude-opus-5-5","content":[{"type":"tool_use","name":"Bash",'
        b'"input":{"command":"ctest","description":"run the tests"}}]}}'
    ),
    b'{"type":"user","message":{"content":[{"type":"tool_result","content":"100% tests passed"}]}}',
    b"not json",
    b'{"type":"result","result":"All done.","num_turns":3,"duration_ms":12000}',
]
POOL = ["impl-1", "impl-2"]


def write_workspace(tmp, containers=None, drop=(), **extra):
    tmp = Path(tmp)
    (tmp / "pub" / "repo").mkdir(parents=True, exist_ok=True)
    (tmp / "pub" / "containers").mkdir(parents=True, exist_ok=True)
    (tmp / "zones.json").write_text(
        json.dumps({"roots": [{"path": str(tmp / "pub"), "role": "public"}], "accounts": ["t-public"]}),
        encoding="utf-8",
    )
    c = {
        "distro": "test-docker",
        "wsl_root": ROOT,
        "pool": list(POOL),
        "cpus": 3,
        "memory": "5g",
        "auth_file": "public:containers\\secrets\\auth.env",
        "model": "claude-opus-5-5",
        "effort": "medium",
    }
    if containers is not None:
        c = containers
    data = {
        "version": 1,
        "zones_file": "zones.json",
        "paths": {"repo": "public:repo", "containers": "public:containers"},
        "zones": {"public": {"account": "t-public", "folder": "public:repo"}},
        "analyst_ports": [{"name": "a", "port": 8090}, {"name": "b", "port": 28011}],
        "check": {"cache_minutes": 5, "schedule_minutes": 5},
        "containers": c,
    }
    data.update(extra)
    for k in drop:
        data.pop(k)
    p = tmp / "ws.json"
    p.write_text(json.dumps(data), encoding="utf-8")
    return p


def worker(name):
    return {
        "image": "coney-impl:local",
        "user": "impl",
        "init": True,
        "read_only": True,
        "cap_drop": ["ALL"],
        "security_opt": ["no-new-privileges:true"],
        "tmpfs": ["/tmp:size=2g", "/home/impl:size=512m,uid=1500,gid=1500"],
        "network_mode": f"service:{name}-net",
        "depends_on": {f"{name}-net": {"condition": "service_healthy", "required": True}},
        "cpus": 3,
        "mem_limit": str(5 << 30),
        "volumes": [
            {"type": "volume", "source": f"{name}-coney", "target": "/work/coney"},
            {"type": "volume", "source": f"{name}-ssh", "target": "/home/impl/.ssh"},
            {"type": "bind", "source": f"{ROOT}/tasks/{name}", "target": "/work/task", "read_only": True},
            {"type": "bind", "source": f"{ROOT}/artifacts/{name}", "target": "/work/artifacts"},
            {"type": "bind", "source": f"{ROOT}/archive/{name}", "target": "/home/impl/.claude"},
            {"type": "bind", "source": f"{ROOT}/outbox/{name}.git", "target": "/outbox"},
        ],
    }


def sidecar(name):
    return {
        "image": "coney-impl:local",
        "read_only": True,
        "cap_drop": ["ALL"],
        "cap_add": ["NET_ADMIN", "NET_RAW"],
        "security_opt": ["no-new-privileges:true"],
        "tmpfs": ["/run:size=1m", "/tmp:size=1m"],
        "cpus": 0.25,
        "mem_limit": "134217728",
        "networks": {name: None},
        "healthcheck": {"test": ["CMD", "test", "-f", "/run/coney/firewall-ok"]},
    }


def good_config():
    services = {}
    for n in POOL:
        services[n] = worker(n)
        services[n + "-net"] = sidecar(n)
    return {
        "name": "coney-impl",
        "services": services,
        "networks": {n: {"name": f"coney-impl_{n}"} for n in POOL},
        "volumes": {f"{n}-{k}": {"name": f"coney-impl_{n}-{k}"} for n in POOL for k in ("coney", "ssh")},
    }


def inspect_pair(name):
    w = {
        "Id": "w" * 64,
        "State": {"Status": "running", "Running": True},
        "HostConfig": {
            "Privileged": False,
            "CapDrop": ["ALL"],
            "CapAdd": None,
            "SecurityOpt": ["no-new-privileges:true"],
            "ReadonlyRootfs": True,
            "NetworkMode": "container:" + "n" * 64,
            "NanoCpus": 3_000_000_000,
            "Memory": 5 << 30,
        },
        "Mounts": [{"Destination": t, "Source": "/x"} for t in impl.MOUNTS],
    }
    n = {
        "Id": "n" * 64,
        "State": {"Status": "running", "Running": True, "Health": {"Status": "healthy"}},
        "HostConfig": {
            "Privileged": False,
            "CapDrop": ["ALL"],
            "CapAdd": ["CAP_NET_ADMIN", "CAP_NET_RAW"],
            "SecurityOpt": ["no-new-privileges:true"],
            "ReadonlyRootfs": True,
        },
        "Mounts": [],
    }
    return w, n


class FakeDistro:
    """Answers commands as the distro and Docker would; records every command with its standard input."""

    def __init__(self, cfg=None, containers=None, outbox=None, files=None, session_exit=0, image=True):
        self.cfg = cfg or good_config()
        self.containers = containers if containers is not None else {}
        self.outbox = outbox or {}  # name -> {ref: sha}
        self.files = files or {}
        self.session_exit = session_exit
        self.image = image
        self.held = False  # the keep-alive
        self.calls = []  # (kind, argv, data)
        self.streamed = []
        self.wsl = "wsl.exe"
        self.name = "test-docker"

    def argv(self, cmd):
        return [self.wsl, "-d", self.name, "-u", "root", "--exec", *cmd]

    def answer(self, cmd, data):
        if cmd[0] in ("pgrep", "pkill"):
            held, self.held = self.held, self.held and cmd[0] == "pgrep"
            return (0, b"") if held else (1, b"")
        if cmd[:3] == ["docker", "image", "inspect"]:
            return (0, b"1500\n") if self.image else (1, b"")
        if "compose" in cmd and "config" in cmd:
            return 0, json.dumps(self.cfg).encode()
        if cmd[:2] == ["docker", "inspect"]:
            info = self.containers.get(cmd[-1])
            return (0, json.dumps([info]).encode()) if info else (1, b"[]")
        if cmd[0] == "cat":
            v = self.files.get(cmd[1])
            return (0, v) if v is not None else (1, b"")
        if cmd[0] == "test" and cmd[1] == "-f" and cmd[2].endswith("/HEAD"):
            name = cmd[2].rsplit("/", 2)[-2].removesuffix(".git")
            return (0, b"") if name in self.outbox else (1, b"")
        if "for-each-ref" in cmd:
            path = cmd[cmd.index("-C") + 1]
            name = path.rsplit("/", 1)[-1].removesuffix(".git")
            refs = self.outbox.get(name, {})
            return 0, "".join(f"{sha} {ref}\n" for ref, sha in refs.items()).encode()
        if cmd[:2] == ["docker", "exec"] and "git" in cmd and "config" in cmd:
            return 0, {"gpg.format": b"ssh\n", "user.signingkey": impl.KEY.encode() + b"\n"}.get(cmd[-1], b"true\n")
        if cmd[:2] == ["docker", "exec"] and impl.MANAGED in cmd:
            v = self.files.get(impl.MANAGED)
            return (0, v) if v is not None else (1, b"")
        if cmd[:2] == ["docker", "exec"] and impl.KEYGEN in cmd:
            return 0, b"new\nssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAItestkey impl-1\n"
        return 0, b""

    def run(self, cmd, data=None, ok=(0,), timeout=None):
        self.calls.append(("run", cmd, data))
        code, out = self.answer(cmd, data)
        if code not in ok:
            raise impl.ImplError(f"{' '.join(cmd[:3])} failed (exit {code})")
        return subprocess.CompletedProcess(self.argv(cmd), code, out, b"")

    def detach(self, cmd):
        self.calls.append(("detach", cmd, None))
        self.held = True

    def stream(self, cmd, data=None):
        self.calls.append(("stream", cmd, data))
        self.streamed.append((cmd, data))
        if impl.SESSION in cmd:
            return self.session_exit
        return 0

    def lines(self, cmd, data=None, each=None):
        self.calls.append(("lines", cmd, data))
        self.streamed.append((cmd, data))
        if impl.SESSION not in cmd:
            return 0
        for raw in SESSION_EVENTS:
            each(raw)
        return self.session_exit

    def reader(self, cmd):
        self.calls.append(("reader", cmd, None))
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w") as t:

            def add(name, data=b"x", kind=tarfile.REGTYPE, link=""):
                info = tarfile.TarInfo(name)
                info.type, info.linkname = kind, link
                info.size = len(data) if kind == tarfile.REGTYPE else 0
                t.addfile(info, io.BytesIO(data) if kind == tarfile.REGTYPE else None)

            add("./good.txt", b"kept")
            add("./sub", kind=tarfile.DIRTYPE)
            add("./sub/deep.log", b"also kept")
            add("./link", kind=tarfile.SYMTYPE, link="/etc/passwd")
            add("../escape.txt", b"no")
            add("/abs.txt", b"no")
            add("./fifo", kind=tarfile.FIFOTYPE)

        class P:
            stdout = io.BytesIO(buf.getvalue())
            stderr = io.BytesIO(b"")

            def wait(self):
                return 0

        return P()

    def commands(self):
        return [c for _, c, _ in self.calls]


class WorkspaceContainers(unittest.TestCase):
    def setUp(self):
        t = tempfile.TemporaryDirectory()
        self.addCleanup(t.cleanup)
        self.tmp = Path(t.name)

    def load(self, **change):
        c = {
            "distro": "test-docker",
            "wsl_root": ROOT,
            "pool": list(POOL),
            "cpus": 3,
            "memory": "5g",
            "auth_file": "public:containers\\secrets\\auth.env",
            "model": "claude-opus-5-5",
            "effort": "medium",
        }
        c.update(change)
        return workspace.load(write_workspace(self.tmp, {k: v for k, v in c.items() if v is not ...}))

    def test_valid(self):
        ws = self.load()
        self.assertEqual(ws.containers["pool"], POOL)
        s = impl.settings(ws)
        self.assertEqual(s.memory, 5 << 30)
        self.assertEqual(s.auth_file, self.tmp / "pub" / "containers" / "secrets" / "auth.env")
        self.assertEqual((s.model, s.effort), ("claude-opus-5-5", "medium"))
        self.assertEqual(
            s.env("1500"), {"CONEY_ROOT": ROOT, "CONEY_CPUS": "3", "CONEY_MEMORY": "5g", "CONEY_UID": "1500"}
        )
        self.assertEqual(self.load(cpus=1.5, memory="512m").containers["cpus"], 1.5)

    def test_no_table_is_fine_until_asked(self):
        ws = workspace.load(write_workspace(self.tmp, drop=("containers",)))
        with self.assertRaises(workspace.WorkspaceError):
            _ = ws.containers

    def test_invalid(self):
        bad = [
            {"distro": ""},
            {"distro": "a b"},
            {"distro": ...},
            {"wsl_root": "/"},
            {"wsl_root": "srv/coney"},
            {"wsl_root": "/srv/../etc"},
            {"wsl_root": "/srv/co ney"},
            {"wsl_root": "C:\\srv"},
            {"pool": []},
            {"pool": ["impl-1", "impl-1"]},
            {"pool": ["impl-1-net"]},
            {"pool": ["worker"]},
            {"pool": "impl-1"},
            {"cpus": 0},
            {"cpus": True},
            {"cpus": "3"},
            {"memory": "lots"},
            {"memory": "5gb!"},
            {"memory": 0},
            {"memory": ...},
            {"auth_file": "nowhere:auth.env"},
            {"auth_file": "public:..\\auth.env"},
            {"auth_file": "C:\\auth.env"},
            {"model": ...},
            {"model": "opus"},
            {"model": "claude-opus-5-5 --dangerous"},
            {"effort": ...},
            {"effort": "huge"},
        ]
        for change in bad:
            with self.subTest(change=change), self.assertRaises(workspace.WorkspaceError):
                self.load(**change)
        with self.assertRaises(workspace.WorkspaceError):
            workspace.load(write_workspace(self.tmp, containers=["not", "an", "object"]))

    def test_sizes(self):
        self.assertEqual(workspace.size_bytes("5g"), 5 << 30)
        self.assertEqual(workspace.size_bytes("512M"), 512 << 20)
        self.assertEqual(workspace.size_bytes("1.5g"), 3 << 29)
        self.assertEqual(workspace.size_bytes("5368709120"), 5 << 30)
        self.assertEqual(workspace.size_bytes(1024), 1024)
        for v in ("", "-1g", "g", "5 t", None, True, 0):
            with self.subTest(v=v), self.assertRaises(workspace.WorkspaceError):
                workspace.size_bytes(v)
        self.assertEqual(impl.size_text(5 << 30), "5g")
        self.assertEqual(impl.size_text(1000), "1000b")


class Hardening(unittest.TestCase):
    def setUp(self):
        t = tempfile.TemporaryDirectory()
        self.addCleanup(t.cleanup)
        self.s = impl.settings(workspace.load(write_workspace(t.name)))

    def problems(self, change):
        cfg = good_config()
        change(cfg)
        return impl.hardening_problems(cfg, self.s)

    def assertFlags(self, change, text):
        found = self.problems(change)
        self.assertTrue(any(text in p for p in found), f"{text!r} not in {found}")

    def test_good_config_passes(self):
        self.assertEqual(impl.hardening_problems(good_config(), self.s), [])

    def test_each_violation(self):
        w = lambda c: c["services"]["impl-1"]
        n = lambda c: c["services"]["impl-1-net"]
        cases = [
            (lambda c: w(c).update(privileged=True), "impl-1: privileged"),
            (lambda c: n(c).update(privileged=True), "impl-1-net: privileged"),
            (lambda c: w(c).update(cap_drop=["NET_RAW"]), "does not drop every capability"),
            (lambda c: w(c).pop("cap_drop"), "does not drop every capability"),
            (lambda c: w(c).update(cap_add=["NET_ADMIN"]), "impl-1: adds capabilities (NET_ADMIN)"),
            (lambda c: n(c).update(cap_add=["NET_ADMIN", "SYS_ADMIN"]), "beyond NET_ADMIN and NET_RAW: SYS_ADMIN"),
            (lambda c: w(c).update(security_opt=[]), "no-new-privileges is not set"),
            (lambda c: w(c).update(security_opt=["no-new-privileges:false"]), "no-new-privileges is not set"),
            (
                lambda c: w(c).update(security_opt=["no-new-privileges:true", "no-new-privileges:false"]),
                "no-new-privileges is not set",
            ),
            (lambda c: w(c)["security_opt"].append("seccomp=unconfined"), "security_opt seccomp is changed"),
            (lambda c: w(c)["security_opt"].append("seccomp:/x.json"), "security_opt seccomp is changed"),
            (lambda c: n(c)["security_opt"].append("apparmor=unconfined"), "security_opt apparmor is changed"),
            (lambda c: w(c)["security_opt"].append("label=disable"), "security_opt label is changed"),
            (lambda c: w(c)["security_opt"].append("systempaths=unconfined"), "security_opt systempaths"),
            (lambda c: w(c).update(read_only=False), "the root filesystem is not read-only"),
            (lambda c: w(c).pop("read_only"), "the root filesystem is not read-only"),
            (lambda c: w(c).update(tmpfs=["/home/impl"]), "no tmpfs on /tmp"),
            (lambda c: w(c).update(pid="host"), "shares the host's pid"),
            (lambda c: n(c).update(ipc="host"), "shares the host's ipc"),
            (lambda c: n(c).update(userns_mode="host"), "shares the host's userns_mode"),
            (lambda c: n(c).update(network_mode="host"), "uses the host's network"),
            (lambda c: w(c).update(devices=["/dev/kvm:/dev/kvm"]), "sets devices"),
            (lambda c: n(c).update(ports=[{"target": 22}]), "sets ports"),
            (lambda c: w(c).update(volumes_from=["impl-2"]), "sets volumes_from"),
            (lambda c: n(c).update(sysctls={"net.ipv4.ip_forward": "1"}), "sets sysctls"),
            (lambda c: w(c).update(user="root"), "runs as root"),
            (lambda c: w(c).update(user="0:0"), "runs as root"),
            (lambda c: w(c).pop("user"), "runs as root"),
            (lambda c: w(c).update(network_mode="service:impl-2-net"), "does not join its firewall's network"),
            (lambda c: w(c).pop("network_mode"), "does not join its firewall's network"),
            (lambda c: w(c).update(depends_on={}), "does not wait for impl-1-net to be healthy"),
            (
                lambda c: w(c)["depends_on"]["impl-1-net"].update(condition="service_started"),
                "does not wait for impl-1-net",
            ),
            (lambda c: w(c)["volumes"].pop(0), "does not mount /work/coney"),
            (
                lambda c: w(c)["volumes"].append(
                    {"type": "bind", "source": "/var/run/docker.sock", "target": "/var/run/docker.sock"}
                ),
                "mounts the Docker socket",
            ),
            (
                lambda c: w(c)["volumes"].append({"type": "bind", "source": f"{ROOT}/x", "target": "/work/x"}),
                "mounts /work/x, which is not one of its mounts",
            ),
            (lambda c: w(c)["volumes"][2].pop("read_only"), "/work/task is not read-only"),
            (lambda c: w(c)["volumes"][3].update(source="/home/someone"), "outside /srv/test-coney"),
            (lambda c: w(c)["volumes"][3].update(source=f"{ROOT}/../etc"), "outside /srv/test-coney"),
            (lambda c: w(c)["volumes"][1].update(type="bind", source=f"{ROOT}/ssh"), "is a bind mount, not a volume"),
            (lambda c: w(c)["volumes"][0].update(type="bind"), "is a bind mount, not a volume"),
            (lambda c: w(c)["volumes"].append(dict(w(c)["volumes"][0])), "mounts /work/coney twice"),
            (lambda c: w(c)["volumes"].append("/srv:/srv"), "a volume in short form"),
            (lambda c: w(c).update(cpus=8), "cpus 8, the workspace says 3"),
            (lambda c: w(c).update(mem_limit="1g"), "mem_limit 1g, the workspace says 5g"),
            (lambda c: w(c).pop("mem_limit"), "has no CPU or memory cap"),
            (lambda c: n(c).pop("cpus"), "impl-1-net: has no CPU or memory cap"),
            (lambda c: n(c).pop("healthcheck"), "has no health check"),
            (lambda c: n(c).update(healthcheck={"disable": True}), "has no health check"),
            (lambda c: n(c).update(healthcheck={"test": ["NONE"]}), "has no health check"),
            (
                lambda c: n(c).update(volumes=[{"type": "volume", "source": "v", "target": "/v"}]),
                "mounts volumes (the firewall needs none)",
            ),
            (lambda c: n(c).update(network_mode="service:impl-2-net"), "shares another container's network"),
            (lambda c: n(c).update(networks={"impl-1": None, "impl-2": None}), "not on exactly one network"),
            (
                lambda c: c["services"]["impl-2-net"].update(networks={"impl-1": None}),
                "impl-2-net: shares the network impl-1 with impl-1-net",
            ),
            (lambda c: c["networks"]["impl-1"].update(external=True), "network impl-1 is external"),
            (lambda c: c["networks"]["impl-1"].update(driver="host"), "does not use the bridge driver"),
            (lambda c: c["services"].pop("impl-2"), "impl-2: missing"),
            (lambda c: c["services"].pop("impl-2-net"), "impl-2-net: missing"),
            (lambda c: c["services"].update(extra=worker("extra")), "extra: a service outside the pool"),
        ]
        for change, text in cases:
            with self.subTest(text=text):
                self.assertFlags(change, text)

    def test_inspect(self):
        w, n = inspect_pair("impl-1")
        self.assertEqual(impl.inspect_problems(w, n), [])
        cases = [
            (lambda w, n: w["HostConfig"].update(Privileged=True), "implementer: privileged"),
            (lambda w, n: w["HostConfig"].update(CapAdd=["CAP_SYS_ADMIN"]), "implementer: adds capabilities"),
            (lambda w, n: n["HostConfig"].update(CapAdd=["CAP_SYS_ADMIN"]), "firewall: adds SYS_ADMIN"),
            (lambda w, n: n["HostConfig"].update(CapDrop=[]), "firewall: does not drop every capability"),
            (lambda w, n: w["HostConfig"].update(SecurityOpt=[]), "implementer: no-new-privileges is not set"),
            (lambda w, n: n["HostConfig"].update(ReadonlyRootfs=False), "firewall: the root filesystem"),
            (lambda w, n: w["HostConfig"].update(NetworkMode="bridge"), "not in its firewall's network namespace"),
            (lambda w, n: w["Mounts"].pop(), "differ from its mounts"),
            (
                lambda w, n: w["Mounts"].append(
                    {"Destination": "/var/run/docker.sock", "Source": "/var/run/docker.sock"}
                ),
                "implementer: mounts the Docker socket",
            ),
        ]
        for change, text in cases:
            w, n = inspect_pair("impl-1")
            change(w, n)
            with self.subTest(text=text):
                found = impl.inspect_problems(w, n)
                self.assertTrue(any(text in p for p in found), f"{text!r} not in {found}")


class Base(unittest.TestCase):
    def setUp(self):
        t = tempfile.TemporaryDirectory()
        self.addCleanup(t.cleanup)
        self.tmp = Path(t.name)
        self.wsp = write_workspace(self.tmp)
        self.ws = workspace.load(self.wsp)
        self.auth = self.tmp / "pub" / "containers" / "secrets" / "auth.env"
        self.compose = b"services:\n  impl-1:\n    image: coney-impl:local\n"

    def pool(self, fake):
        return impl.Pool(self.ws, fake, self.compose)

    def cli(self, fake, *argv):
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch.object(impl, "Distro", lambda name: fake),
            mock.patch.object(impl, "containers_dir", lambda: self.tmp),
            redirect_stdout(out),
            redirect_stderr(err),
        ):
            (self.tmp / impl.COMPOSE).write_bytes(self.compose)
            code = main(["impl", *argv, "--workspace", str(self.wsp)])
        return code, out.getvalue(), err.getvalue()


class Commands(Base):
    @unittest.skipUnless(sys.platform == "win32", "the distro is driven through wsl.exe: Windows only")
    def test_distro_argv_has_no_shell(self):
        d = impl.Distro("test-docker", wsl="C:\\Windows\\System32\\wsl.exe")
        self.assertEqual(
            d.argv(["docker", "ps"]),
            ["C:\\Windows\\System32\\wsl.exe", "-d", "test-docker", "-u", "root", "--exec", "docker", "ps"],
        )

    @unittest.skipIf(sys.platform == "win32", "elsewhere than Windows")
    def test_distro_needs_windows(self):
        with self.assertRaisesRegex(impl.ImplError, "needs Windows"):
            impl.Distro("test-docker", wsl="/usr/bin/wsl.exe")
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(impl, "containers_dir", lambda: self.tmp), redirect_stdout(out), redirect_stderr(err):
            (self.tmp / impl.COMPOSE).write_bytes(self.compose)
            code = main(["impl", "status", "--workspace", str(self.wsp)])
        self.assertEqual((code, out.getvalue()), (2, ""))
        self.assertIn("needs Windows", err.getvalue())

    def test_compose_argv(self):
        p = self.pool(FakeDistro())
        self.assertEqual(
            p.compose_argv("up", "-d"),
            [
                "env",
                f"CONEY_ROOT={ROOT}",
                "CONEY_CPUS=3",
                "CONEY_MEMORY=5g",
                "CONEY_UID=1500",
                "docker",
                "compose",
                "--project-directory",
                ROOT,
                "-f",
                "-",
                "up",
                "-d",
            ],
        )
        # without an image, a stand-in renders the file; up itself refuses
        p = self.pool(FakeDistro(image=False))
        self.assertIn("CONEY_UID=65534", p.compose_argv("config"))
        with self.assertRaises(impl.ImplError):
            p.uid()

    def test_compose_text_goes_to_standard_input(self):
        fake = FakeDistro()
        self.pool(fake).config()
        (cmd, data) = next((c, d) for k, c, d in fake.calls if "config" in c)
        self.assertEqual(data, self.compose)
        self.assertEqual(cmd[cmd.index("-f") + 1], "-")

    def test_as_impl(self):
        p = self.pool(FakeDistro())
        cmd = p.as_impl("git", "status")
        self.assertEqual(cmd[:2], ["env", "-i"])
        self.assertIn("--reuid=1500", cmd)
        self.assertIn("--clear-groups", cmd)
        self.assertEqual(cmd[-2:], ["git", "status"])

    def test_names(self):
        s = impl.settings(self.ws)
        self.assertEqual(impl.names_arg([], s), POOL)
        self.assertEqual(impl.names_arg(["impl-2", "impl-2"], s), ["impl-2"])
        with self.assertRaises(impl.ImplError):
            impl.names_arg(["impl-9"], s)

    def test_context_tar_is_fixed_and_lf(self):
        d = self.tmp / "ctx"
        (d / "sub").mkdir(parents=True)
        (d / "init.sh").write_bytes(b"#!/bin/bash\r\necho hi\r\n")
        (d / "sub" / "b.txt").write_bytes(b"b\n")
        a = impl.context_tar(d)
        os.utime(d / "init.sh", (1, 1))
        self.assertEqual(a, impl.context_tar(d))
        with tarfile.open(fileobj=io.BytesIO(a)) as t:
            self.assertEqual(t.getnames(), ["init.sh", "sub/b.txt"])
            self.assertEqual(t.extractfile("init.sh").read(), b"#!/bin/bash\necho hi\n")
            self.assertTrue(all(m.uid == 0 and m.mtime == 0 for m in t.getmembers()))

    def test_context_tar_adds_extra_files_and_folders_under_their_names(self):
        d, mod = self.tmp / "ctx", self.tmp / "mod"
        (mod / "hooks").mkdir(parents=True)
        d.mkdir()
        (d / "Dockerfile").write_bytes(b"FROM x\n")
        (mod / "hooks" / "a.ts").write_bytes(b"a\r\n")
        (self.tmp / "cfg.json").write_bytes(b"{}\n")
        a = impl.context_tar(d, [("tether", mod), ("tether.json", self.tmp / "cfg.json")])
        with tarfile.open(fileobj=io.BytesIO(a)) as t:
            self.assertEqual(t.getnames(), ["Dockerfile", "tether.json", "tether/hooks/a.ts"])
            self.assertEqual(t.extractfile("tether/hooks/a.ts").read(), b"a\n")

    def test_a_build_without_tether_is_refused(self):
        paths = {"tether_mod": self.tmp / "no-mod", "tether_configs": self.tmp / "no-configs"}
        ws = mock.Mock(where=lambda key: paths[key])
        with self.assertRaises(impl.ImplError) as e:
            impl.tether_context(ws)
        self.assertIn("Tether is not installed", str(e.exception))
        (paths["tether_mod"] / ".claude-plugin").mkdir(parents=True)
        (paths["tether_mod"] / ".claude-plugin" / "plugin.json").write_text("{}", encoding="utf-8")
        paths["tether_configs"].mkdir()
        (paths["tether_configs"] / "container.json").write_text("{}", encoding="utf-8")
        self.assertEqual([n for n, _ in impl.tether_context(ws)], ["tether", "tether.json"])


class Auth(Base):
    def write(self, text):
        self.auth.parent.mkdir(parents=True, exist_ok=True)
        self.auth.write_text(text, encoding="utf-8")

    def test_reads(self):
        self.write(f"# made by the maintainer\n\nCLAUDE_CODE_OAUTH_TOKEN={SECRET}\n")
        self.assertEqual(impl.read_auth(self.auth), {"CLAUDE_CODE_OAUTH_TOKEN": SECRET})

    def test_missing_says_how_to_make_it(self):
        with self.assertRaises(impl.ImplError) as e:
            impl.read_auth(self.auth)
        self.assertIn("claude setup-token", str(e.exception))
        self.assertIn("API key is refused", str(e.exception))

    def test_bad_files_never_show_a_value(self):
        for text in (
            SECRET,
            f"export CLAUDE_CODE_OAUTH_TOKEN={SECRET}",
            f"OTHER_KEY={SECRET}",
            f"ANTHROPIC_API_KEY={SECRET}",
            f"CLAUDE_CODE_OAUTH_TOKEN={SECRET}\nCLAUDE_CODE_OAUTH_TOKEN={SECRET}",
            "CLAUDE_CODE_OAUTH_TOKEN=",
            "# only a comment\n",
        ):
            self.write(text)
            with self.subTest(text=text[:20]), self.assertRaises(impl.ImplError) as e:
                impl.read_auth(self.auth)
            self.assertNotIn(SECRET, str(e.exception))
            self.assertNotIn(SECRET[:12], str(e.exception))

    def test_session_command_carries_no_secret(self):
        auth = {"CLAUDE_CODE_OAUTH_TOKEN": SECRET}
        author = ("Maintainer", "m@example.invalid")
        argv = impl.session_argv("impl-1", "task/one", auth, "claude-opus-5-5", "medium", author)
        self.assertNotIn(SECRET, " ".join(argv))
        self.assertIn("CLAUDE_CODE_OAUTH_TOKEN", argv)
        self.assertEqual(argv[-7:], ["impl-1", impl.SESSION, "task/one", "claude-opus-5-5", "medium", *author])
        self.assertIn(SECRET.encode(), impl.auth_input(auth))
        self.assertTrue(impl.auth_input(auth).endswith(b"\n\n"))  # an empty line ends the variables

    def test_commits_name_the_maintainer_as_committer(self):
        session = (Path(__file__).resolve().parents[2] / "containers" / "session.sh").read_text(encoding="utf-8")
        self.assertIn('GIT_COMMITTER_NAME="$author_name" GIT_COMMITTER_EMAIL="$author_email"', session)
        self.assertIn('GIT_COMMITTER_NAME="$2" GIT_COMMITTER_EMAIL="$3"', impl.REAUTHOR)

    def test_run_refuses_without_auth_file(self):
        task = self.tmp / "task.md"
        task.write_text("# Task\n", encoding="utf-8")
        fake = FakeDistro()
        code, _out, err = self.cli(fake, "run", "impl-1", "--task", str(task), "--branch", "task/one")
        self.assertEqual(code, 2)
        self.assertIn("claude setup-token", err)
        self.assertFalse([c for c in fake.commands() if c[:2] == ["docker", "exec"]])

    def run_session(self, fake, **kw):
        self.write(f"CLAUDE_CODE_OAUTH_TOKEN={SECRET}\n")
        task = self.tmp / "task.md"
        task.write_bytes(b"# Task\n\nDo one thing.\n")
        self.bases = []

        def bundle(pool, git, tmp, base=None):
            self.bases.append(base)
            return b"BUNDLE"

        extra = ["--base=" + kw["base"]] if "base" in kw else []
        with (
            mock.patch.object(impl, "main_bundle", bundle),
            mock.patch.object(impl, "author_of", lambda git, repo: ("Maintainer", "m@example.invalid")),
            mock.patch.object(impl.signing, "program", lambda name, given=None: "git"),
        ):
            branch = "--branch=" + kw.get("branch", "task/one")
            return self.cli(fake, "run", "impl-1", "--task", str(task), branch, *extra)

    def running(self):
        w, n = inspect_pair("impl-1")
        return {"impl-1": w, "impl-1-net": n}

    def test_run_secret_only_on_standard_input(self):
        fake = FakeDistro(containers=self.running(), outbox={"impl-1": {"refs/heads/task/one": "a" * 40}})
        code, out, err = self.run_session(fake)
        self.assertEqual(code, 0, err)
        self.assertNotIn(SECRET, out + err)
        for kind, cmd, data in fake.calls:
            self.assertNotIn(SECRET, " ".join(cmd))
            if impl.SESSION not in cmd:
                self.assertNotIn(SECRET.encode(), data or b"")
        session = [(c, d) for c, d in fake.streamed if impl.SESSION in c]
        self.assertEqual(len(session), 1)
        self.assertIn(SECRET.encode(), session[0][1])
        # the task went in read-only, the bundle to the container's /tmp
        puts = [(c, d) for k, c, d in fake.calls if c[:2] == ["sh", "-c"] and c[2] == impl.PUT]
        self.assertEqual(puts[0][0][4:], [f"{ROOT}/tasks/impl-1/task.md", "0444", "root:root"])
        self.assertEqual(puts[0][1], b"# Task\n\nDo one thing.\n")
        bundle = [d for k, c, d in fake.calls if impl.BUNDLE in c]
        self.assertEqual(bundle, [b"BUNDLE"])
        self.assertIn("task/one pushed to its outbox", out)

    def test_run_writes_the_live_log_and_prints_the_final_message(self):
        fake = FakeDistro(containers=self.running(), outbox={"impl-1": {"refs/heads/task/one": "a" * 40}})
        code, out, err = self.run_session(fake)
        self.assertEqual(code, 0, err)
        self.assertIn("All done.", out)
        code, out, _ = self.cli(fake, "log", "impl-1")
        self.assertEqual(code, 0)
        self.assertIn("impl-1: exit 0, task.md on task/one", out)
        self.assertIn("Bash     run the tests (ctest)", out)
        self.assertIn("done     ok after 3 turns, 12 s", out)
        self.assertEqual(self.cli(fake, "log", "x; rm")[0], 2)
        state = json.loads((self.tmp / "pub" / "containers" / "live" / "impl-1.json").read_text(encoding="utf-8"))
        self.assertEqual((state["model"], state["effort"]), ("claude-opus-5-5", "medium"))
        self.assertEqual(state["served"], {"claude-opus-5-5": 1})  # as the API reported it
        session = next(c for c, _ in fake.streamed if impl.SESSION in c)
        self.assertEqual(session[-5:], ["task/one", "claude-opus-5-5", "medium", "Maintainer", "m@example.invalid"])

    def test_run_on_a_base_branch(self):
        fake = FakeDistro(containers=self.running(), outbox={"impl-1": {"refs/heads/task/one": "a" * 40}})
        code, _out, err = self.run_session(fake, base="impl-1/engine/store")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.bases, ["impl-1/engine/store"])
        state = json.loads((self.tmp / "pub" / "containers" / "live" / "impl-1.json").read_text(encoding="utf-8"))
        self.assertEqual(state.get("base"), "impl-1/engine/store")

    @unittest.skipUnless(shutil.which("git"), "needs git on PATH")
    def test_main_bundle_takes_a_base_as_main(self):
        git = shutil.which("git")

        def g(repo, *args):
            env = impl.signing.git_env(GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@x", GIT_COMMITTER_NAME="t",
                                       GIT_COMMITTER_EMAIL="t@x")  # fmt: skip
            return subprocess.run([git, "-C", str(repo), *args], env=env, capture_output=True, check=True).stdout

        repo, collecting = self.tmp / "repo", self.tmp / "collecting.git"
        repo.mkdir()
        g(repo, "init", "-q", "-b", "main")
        g(repo, "commit", "-q", "--allow-empty", "-m", "main")
        g(self.tmp, "init", "-q", "--bare", str(collecting))
        g(collecting, "fetch", "-q", str(repo), "main:refs/heads/impl-1/engine/store")
        g(repo, "commit", "-q", "--allow-empty", "-m", "later")
        wip = g(collecting, "rev-parse", "impl-1/engine/store").strip()
        pool = types.SimpleNamespace(s=types.SimpleNamespace(collecting=collecting, repo=repo))
        for base, want in ((None, g(repo, "rev-parse", "main").strip()), ("impl-1/engine/store", wip)):
            with self.subTest(base=base), tempfile.TemporaryDirectory() as tmp:
                data = impl.main_bundle(pool, git, tmp, base)
                (Path(tmp) / "b.bundle").write_bytes(data)
                heads = g(self.tmp, "bundle", "list-heads", str(Path(tmp) / "b.bundle")).decode().split()
                self.assertEqual(heads, [want.decode(), "refs/heads/main"])
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(impl.ImplError):
            impl.main_bundle(pool, git, tmp, "impl-1/engine/none")

    @unittest.skipUnless(shutil.which("git"), "needs git on PATH")
    def test_author_comes_from_the_checkout(self):
        git, repo = shutil.which("git"), self.tmp / "who"
        repo.mkdir()
        subprocess.run([git, "-C", str(repo), "init", "-q"], check=True)
        for k, v in (("user.name", "Maintainer"), ("user.email", "m@example.invalid")):
            subprocess.run([git, "-C", str(repo), "config", k, v], check=True)
        self.assertEqual(impl.author_of(git, repo), ("Maintainer", "m@example.invalid"))
        subprocess.run([git, "-C", str(repo), "config", "user.email", "x <y>"], check=True)
        with self.assertRaises(impl.ImplError):
            impl.author_of(git, repo)

    def test_run_branch_not_pushed_fails(self):
        fake = FakeDistro(containers=self.running())
        code, out, _err = self.run_session(fake)
        self.assertEqual(code, 1)
        self.assertIn("not in its outbox", out)

    def test_run_refuses_bad_branch_and_stopped_container(self):
        fake = FakeDistro(containers=self.running())
        for branch in ("-x", "a b", "a..b", "x.lock", "a/b/c/d/e", "", "x;y"):
            with self.subTest(branch=branch):
                code, _out, _err = self.run_session(fake, branch=branch)
                self.assertEqual(code, 2)
        for base in ("-x", "a..b", "x;y"):
            with self.subTest(base=base):
                self.assertEqual(self.run_session(fake, base=base)[0], 2)
        w, n = inspect_pair("impl-1")
        n["State"]["Health"]["Status"] = "unhealthy"
        code, _out, err = self.run_session(FakeDistro(containers={"impl-1": w, "impl-1-net": n}))
        self.assertEqual(code, 2)
        self.assertIn("impl up impl-1", err)


class Up(Base):
    def test_refuses_on_any_problem_and_starts_nothing(self):
        cfg = good_config()
        cfg["services"]["impl-2"]["privileged"] = True
        fake = FakeDistro(cfg=cfg)
        code, out, _err = self.cli(fake, "up", "impl-1")
        self.assertEqual(code, 1)
        self.assertIn("REFUSED impl-2: privileged", out)
        self.assertFalse([c for c in fake.commands() if "up" in c])

    def test_refuses_without_image(self):
        code, _out, err = self.cli(FakeDistro(image=False), "up")
        self.assertEqual(code, 2)
        self.assertIn("impl build", err)

    def test_starts_and_prints_new_key(self):
        fake = FakeDistro()
        code, out, err = self.cli(fake, "up", "impl-1")
        self.assertEqual(code, 0, err)
        self.assertIn('impl-1 namespaces="git" ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAItestkey', out)
        detached = [c for k, c, _ in fake.calls if k == "detach"]
        self.assertEqual(detached, [["bash", "-c", f"exec -a {impl.KEEPALIVE} sleep infinity"]])
        self.assertTrue(fake.held)
        # down releases the distro once nothing runs
        code, out, err = self.cli(fake, "down")
        self.assertEqual(code, 0, err)
        self.assertFalse(fake.held)
        self.assertIn("the distro may stop", out)
        ups = [c for c in fake.commands() if "compose" in c and "up" in c]
        self.assertEqual(len(ups), 1)
        self.assertEqual(ups[0][-1], "impl-1")
        prep = [c for c in fake.commands() if c[:2] == ["sh", "-c"] and c[2] == impl.PREPARE]
        self.assertEqual(
            prep[0][4:],
            [
                "1500",
                f"{ROOT}/tasks/impl-1",
                f"{ROOT}/outbox/impl-1.git",
                f"{ROOT}/artifacts/impl-1",
                f"{ROOT}/archive/impl-1",
            ],
        )


class Collect(Base):
    def test_copy_out_keeps_plain_files_inside(self):
        dest = self.tmp / "out"
        copied, skipped = impl.copy_out(self.pool(FakeDistro()), "/x", dest)
        self.assertEqual(copied, 3)  # an absolute name is kept, inside dest
        self.assertEqual((dest / "good.txt").read_bytes(), b"kept")
        self.assertEqual((dest / "abs.txt").read_bytes(), b"no")
        self.assertEqual((dest / "sub" / "deep.log").read_bytes(), b"also kept")
        self.assertFalse((dest / "link").exists())
        self.assertFalse((self.tmp / "escape.txt").exists())
        self.assertGreaterEqual(skipped, 3)

    def test_collect_reports_each_needs_file_and_does_not_fail_on_a_malformed_one(self):
        good = {"schema": "coney-needs/1", "questions": [{"id": "q-1", "page": None, "topic": "t", "question": "q?"}]}
        files = {"impl-1": json.dumps(good).encode(), "impl-2": b'{"schema": "coney-needs/1"}'}

        def copy_out(pool, src, dest):
            dest = Path(dest)
            dest.mkdir(parents=True, exist_ok=True)
            if dest.parent.name == "artifacts":
                (dest / "needs.json").write_bytes(files[dest.name])
            return 1, 0

        with (
            mock.patch.object(impl, "collect_outbox", lambda pool, git, name: f"{name}: outbox empty"),
            mock.patch.object(impl, "copy_out", copy_out),
            mock.patch.object(impl.implog, "write_transcripts", lambda folder: 0),
        ):
            code, out, err = self.cli(FakeDistro(), "collect")
        self.assertEqual(code, 0, err)
        self.assertIn("page ids checked for form only", out)  # the made-up repo folder is no git repository
        self.assertIn("impl-1: needs: 1 questions", out)
        self.assertIn("impl-2: needs.json malformed: not an object with exactly questions, schema", out)
        files["impl-2"] = b"[]"  # the bare list task files asked for before the schema
        with (
            mock.patch.object(impl, "collect_outbox", lambda pool, git, name: f"{name}: outbox empty"),
            mock.patch.object(impl, "copy_out", copy_out),
            mock.patch.object(impl.implog, "write_transcripts", lambda folder: 0),
        ):
            code, out, err = self.cli(FakeDistro(), "collect")
        self.assertEqual(code, 0, err)
        self.assertIn("impl-2: needs: 0 questions", out)

    def test_reset_refuses_uncollected_work(self):
        fake = FakeDistro(outbox={"impl-1": {"refs/heads/task/one": "a" * 40}})
        with mock.patch.object(impl.signing, "program", lambda name, given=None: "git"):
            code, out, _err = self.cli(fake, "reset", "impl-1")
        self.assertEqual(code, 1)
        self.assertIn("not yet collected (refs/heads/task/one)", out)
        self.assertFalse([c for c in fake.commands() if "rm" in c])

    def test_reset_removes_everything_and_names_the_key(self):
        fake = FakeDistro()
        code, out, err = self.cli(fake, "reset", "impl-1", "--force")
        self.assertEqual(code, 0, err)
        cmds = fake.commands()
        self.assertIn(["docker", "volume", "rm", "--force", "coney-impl_impl-1-coney", "coney-impl_impl-1-ssh"], cmds)
        rm = next(c for c in cmds if c[:2] == ["rm", "-rf"])
        self.assertIn(f"{ROOT}/outbox/impl-1.git", rm)
        self.assertIn("allowed_signers", out)


class GitHubKeys(unittest.TestCase):
    KEY1 = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    KEY2 = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"

    def setUp(self):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        self.repo = Path(d.name)
        (self.repo / "allowed_signers").write_text(
            f'# keys\nimpl-1 namespaces="git" {self.KEY1}\nimpl-2 namespaces="git" {self.KEY2}\n'
            f'pr-analyst-1 namespaces="coney-verdict" {self.KEY1}\n',
            encoding="utf-8",
        )

    def test_implementer_keys_come_from_allowed_signers(self):
        self.assertEqual(impl.implementer_keys(self.repo), {"impl-1": self.KEY1, "impl-2": self.KEY2})

    def test_a_key_missing_on_github_is_named(self):
        with mock.patch.object(impl, "github_signing_keys", lambda: {self.KEY1}):
            problems = impl.github_key_problems(self.repo)
        self.assertEqual(len(problems), 1)
        self.assertIn("impl-2's key is not a signing key", problems[0])
        self.assertIn('--type signing --title "coney impl-2"', problems[0])

    def test_all_keys_on_github_pass(self):
        with mock.patch.object(impl, "github_signing_keys", lambda: {self.KEY1, self.KEY2}):
            self.assertEqual(impl.github_key_problems(self.repo), [])

    def test_no_gh_access_names_the_scope(self):
        def fail():
            raise impl.signing.SigningError("gh not found on PATH")

        with mock.patch.object(impl, "github_signing_keys", fail):
            problems = impl.github_key_problems(self.repo)
        self.assertEqual(len(problems), 1)
        self.assertIn(impl.GITHUB_SCOPE, problems[0])


class Check(Base):
    def test_check_reports_probe_results(self):
        w, n = inspect_pair("impl-1")
        fake = FakeDistro(containers={"impl-1": w, "impl-1-net": n})
        probe = [{"what": "egress to example.com:443 is refused", "ok": True, "got": "ECONNREFUSED"}]
        with mock.patch.object(impl, "probe", lambda pool, name, args: probe):
            code, out, err = self.cli(fake, "check")
        self.assertEqual(code, 0, err)
        self.assertIn("PASS impl-1: egress to example.com:443 is refused", out)
        with mock.patch.object(impl, "probe", lambda pool, name, args: [dict(probe[0], ok=False)]):
            code, out, err = self.cli(fake, "check")
        self.assertEqual(code, 1)

    def test_check_wants_the_claude_settings(self):
        self.wsp = write_workspace(self.tmp, claude_settings={"comment": "why", "env": {"MADE_UP_TTL": "1h"}})
        w, n = inspect_pair("impl-1")
        cases = [
            (b'{"env": {"MADE_UP_TTL": "1h"}}', 0, None),
            (b'{"env": {"MADE_UP_TTL": "5m"}}', 1, "FAIL impl-1: impl-1: env.MADE_UP_TTL is '5m', not '1h'"),
            (b'{"env": {}}', 1, "FAIL impl-1: impl-1: env.MADE_UP_TTL is unset"),
            (None, 1, f"FAIL impl-1: impl-1: {impl.MANAGED} is missing"),
        ]
        for text, want, line in cases:
            fake = FakeDistro(containers={"impl-1": w, "impl-1-net": n}, files={impl.MANAGED: text})
            with self.subTest(text=text), mock.patch.object(impl, "probe", lambda pool, name, args: []):
                code, out, err = self.cli(fake, "check")
                self.assertEqual(code, want, out + err)
                if line:
                    self.assertIn(line, out)

    def test_check_with_nothing_running(self):
        code, _out, _err = self.cli(FakeDistro(), "check")
        self.assertEqual(code, 2)


@unittest.skipUnless(
    os.environ.get("CONEY_TEST_DOCKER") == "1", "set CONEY_TEST_DOCKER=1 to build and start a container"
)
class Integration(unittest.TestCase):
    """Builds the image, starts the first container of the pool, runs `impl check` on it and stops it again."""

    def cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = main(["impl", *argv])
        sys.__stdout__.write(out.getvalue() + err.getvalue())
        return code, out.getvalue(), err.getvalue()

    def test_build_up_check_down(self):
        name = workspace.load().containers["pool"][0]
        code, out, err = self.cli("build")
        self.assertEqual(code, 0, err)
        try:
            code, out, err = self.cli("up", name)
            self.assertEqual(code, 0, out + err)
            code, out, err = self.cli("check", name)
            self.assertEqual(code, 0, out + err)
            self.assertIn(f"PASS {name}: egress to example.com:443 is refused", out)
            self.assertNotIn("FAIL", out)
            code, out, err = self.cli("status")
            self.assertEqual(code, 0, err)
            self.assertIn(f"{name}: running, firewall healthy, key SHA256:", out)
        finally:
            self.cli("down", name)


if __name__ == "__main__":
    unittest.main()
