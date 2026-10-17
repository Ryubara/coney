"""Tests for the workspace loader and `zones check`, on made-up workspaces, accounts and roots in temp folders.

Folder permissions cannot be changed without administrator rights, so refusals are simulated by an injected probe,
and security descriptors are made-up SDDL strings."""

import io
import json
import os
import socket
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import ClassVar
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import workspace, zones
from coney_tools.cli import main

PUB, ANA, REV, TST, MNT = "zt-public", "zt-analyst", "zt-reviewer", "zt-tester", "zt-maint"
SIDS = {
    PUB: "S-1-5-21-1-2-3-1001",
    ANA: "S-1-5-21-1-2-3-1002",
    REV: "S-1-5-21-1-2-3-1003",
    TST: "S-1-5-21-1-2-3-1004",
    MNT: "S-1-5-21-1-2-3-1005",
}


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main(["zones", "check", *argv])
    return code, out.getvalue(), err.getvalue()


def closed_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Base(unittest.TestCase):
    def setUp(self):
        t = tempfile.TemporaryDirectory()
        self.addCleanup(t.cleanup)
        self.tmp = Path(t.name)
        self.users = self.tmp / "Users"
        for a in (PUB, ANA, REV, TST, MNT):
            (self.users / a / ".claude").mkdir(parents=True)
        self.r = {k: self.tmp / k for k in ("pub", "ana", "tst", "mach")}
        for p in self.r.values():
            p.mkdir()
        for sub in ("exchange/to-ana", "exchange/from-ana", "exchange/to-tst", "exchange/from-tst", "repo"):
            (self.r["pub"] / sub).mkdir(parents=True)
        (self.r["ana"] / "gate").mkdir()
        self.port = closed_port()

    def zones_doc(self, me=None, extra_roots=()):
        return {
            "maintainer": MNT,
            "roots": [
                {
                    "path": str(self.r["pub"]),
                    "role": "public",
                    "grants": {(me or PUB): "M", MNT: "F"},
                    "sub": [
                        {"path": "exchange\\to-ana", "read": [ANA]},
                        {"path": "exchange\\from-ana", "add_only": [ANA]},
                        {"path": "exchange\\to-tst", "read": [TST]},
                        {"path": "exchange\\from-tst", "add_only": [TST]},
                        {"path": "repo", "read": [ANA], "when_exists": True},
                        {"path": "later", "read": [ANA], "when_exists": True},
                    ],
                    "apply": "finish",
                },
                {
                    "path": str(self.r["ana"]),
                    "role": "analyst",
                    "grants": {ANA: "M"},
                    "read": [REV],
                    "sub": [{"path": "gate", "add_only_inherit": [REV]}],
                    "apply": "setup",
                },
                {"path": str(self.r["tst"]), "role": "tester", "grants": {TST: "M"}, "apply": "setup"},
                {
                    "path": str(self.r["mach"]),
                    "role": "machine",
                    "grants": {},
                    "read": ["*S-1-5-32-545"],
                    "no_owner_rights": True,
                    "apply": "setup",
                },
                *extra_roots,
            ],
            "accounts": [PUB, ANA, REV, TST],
        }

    def write_ws(self, zones_doc=None, **over):
        (self.tmp / "zones").mkdir(exist_ok=True)
        (self.tmp / "zones" / "zones.json").write_text(json.dumps(zones_doc or self.zones_doc()), encoding="utf-8")
        doc = {
            "version": 1,
            "zones_file": "zones\\zones.json",
            "paths": {
                "repo": "public:repo",
                "machine_source": "public:src",
                "breach_flag": "machine:state\\breach.json",
                "check_result": "machine:state\\check.json",
                "tools": "machine:tools",
            },
            "zones": {
                "public": {"account": PUB, "folder": "public:repo"},
                "analyst": {"account": ANA, "folder": "analyst:repo"},
                "reviewer-1": {"account": REV, "folder": "analyst:gate"},
                "tester": {"account": TST, "folder": "tester:"},
            },
            "analyst_ports": [{"name": "made-up port", "port": self.port}],
            "check": {"cache_minutes": 5, "schedule_minutes": 5},
            "install": {"items": ["ws.json", "hooks"], "skip": ["*.result.txt"]},
        }
        doc.update(over)
        p = self.tmp / "ws.json"
        p.write_text(json.dumps(doc), encoding="utf-8")
        return p

    def load(self, **kw):
        return workspace.load(self.write_ws(**kw))


class Workspace(Base):
    def test_resolves_roles(self):
        ws = self.load()
        self.assertEqual(ws.where("repo"), self.r["pub"] / "repo")
        self.assertEqual(ws.resolve("tester:"), self.r["tst"])
        self.assertEqual(ws.where("breach_flag"), self.r["mach"] / "state" / "breach.json")
        self.assertEqual(ws.zone_account("public"), PUB)
        self.assertEqual(ws.analyst_side_accounts(), {ANA, REV})

    def test_errors(self):
        cases = [
            ({"paths": {"x": "nowhere:a"}}, "unknown role"),
            ({"paths": {"x": "public:..\\a"}}, "stay under"),
            ({"paths": {"x": "public:C:\\a"}}, "stay under"),
            ({"paths": {"x": "no-role"}}, "not '<role>:<path>'"),
            ({"paths": {"x": "public:mods" + chr(9) + "ether"}}, "control character"),
            ({"version": 2}, "version"),
            ({"check": {"cache_minutes": 0, "schedule_minutes": 5}}, "cache_minutes"),
            ({"analyst_ports": [{"port": 70000}]}, "analyst_ports"),
            ({"analyst_ports": [{"port": 5000, "accounts": []}]}, r"analyst_ports\[0\]\.accounts must be a non-empty"),
            ({"analyst_ports": [{"port": 5000, "accounts": ANA}]}, r"accounts must be a non-empty list"),
            ({"analyst_ports": [{"port": 5000, "accounts": [ANA, ""]}]}, r"accounts must be a non-empty list"),
            ({"analyst_ports": [{"port": 5000, "accounts": [ANA, PUB]}]}, f"{PUB} is not an analyst-side account"),
            ({"analyst_ports": [{"port": 5000, "accounts": ["zt-nobody"]}]}, "not an analyst-side account"),
            ({"listener_exceptions": {}}, "listener_exceptions"),
            ({"listener_exceptions": [{"port": 5000}]}, r"listener_exceptions\[0\]"),
            ({"zones": {"x": {"account": "a", "folder": "elsewhere:"}}}, "unknown role"),
        ]
        for over, msg in cases:
            with self.subTest(msg=msg), self.assertRaisesRegex(workspace.WorkspaceError, msg):
                self.load(**over)

    def test_missing_keys_and_bad_json(self):
        p = self.write_ws()
        doc = json.loads(p.read_text())
        del doc["paths"]
        p.write_text(json.dumps(doc))
        with self.assertRaisesRegex(workspace.WorkspaceError, "'paths'"):
            workspace.load(p)
        p.write_text("{not json")
        with self.assertRaisesRegex(workspace.WorkspaceError, "not valid JSON"):
            workspace.load(p)
        with self.assertRaisesRegex(workspace.WorkspaceError, "cannot read"):
            workspace.load(self.tmp / "absent.json")
        z = self.zones_doc()
        z["roots"].append(dict(z["roots"][0]))
        with self.assertRaisesRegex(workspace.WorkspaceError, "given twice"):
            workspace.load(self.write_ws(z))

    def test_location(self):
        p = self.write_ws()
        with mock.patch.dict(os.environ, {workspace.ENV: str(p)}):
            self.assertEqual(workspace.locate(), p)
            self.assertEqual(workspace.locate("other.json"), Path("other.json"))
        env = {k: v for k, v in os.environ.items() if k.upper() not in (workspace.ENV, "PROGRAMDATA")}
        env["ProgramData"] = str(self.tmp)
        with mock.patch.dict(os.environ, env, clear=True):
            self.assertEqual(workspace.locate(), self.tmp / "Coney" / "coney.workspace.json")


class Walls(Base):
    """Expectations per account kind; the probe answers as correct walls would, from a set of refused folders."""

    def walls(self, me, refused, opened=()):
        ws = self.load()

        def probe(p):
            p = str(p)
            if p in map(str, opened):
                return "open"
            if p in map(str, refused):
                return "refused"
            return zones.probe(p)

        return zones.wall_checks(ws, me, probe, self.users)

    def homes_except(self, me):
        return [self.users / a for a in (PUB, ANA, REV, TST, MNT) if a != me] + [self.users / MNT / ".claude"]

    def wants(self, checks):
        return {(c.what, Path(c.path).relative_to(self.tmp).as_posix()): c.want for c in checks}

    def test_public(self):
        refused = [self.r["ana"], self.r["ana"] / "gate", self.r["tst"], *self.homes_except(PUB)]
        checks = self.walls(PUB, refused)
        self.assertTrue(all(c.ok for c in checks), [c for c in checks if not c.ok])
        w = self.wants(checks)
        self.assertEqual(w[("zone root", "pub")], "open")
        self.assertEqual(w[("zone sub-folder", "pub/exchange/from-ana")], "open")
        self.assertEqual(w[("zone root", "ana")], "refused")
        self.assertEqual(w[("zone sub-folder", "ana/gate")], "refused")
        self.assertEqual(w[("zone root", "mach")], "open")
        self.assertEqual(w[("own home", f"Users/{PUB}")], "open")
        self.assertEqual(w[("maintainer .claude", f"Users/{MNT}/.claude")], "refused")
        self.assertNotIn(("zone sub-folder", "pub/later"), w)  # when_exists and missing: skipped

    def test_analyst(self):
        refused = [self.r["pub"], self.r["tst"], *self.homes_except(ANA)]
        refused += [self.r["pub"] / s for s in ("exchange/to-tst", "exchange/from-tst")]
        checks = self.walls(ANA, refused)
        self.assertTrue(all(c.ok for c in checks), [c for c in checks if not c.ok])
        w = self.wants(checks)
        self.assertEqual(w[("zone sub-folder", "pub/exchange/to-ana")], "open")
        self.assertEqual(w[("zone sub-folder", "pub/repo")], "open")
        self.assertNotIn(("zone sub-folder", "pub/exchange/from-ana"), w)  # add only: not checked by listing
        self.assertEqual(w[("zone sub-folder", "pub/exchange/to-tst")], "refused")
        self.assertEqual(w[("zone root", "ana")], "open")

    def test_reviewer(self):
        subs = ("exchange/to-ana", "exchange/from-ana", "exchange/to-tst", "exchange/from-tst", "repo")
        refused = [self.r["pub"], *[self.r["pub"] / s for s in subs]]
        checks = self.walls(REV, [*refused, self.r["tst"], *self.homes_except(REV)])
        self.assertTrue(all(c.ok for c in checks), [c for c in checks if not c.ok])
        w = self.wants(checks)
        self.assertEqual(w[("zone root", "ana")], "open")
        self.assertEqual(w[("zone sub-folder", "ana/gate")], "open")
        self.assertEqual(w[("zone sub-folder", "pub/exchange/from-ana")], "refused")

    def test_tester(self):
        refused = [self.r["pub"], self.r["ana"], self.r["ana"] / "gate", *self.homes_except(TST)]
        refused += [self.r["pub"] / s for s in ("exchange/to-ana", "exchange/from-ana", "repo")]
        checks = self.walls(TST, refused)
        self.assertTrue(all(c.ok for c in checks), [c for c in checks if not c.ok])
        w = self.wants(checks)
        self.assertEqual(w[("zone sub-folder", "pub/exchange/to-tst")], "open")
        self.assertNotIn(("zone sub-folder", "pub/exchange/from-tst"), w)
        self.assertEqual(w[("zone root", "tst")], "open")

    def test_open_wall_is_a_breach(self):
        refused = [self.r["ana"] / "gate", self.r["tst"], *self.homes_except(PUB)]
        checks = self.walls(PUB, refused, opened=[self.r["ana"]])
        bad = [c for c in checks if not c.ok]
        self.assertEqual([(c.what, c.got, c.widens) for c in bad], [("zone root", "open", True)])
        self.assertTrue(zones.result("public", PUB, checks, [])["breach"])

    def test_refused_own_folder_fails_without_breach(self):
        refused = [self.r["pub"], self.r["ana"], self.r["ana"] / "gate", self.r["tst"], *self.homes_except(PUB)]
        bad = [c for c in self.walls(PUB, refused) if not c.ok]
        self.assertEqual([(c.what, c.widens) for c in bad], [("zone root", False)])

    def test_missing_counts_as_refused(self):
        self.assertEqual(zones.probe(self.tmp / "absent"), "missing")
        self.assertEqual(zones.probe(self.tmp), "open")


class Ports(Base):
    """No port wall stands in a test run, so a connection to this account's own listener goes through."""

    def test_free_port_without_wall_is_a_breach(self):
        ws = self.load()
        (c,) = zones.port_checks(ws, PUB)
        self.assertFalse(c.ok)
        self.assertTrue(c.widens)
        self.assertIn("127.0.0.1 own listener: connected", c.got)
        self.assertEqual(c.want, "no connection")
        for who in (ANA, REV):
            (c,) = zones.port_checks(ws, who)
            self.assertTrue(c.ok)
            self.assertIn("not checked", c.got)

    def test_port_accounts_check_the_other_analyst_side_accounts(self):
        port = {"name": "made-up port", "port": self.port, "accounts": [ANA.upper()]}
        ws = self.load(analyst_ports=[port])
        (c,) = zones.port_checks(ws, ANA)
        self.assertTrue(c.ok)
        self.assertEqual(c.got, "not checked: one of the port's accounts")
        for who in (REV, PUB):
            (c,) = zones.port_checks(ws, who)
            self.assertFalse(c.ok)
            self.assertTrue(c.widens)
            self.assertIn("connected", c.got)
        outcomes = {"127.0.0.1": "own listener: blocked", "::1": "own listener: blocked"}
        state = lambda p: zones.port_state(p, probe=lambda h, q: outcomes[h])
        (c,) = zones.port_checks(ws, REV, state)
        self.assertTrue(c.ok, c)

    def test_wall_allowed(self):
        ports = [{"port": 4001}, {"port": 4002, "accounts": [ANA]}, {"port": 4003, "accounts": [REV]}]
        ws = self.load(analyst_ports=[*ports, {"port": 4003, "accounts": [ANA]}])
        got = zones.wall_allowed(ws, SIDS.__getitem__)
        both = {ANA: SIDS[ANA], REV: SIDS[REV]}
        self.assertEqual(got, {4001: both, 4002: {ANA: SIDS[ANA]}, 4003: both})

    def test_port_in_use_that_accepts_is_a_breach(self):
        srv = socket.socket()
        srv.bind(("127.0.0.1", 0))
        srv.listen(4)
        self.addCleanup(srv.close)
        self.port = srv.getsockname()[1]
        got = zones.port_probe("127.0.0.1", self.port)
        self.assertEqual(got, "port in use: connected")
        (c,) = zones.port_checks(self.load(), PUB)
        self.assertFalse(c.ok)
        self.assertTrue(c.widens)

    def test_wall_turns_the_connection_away(self):
        outcomes = {"127.0.0.1": "own listener: blocked", "::1": "port in use: refused"}
        state = lambda port: zones.port_state(port, probe=lambda h, p: outcomes[h])
        (c,) = zones.port_checks(self.load(), PUB, state)
        self.assertTrue(c.ok, c)
        self.assertEqual(c.got, "127.0.0.1 own listener: blocked; ::1 port in use: refused")

    def test_connect_outcomes(self):
        cases = [
            (PermissionError(13, "denied"), "blocked"),
            (ConnectionRefusedError(), "refused"),
            (TimeoutError(), "timed out"),
            (OSError(99, "other"), "failed (99)"),
        ]
        for exc, want in cases:
            with self.subTest(want=want), mock.patch.object(zones.socket, "create_connection", side_effect=exc):
                self.assertEqual(zones._connect("127.0.0.1", 1, 0.1), want)


class Drift(Base):
    def test_drift_warnings(self):
        ws = self.load()
        src = self.r["pub"] / "src"
        (src / "hooks").mkdir(parents=True)
        (src / "ws.json").write_text("a")
        (src / "hooks" / "one.js").write_text("same")
        (src / "hooks" / "two.js").write_text("new")
        (src / "hooks" / "x.result.txt").write_text("skipped")
        (self.r["mach"] / "hooks").mkdir()
        (self.r["mach"] / "ws.json").write_text("a")
        (self.r["mach"] / "hooks" / "one.js").write_text("old")
        w = zones.drift_warnings(ws)
        self.assertEqual(len(w), 2, w)
        self.assertIn("one.js differs", w[0])
        self.assertIn("two.js is not installed", w[1])
        (self.r["mach"] / "hooks" / "one.js").write_text("same")
        (self.r["mach"] / "hooks" / "two.js").write_text("new")
        self.assertEqual(zones.drift_warnings(ws), [])


class ClaudeSettings(Base):
    WANT: ClassVar = {
        "comment": "why",
        "env": {"MADE_UP_TTL": "1h"},
        "madeUpInbound": "refuse",
        "permissions": {"deny": ["MadeUpTool"]},
    }

    def test_settings_env_checks(self):
        managed = self.tmp / "managed.json"
        ws = self.load(claude_settings=self.WANT, install={"items": ["ws.json"], "managed_settings": str(managed)})
        self.assertNotIn("comment", ws.claude_settings)
        good = {"env": {"MADE_UP_TTL": "1h", "OTHER": "x"}, "madeUpInbound": "refuse"}
        good["permissions"] = {"deny": ["Other", "MadeUpTool"], "allow": []}
        cases = [
            (json.dumps(good), True, "all held"),
            (
                '{"env": {"MADE_UP_TTL": "5m"}, "madeUpInbound": "accept", "permissions": {"deny": []}}',
                False,
                (
                    "env.MADE_UP_TTL is '5m', not '1h'; madeUpInbound is 'accept', not 'refuse'; "
                    "permissions.deny lacks 'MadeUpTool'"
                ),
            ),
            (
                '{"env": [], "permissions": {"deny": {}}}',
                False,
                "env is not an object; madeUpInbound is unset; permissions.deny is not a list",
            ),
            ("[1]", False, "the settings are not a JSON object"),
            ("{nope", False, "the settings are not JSON"),
        ]
        for text, ok, got in cases:
            managed.write_text(text, encoding="utf-8")
            with self.subTest(text=text):
                (c,) = zones.settings_env_checks(ws)
                self.assertEqual((c.ok, c.got), (ok, got))
        managed.unlink()
        (c,) = zones.settings_env_checks(ws)
        self.assertFalse(c.ok)
        self.assertIn("unreadable", c.got)
        self.assertEqual(zones.settings_env_checks(self.load()), [])

    def test_claude_program(self):
        with mock.patch.dict(os.environ, {"MADE_UP_DRIVE": "Q:"}):
            ws = self.load(claude_program={"comment": "why", "path": "%MADE_UP_DRIVE%\\Homes\\{account}\\t.exe"})
            self.assertEqual(ws.claude_program("ana"), "Q:\\Homes\\ana\\t.exe")
        self.assertIsNone(self.load().claude_program("ana"))
        for bad in ([], {"path": 1}, {"path": "C:\\no-account.exe"}):
            with self.subTest(bad=bad), self.assertRaisesRegex(workspace.WorkspaceError, "claude_program"):
                self.load(claude_program=bad)

    def test_claude_settings_must_be_an_object(self):
        with self.assertRaisesRegex(workspace.WorkspaceError, "claude_settings"):
            self.load(claude_settings=[])


ROOT_SDDL = (
    "O:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;{pub})(A;OICI;FA;;;{mnt})(A;OICI;0x1301bf;;;OW)"
)


def sddl(t):
    return t.format(pub=SIDS[PUB], mnt=SIDS[MNT], ana=SIDS[ANA], rev=SIDS[REV], tst=SIDS[TST])


class Sddl(unittest.TestCase):
    def test_parse(self):
        sd = zones.parse_sddl(sddl(ROOT_SDDL + "(A;OICIID;FA;;;BU)"))
        self.assertEqual(sd.owner, zones.ADMINS)
        self.assertTrue(sd.protected)
        self.assertEqual(len(sd.explicit), 5)
        self.assertEqual(sd.inherited, [zones.Ace("S-1-5-32-545", "allow", zones.FULL, zones.INHERIT_ALL)])
        self.assertIn(zones.Ace(zones.OWNER_RIGHTS, "allow", zones.MODIFY, zones.INHERIT_ALL), sd.explicit)

    def test_parse_forms(self):
        sd = zones.parse_sddl("O:S-1-5-21-9-9-9-500G:SYD:AI(D;CI;CCDCLC;;;WD)(A;;1179817;;;AU)")
        self.assertEqual(sd.owner, "S-1-5-21-9-9-9-500")
        self.assertFalse(sd.protected)
        self.assertEqual(sd.explicit[0], zones.Ace("S-1-1-0", "deny", 0x7, zones.INHERIT_FOLDERS))
        self.assertEqual(sd.explicit[1].mask, zones.READ_EXECUTE)
        self.assertEqual(zones.parse_sddl("O:BAD:P").explicit, [])

    def test_parse_errors(self):
        for bad in (
            "O:BA",
            "O:BAD:(A;OICI;ZZ;;;SY)",
            "O:BAD:(A;OICI;FA;;;QQ)",
            "O:BAD:(A;OICI;FA)",
            "O:BAD:(A;O;FA;;;SY)",
        ):
            with self.subTest(bad=bad), self.assertRaises(zones.SddlError):
                zones.parse_sddl(bad)

    def test_masks(self):
        self.assertEqual(zones.MODIFY, 0x1301BF)
        self.assertEqual(zones.READ_EXECUTE, 0x1200A9)
        self.assertEqual(zones.ADD_ONLY, 0x1200AF)
        self.assertEqual(zones.parse_sddl("O:BAD:(A;;FA;;;SY)").explicit[0].mask, zones.FULL)


class FullMode(Base):
    def setUp(self):
        super().setUp()
        z = self.zones_doc()
        z["roots"].append({"path": "Q:\\nowhere", "role": "gone", "grants": {}, "apply": "finish"})
        z["roots"].append(
            {"path": str(self.tmp / "manual"), "role": "manual", "grants": {MNT: "F"}, "apply": "maintainer"}
        )
        self.ws = workspace.load(self.write_ws(z))
        pub, ana, tst, mach = (str(self.r[k]) for k in ("pub", "ana", "tst", "mach"))
        ex = os.path.join(pub, "exchange", "")
        self.sd = {
            pub: sddl(ROOT_SDDL),
            ex + "to-ana": sddl("O:BAD:AI(A;OICI;0x1200a9;;;{ana})(A;OICIID;FA;;;SY)"),
            ex + "from-ana": sddl("O:BAD:AI(A;;0x1200af;;;{ana})(A;OICIID;FA;;;SY)"),
            ex + "to-tst": sddl("O:BAD:AI(A;OICI;0x1200a9;;;{tst})"),
            ex + "from-tst": sddl("O:BAD:AI(A;;0x1200af;;;{tst})"),
            os.path.join(pub, "repo"): sddl("O:BAD:AI(A;OICI;0x1200a9;;;{ana})"),
            ana: sddl(
                "O:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;{ana})"
                "(A;OICI;0x1200a9;;;{rev})(A;OICI;0x1301bf;;;OW)"
            ),
            os.path.join(ana, "gate"): sddl("O:BAD:AI(A;CI;0x1200af;;;{rev})"),
            tst: sddl("O:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;{tst})(A;OICI;0x1301bf;;;OW)"),
            mach: "O:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)",
            str(self.tmp / "manual"): sddl("O:S-1-5-21-1-2-3-1005D:PAI(A;OICI;FA;;;{mnt})(A;OICI;FA;;;SY)"),
        }

    def read(self, path):
        if path not in self.sd:
            raise FileNotFoundError(2, "not found", path)
        return self.sd[path]

    def full(self):
        exists = lambda p: not p.startswith("Q:")
        return zones.full_run(self.ws, self.read, lambda n: SIDS[n], exists)

    def test_clean(self):
        checks, warnings = self.full()
        self.assertTrue(all(c.ok for c in checks), [c for c in checks if not c.ok])
        self.assertEqual(len(warnings), 1)
        self.assertIn("drive Q: is missing", warnings[0])
        self.assertIn(
            ("zone sub-folder", "present when it exists", "missing"), [(c.what, c.want, c.got) for c in checks]
        )

    def failures(self):
        checks, _ = self.full()
        return [(c.what, c.want, c.got, c.widens) for c in checks if not c.ok]

    def test_extra_allow_entry_is_a_breach(self):
        self.sd[str(self.r["tst"])] += sddl("(A;OICI;0x1200a9;;;{pub})")
        (f,) = self.failures()
        self.assertEqual(f[:2], ("entry", "absent"))
        self.assertIn(f"{PUB} ({SIDS[PUB]})", f[2])
        self.assertTrue(f[3])

    def test_missing_grant_fails_without_breach(self):
        self.sd[str(self.r["tst"])] = self.sd[str(self.r["tst"])].replace(sddl("(A;OICI;0x1301bf;;;{tst})"), "")
        (f,) = self.failures()
        self.assertEqual((f[0], f[2], f[3]), ("entry", "missing", False))

    def test_missing_owner_rights_is_a_breach(self):
        self.sd[str(self.r["tst"])] = self.sd[str(self.r["tst"])].replace("(A;OICI;0x1301bf;;;OW)", "")
        (f,) = self.failures()
        self.assertTrue(f[3])
        self.assertIn("OWNER RIGHTS", f[1])

    def test_owner_inheritance_and_flags(self):
        tst = str(self.r["tst"])
        self.sd[tst] = self.sd[tst].replace("O:BAD:PAI", sddl("O:{tst}D:AI"))
        gate = str(self.r["ana"] / "gate")
        self.sd[gate] = self.sd[gate].replace("A;CI;", "A;OICI;")
        got = self.failures()
        self.assertIn(("owner", f"Administrators ({zones.ADMINS})", SIDS[TST], True), got)
        self.assertIn(("inheritance", "cut", "inherited", True), got)
        self.assertEqual(sum(1 for f in got if f[0] == "entry"), 2)  # the CI entry missing, the OICI one extra

    def test_sub_inheritance_cut_and_missing_sub(self):
        to_ana = str(self.r["pub"] / "exchange" / "to-ana")
        self.sd[to_ana] = self.sd[to_ana].replace("D:AI", "D:PAI")
        del self.sd[str(self.r["pub"] / "exchange" / "to-tst")]
        got = self.failures()
        self.assertIn(("inheritance", "inherited", "cut", False), got)
        self.assertIn(("zone sub-folder", "present", "missing", False), got)

    def test_sealed_sub(self):
        z = self.zones_doc()
        z["roots"][1]["sub"].append({"path": "notes-rev", "sealed": REV})
        self.ws = workspace.load(self.write_ws(z))
        sealed = str(self.r["ana"] / "notes-rev")
        good = sddl(
            "O:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;{mnt})(A;OICI;0x1301bf;;;{rev})(A;OICI;0x1301bf;;;OW)"
        )
        self.sd[sealed] = good
        self.assertEqual([f for f in self.failures() if "Q:" not in f[2]], [])
        # Inheriting from the root would let the analyst in: a breach
        self.sd[sealed] = good.replace("D:PAI", "D:AI")
        self.assertIn(("inheritance", "cut", "inherited", True), self.failures())
        # Another account's entry is a breach too
        self.sd[sealed] = good + sddl("(A;OICI;0x1200a9;;;{ana})")
        (f,) = self.failures()
        self.assertEqual((f[0], f[3]), ("entry", True))
        del self.sd[sealed]
        self.assertIn(("zone sub-folder", "present", "missing", False), self.failures())

    def test_sealed_sub_walls(self):
        z = self.zones_doc()
        z["roots"][1]["sub"].append({"path": "notes-rev", "sealed": REV})
        ws = workspace.load(self.write_ws(z))
        sealed = str(self.r["ana"] / "notes-rev")
        for me, want in ((REV, "open"), (ANA, "refused"), (PUB, "refused")):
            with self.subTest(me=me):
                (c,) = [c for c in zones.wall_checks(ws, me, lambda p: "open", self.users) if c.path == sealed]
                self.assertEqual((c.what, c.want), ("sealed sub-folder", want))

    def test_sealed_sub_is_validated(self):
        for bad, msg in (({"sealed": ""}, "must name one account"), ({"sealed": REV, "read": [ANA]}, "takes no read")):
            z = self.zones_doc()
            z["roots"][1]["sub"].append({"path": "notes-rev", **bad})
            with self.subTest(msg=msg), self.assertRaisesRegex(workspace.WorkspaceError, msg):
                workspace.load(self.write_ws(z))

    def test_manual_root(self):
        m = str(self.tmp / "manual")
        self.sd[m] += sddl("(A;OICIID;0x1200a9;;;{pub})")
        (f,) = self.failures()
        self.assertTrue(f[3])
        self.sd[m] = self.sd[m].replace("D:PAI", "D:AI").replace(sddl("(A;OICIID;0x1200a9;;;{pub})"), "")
        self.assertEqual(self.failures(), [("inheritance", "cut", "inherited", True)])

    def test_unresolvable_account(self):
        def sid(n):
            if n == TST:
                raise OSError(None, "account does not resolve")
            return SIDS[n]

        checks, _ = zones.full_run(self.ws, self.read, sid, lambda p: not p.startswith("Q:"))
        self.assertEqual([c.what for c in checks if not c.ok], ["accounts", "accounts"])  # two roots name it

    @unittest.skipUnless(os.name == "nt", "Windows security descriptors")
    def test_reads_a_real_descriptor(self):
        sd = zones.parse_sddl(zones.read_sd(str(self.tmp)))
        self.assertIsNotNone(sd.owner)
        self.assertTrue(sd.inherited or sd.explicit)
        self.assertTrue(zones.sid_of(zones.current_account()).startswith("S-1-"))
        with self.assertRaises(FileNotFoundError):
            zones.read_sd(str(self.tmp / "absent"))


class Cli(Base):
    def setUp(self):
        super().setUp()
        self.me = zones.current_account()
        # Every root readable for this account, so the real listing matches; homes are made-up names.
        z = self.zones_doc(me=self.me)
        z["roots"][1]["read"].append(self.me)
        z["roots"][2]["grants"][self.me] = "RX"
        self.clean = z
        self.ws_path = self.write_ws(z)
        # The port wall is not installed in a test run: answer as a standing wall would.
        walled = mock.patch.object(zones, "port_state", lambda port: "127.0.0.1 own listener: blocked")
        walled.start()
        self.addCleanup(walled.stop)
        # Nothing listens on the made-up analyst ports, whatever this machine runs.
        quiet = mock.patch.object(zones, "tcp_listeners", list)
        quiet.start()
        self.addCleanup(quiet.stop)

    def env(self):
        # home folders under a folder that does not exist: every other home is missing, which counts as refused
        return mock.patch.object(zones.Path, "home", return_value=self.tmp / "no-homes" / "me")

    def test_pass_and_json_shape(self):
        with self.env():
            code, out, err = run("--workspace", str(self.ws_path), "--mode", "public", "--json")
        self.assertEqual(code, 0, out + err)
        doc = json.loads(out)
        self.assertEqual(set(doc), {"checks", "warnings", "breach", "legacy", "mode", "account", "time"})
        self.assertEqual(set(doc["checks"][0]), {"what", "path", "want", "got", "ok"})
        self.assertEqual((doc["mode"], doc["account"], doc["breach"]), ("public", self.me, False))
        self.assertRegex(doc["time"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")

    def test_human_report(self):
        with self.env():
            code, out, _ = run("--workspace", str(self.ws_path), "--mode", "public")
        self.assertEqual(code, 0)
        self.assertIn("PASS:", out)

    def test_breach_exit_and_state(self):
        z = self.zones_doc(me=self.me)  # the analyst and tester roots are not granted, but open: a breach
        ws_path = self.write_ws(z)
        cache = self.tmp / "cache" / "zones-check.json"
        with self.env():
            code, out, _ = run("--workspace", str(ws_path), "--mode", "public", "--write-state", "--cache", str(cache))
        self.assertEqual(code, 1)
        self.assertIn("FAIL", out)
        self.assertTrue(json.loads(cache.read_text())["breach"])
        state = self.r["mach"] / "state"
        self.assertTrue(json.loads((state / "check.json").read_text())["breach"])
        flag = json.loads((state / "breach.json").read_text())
        self.assertIn(str(self.r["ana"]), flag["folders"])
        # a clean public run never clears the flag
        self.write_ws(self.clean)
        with self.env():
            code, _, _ = run("--workspace", str(self.ws_path), "--mode", "public", "--write-state")
        self.assertEqual(code, 0)
        self.assertTrue((state / "breach.json").exists())

    def test_full_needs_elevation(self):
        with mock.patch.object(zones, "is_elevated", return_value=False):
            code, out, err = run("--workspace", str(self.ws_path), "--mode", "full")
        self.assertEqual((code, out), (2, ""))
        self.assertIn("elevated", err)
        with mock.patch.object(zones, "is_elevated", return_value=False), self.env():
            code, out, _ = run("--workspace", str(self.ws_path), "--json")  # default mode: public
        self.assertEqual(json.loads(out)["mode"], "public")

    @unittest.skipUnless(os.name == "nt", "full mode needs Windows")
    def test_full_clean_clears_the_flag(self):
        state = self.r["mach"] / "state"
        state.mkdir()
        (state / "breach.json").write_text("{}")
        with (
            mock.patch.object(zones, "is_elevated", return_value=True),
            mock.patch.object(zones, "full_run", return_value=([zones.Check("owner", "x", "a", "a", True)], [])),
            mock.patch.object(
                zones, "wall_filter_checks", return_value=[zones.Check("port wall", "x", "a", "a", True)]
            ),
            mock.patch.object(
                zones, "unlisted_listener_checks", return_value=[zones.Check("unlisted listener", "x", "a", "a", True)]
            ),
            mock.patch.object(
                zones, "legacy_checks", return_value=([zones.Check("legacy scan", "x", "a", "a", True)], [])
            ),
        ):
            code, out, _ = run("--workspace", str(self.ws_path), "--write-state", "--json")
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out)["mode"], "full")
        self.assertIn("unlisted listener", [c["what"] for c in json.loads(out)["checks"]])
        self.assertFalse((state / "breach.json").exists())

    def test_bad_workspace(self):
        code, out, err = run("--workspace", str(self.tmp / "absent.json"))
        self.assertEqual((code, out), (2, ""))
        self.assertIn("cannot read", err)
        self.ws_path.write_text("[]")
        self.assertEqual(run("--workspace", str(self.ws_path))[0], 2)


class Listeners(unittest.TestCase):
    NETSTAT = """
Active Connections

  Proto  Local Address          Foreign Address        State           PID
  TCP    0.0.0.0:135            0.0.0.0:0              LISTENING       1980
  TCP    127.0.0.1:4101         0.0.0.0:0              ABHOEREN        4242
  TCP    10.0.0.5:4102          0.0.0.0:0              LISTENING       4243
  TCP    127.0.0.1:50000        127.0.0.1:4101         ESTABLISHED     77
  TCP    [::]:4102              [::]:0                 LISTENING       4243
  TCP    [::1]:4101             [::]:0                 LISTENING       4242
"""

    def test_parse(self):
        got = zones.parse_listeners(self.NETSTAT)
        self.assertIn(("127.0.0.1", 4101, 4242), got)
        self.assertIn(("[::]", 4102, 4243), got)
        self.assertNotIn(50000, [port for _, port, _ in got])  # a connection, not a listener

    def test_checks(self):
        ws = mock.Mock()
        ws.ports = [{"port": 4101, "name": "tool A"}, {"port": 4102, "name": "tool B"}, {"port": 4103}]
        checks = zones.listener_checks(ws, lambda: zones.parse_listeners(self.NETSTAT))
        by = {c.path: c for c in checks}
        self.assertTrue(by["port 4101 (tool A)"].ok)
        self.assertEqual(by["port 4101 (tool A)"].got, "listening on 127.0.0.1, [::1]")
        self.assertFalse(by["port 4102 (tool B)"].ok)
        self.assertTrue(by["port 4102 (tool B)"].widens)
        self.assertEqual(by["port 4103 (analyst port)"].got, "not listening")


class UnlistedListeners(unittest.TestCase):
    """Made-up accounts, SIDs, ports and processes only."""

    SIDS: ClassVar = {"ana": "S-1-5-21-1-2-3-1001", "rev": "S-1-5-21-1-2-3-1002"}
    OWNERS: ClassVar = {
        10: ("S-1-5-21-1-2-3-1001", "tool-a.exe"),
        11: ("S-1-5-21-1-2-3-1002", "tool-b.exe"),
        12: ("S-1-5-21-1-2-3-1009", "other.exe"),  # an account outside the analyst side
        13: (None, None),  # a process that could not be opened
    }

    def ws(self, exceptions=()):
        ws = mock.Mock()
        ws.ports = [{"port": 4101}]
        ws.listener_exceptions = [{"port": p, "why": "made up"} for p in exceptions]
        ws.analyst_side_accounts.return_value = {"ana", "rev"}
        return ws

    def run_checks(self, listening, ws=None):
        return zones.unlisted_listener_checks(
            ws or self.ws(), lambda name: self.SIDS[name].lower(), lambda: listening, self.OWNERS.get
        )

    def test_listed_and_foreign_listeners_pass(self):
        checks = self.run_checks([("127.0.0.1", 4101, 10), ("0.0.0.0", 5000, 12), ("127.0.0.1", 5001, 13)])
        self.assertEqual([(c.ok, c.got) for c in checks], [(True, "none unlisted")])

    def test_unlisted_port_is_a_breach(self):
        listening = [("127.0.0.1", 5100, 10), ("[::1]", 5100, 10), ("127.0.0.1", 5200, 11), ("127.0.0.1", 4101, 10)]
        checks = self.run_checks(listening)
        self.assertEqual([c.path for c in checks], ["port 5100 (ana, tool-a.exe)", "port 5200 (rev, tool-b.exe)"])
        self.assertTrue(all(not c.ok and c.widens for c in checks))
        self.assertEqual(checks[0].got, "listening on 127.0.0.1, [::1]")

    def test_exception_passes(self):
        checks = self.run_checks([("127.0.0.1", 5100, 10)], self.ws(exceptions=[5100]))
        self.assertTrue(checks[0].ok)

    def test_owner_looked_up_once_per_process(self):
        seen = []

        def owner(pid):
            seen.append(pid)
            return self.OWNERS[pid]

        listening = [("127.0.0.1", 5100, 10), ("[::1]", 5100, 10), ("127.0.0.1", 5101, 10)]
        zones.unlisted_listener_checks(self.ws(), lambda n: self.SIDS[n], lambda: listening, owner)
        self.assertEqual(seen, [10])

    def test_walled_program_passes_only_on_loopback_while_walled(self):
        owners = {20: ("S-1-5-21-1-2-3-1001", "C:\\Made\\Up\\Claude.exe"), 21: ("S-1-5-21-1-2-3-1001", "C:\\x\\t.exe")}
        walled = {"s-1-5-21-1-2-3-1001": "c:\\made\\up\\claude.exe"}
        listening = [("127.0.0.1", 5300, 20), ("[::1]", 5300, 20), ("127.0.0.1", 5301, 21)]

        def run(listen, w):
            return zones.unlisted_listener_checks(self.ws(), lambda n: self.SIDS[n], lambda: listen, owners.get, w)

        checks = run(listening, walled)
        self.assertEqual(
            [(c.what, c.path, c.ok) for c in checks],
            [
                ("walled listener", "port 5300 (ana, Claude.exe)", True),
                ("unlisted listener", "port 5301 (ana, t.exe)", False),
            ],
        )
        self.assertIn("accepts no loopback connection", checks[0].got)
        self.assertTrue(all(not c.ok and c.widens for c in run(listening[:1], {})))
        wide = run([("0.0.0.0", 5300, 20)], walled)
        self.assertEqual([(c.what, c.ok) for c in wide], [("unlisted listener", False)])
        (c,) = [c for c in run(listening[:1], walled) if c.what == "unlisted listener"]
        self.assertEqual((c.ok, c.got), (True, "none unlisted"))

    def test_program_specs_and_walled(self):
        ws = mock.Mock()
        ws.analyst_side_accounts.return_value = {"ana", "rev", "tst"}
        ws.claude_program.side_effect = lambda a: None if a == "tst" else f"C:\\Homes\\{a}\\tool.exe"
        apps = {"C:\\Homes\\ana\\tool.exe": "\\device\\v\\homes\\ana\\tool.exe"}  # rev's program is not there
        programs = zones.program_specs(ws, lambda n: self.SIDS[n].lower(), apps.get)
        self.assertEqual(
            [(a, sid, path) for a, sid, path, _ in programs], [("ana", self.SIDS["ana"], "C:\\Homes\\ana\\tool.exe")]
        )
        specs = programs[0][3]
        self.assertEqual(zones.walled_programs(programs, specs), {self.SIDS["ana"]: "C:\\Homes\\ana\\tool.exe"})
        self.assertEqual(zones.walled_programs(programs, specs[:1]), {})

    def test_unreadable_is_a_failure_not_a_breach(self):
        def boom():
            raise OSError("no netstat")

        (c,) = zones.unlisted_listener_checks(self.ws(), lambda n: self.SIDS[n], boom, self.OWNERS.get)
        self.assertFalse(c.ok)
        self.assertFalse(c.widens)

    @unittest.skipUnless(os.name == "nt", "Windows only")
    def test_own_process_owner(self):
        sid, program = zones.process_owner(os.getpid())
        self.assertEqual(sid, zones.sid_of(zones.current_account()))
        self.assertEqual(Path(program).resolve(), Path(sys.executable).resolve())
        self.assertEqual(zones.process_owner(None), (None, None))


if __name__ == "__main__":
    unittest.main()
