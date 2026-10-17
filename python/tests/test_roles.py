"""Tests for `inbox` and `roles check`. Made-up roles, folders and inboxes only."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from coney_tools import roles
from coney_tools.workspace import WorkspaceError


class FakeWs:
    """Each zone's folder is the root named after it ('public:', 'analyst:')."""

    def __init__(self, table, briefs_dir, zones=("public", "analyst")):
        self.data = {"roles": {"comment": "x", **table}, "zones": {z: {"folder": f"{z}:"} for z in zones}}
        self.briefs = Path(briefs_dir)

    def where(self, key):
        assert key == "roles"
        return self.briefs

    def split(self, spec, where="path"):
        root, sep, rel = spec.partition(":")
        if not sep or root not in self.data["zones"]:
            raise WorkspaceError(f"{where}: unknown role '{root}'")
        return root, rel

    def resolve(self, spec, where="path"):
        root, rel = self.split(spec, where)
        return Path("/roots", root, rel)


def doc(*action):
    return {"role": "x", "pages": [], "counts": {}, "action": list(action)}


class Roles(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.table = {
            "lead": {"zone": "public", "loop_minutes": 10, "active": True},
            "helper": {"zone": "analyst", "loop_minutes": 15, "active": False},
            "worker": {"zone": "container", "loop_minutes": None, "active": False},
        }
        for r in self.table:
            (self.dir / f"{r}.md").write_text("# brief\n", encoding="utf-8")
        self.providers = {"lead": [("pages", lambda ws: doc("p-1", "p-2"))]}

    def ws(self, **over):
        return FakeWs({**self.table, **over}, self.dir)

    def test_a_wired_table_passes(self):
        self.assertEqual(roles.check(self.ws(), self.providers), [])

    def test_notes_inside_the_roles_own_zone_pass(self):
        ws = self.ws(lead={"zone": "public", "notes": "public:notes/HANDOFF.md", "loop_minutes": 10, "active": True})
        self.assertEqual(roles.check(ws, self.providers), [])

    def test_notes_of_a_fenced_zone_lie_inside_its_fence(self):
        helper = {"zone": "analyst", "notes": "analyst:notes-helper/HANDOFF.md", "loop_minutes": 15, "active": False}
        ws = self.ws(helper=helper)
        ws.data["write_fence"] = {"analyst": ["analyst:gate"]}
        self.assertIn(
            "helper: notes 'analyst:notes-helper/HANDOFF.md' lie outside its zone's write fence",
            roles.check(ws, self.providers),
        )
        ws.data["write_fence"]["analyst"].append("analyst:notes-helper")
        self.assertEqual(roles.check(ws, self.providers), [])

    def test_notes_elsewhere_are_refused(self):
        (self.dir / "odd.md").write_text("# brief\n", encoding="utf-8")
        problems = roles.check(
            self.ws(
                lead={"zone": "public", "notes": "analyst:notes/HANDOFF.md", "loop_minutes": 10, "active": True},
                helper={"zone": "analyst", "notes": "nowhere:HANDOFF.md", "loop_minutes": 15, "active": False},
                worker={"zone": "container", "notes": "public:x.md", "loop_minutes": None, "active": False},
                odd={"zone": "public", "notes": 7, "loop_minutes": 5, "active": False},
            ),
            self.providers,
        )
        text = "\n".join(problems)
        self.assertIn("lead: notes 'analyst:notes/HANDOFF.md' lie outside its zone's root 'public'", text)
        self.assertIn("helper: notes 'nowhere:HANDOFF.md'", text)
        self.assertIn("worker: a container role keeps no notes", text)
        self.assertIn("odd: notes must be a workspace path", text)

    def test_each_gap_is_named(self):
        (self.dir / "helper.md").unlink()
        problems = roles.check(
            self.ws(
                lead={"zone": "public", "loop_minutes": 10, "active": True},
                stray={"zone": "nowhere", "loop_minutes": 0, "active": "yes"},
                looper={"zone": "container", "loop_minutes": 5, "active": False},
                orphan={"zone": "analyst", "loop_minutes": 5, "active": True},
                waker={"zone": "container", "loop_minutes": None, "active": False, "idle_wake_minutes": 30},
                zero={"zone": "public", "loop_minutes": 5, "active": False, "idle_wake_minutes": 0},
                flag={"zone": "public", "loop_minutes": 5, "active": False, "idle_wake_minutes": True},
            ),
            self.providers,
        )
        text = "\n".join(problems)
        self.assertIn("helper: no brief", text)
        self.assertIn("stray: zone 'nowhere'", text)
        self.assertIn("stray: 'active' must be true or false", text)
        self.assertIn("stray: loop_minutes", text)
        self.assertIn("looper: a container role never loops", text)
        self.assertIn("orphan: active, but no inbox serves it", text)
        for role in ("waker", "zero", "flag"):
            self.assertIn(f"{role}: idle_wake_minutes", text)

    def test_idle_wake_is_optional(self):
        lead = {"zone": "public", "loop_minutes": 10, "active": True, "idle_wake_minutes": 30}
        self.assertEqual(roles.check(self.ws(lead=lead), self.providers), [])
        lead["idle_wake_minutes"] = None
        self.assertEqual(roles.check(self.ws(lead=lead), self.providers), [])

    def test_inbox_gathers_every_station(self):
        got = roles.inbox(self.ws(), "lead", self.providers)
        self.assertTrue(got["active"])
        self.assertEqual(got["action"], ["pages:p-1", "pages:p-2"])

    def test_inactive_role_has_nothing_to_do(self):
        got = roles.inbox(self.ws(), "helper", self.providers)
        self.assertEqual((got["active"], got["action"]), (False, []))
        self.assertIn("inactive", roles.inbox_lines(got)[0])

    def test_unknown_role_and_missing_table(self):
        with self.assertRaises(roles.RoleError):
            roles.inbox(self.ws(), "nobody", self.providers)
        ws = self.ws()
        ws.data.pop("roles")
        with self.assertRaises(roles.RoleError):
            roles.table(ws)

    def test_every_known_provider_serves_a_role_of_the_real_table(self):
        # the page-gate inboxes and the code gate's exist for the roles the workspace names
        self.assertEqual(
            set(roles.PROVIDERS),
            {"engine-coordinator", "research-coordinator", "page-reviewer-1", "page-reviewer-2", "pr-analyst"},
        )

    def dispatched(self, **over):
        (self.dir / "aide.md").write_text("# brief\n", encoding="utf-8")
        self.table["helper"]["active"] = True
        aide = {"zone": "analyst", "dispatched_by": "helper", "loop_minutes": None, "active": True, **over}
        providers = {**self.providers, "helper": [("pages", lambda ws: doc("h-1"))],
                     "aide": [("branches", lambda ws: doc("b-1"))]}  # fmt: skip
        return self.ws(aide=aide), providers

    def test_a_dispatched_role_fills_its_dispatchers_inbox(self):
        ws, providers = self.dispatched()
        self.assertEqual(roles.check(ws, providers), [])
        self.assertEqual(roles.inbox(ws, "helper", providers)["action"], ["pages:h-1", "branches:b-1"])
        self.assertEqual(roles.inbox(ws, "aide", providers)["action"], ["branches:b-1"])
        self.assertEqual(roles.looping_role(ws, "analyst"), "helper")  # the dispatcher still wakes alone
        ws.data["roles"]["aide"]["active"] = False
        self.assertEqual(roles.inbox(ws, "helper", providers)["action"], ["pages:h-1"])

    def test_a_dispatched_role_is_checked(self):
        for over, want in (
            ({"dispatched_by": "lead"}, "must name a role of its own zone"),
            ({"dispatched_by": "nobody"}, "must name a role of its own zone"),
            ({"loop_minutes": 5}, "never loops or wakes"),
            ({"idle_wake_minutes": 30}, "never loops or wakes"),
        ):
            with self.subTest(over):
                ws, providers = self.dispatched(**over)
                self.assertIn(want, "\n".join(roles.check(ws, providers)))
        ws, providers = self.dispatched()
        ws.data["roles"]["helper"]["active"] = False
        self.assertIn("aide: active, but its dispatcher 'helper' is not", roles.check(ws, providers))

    def test_inbox_lines(self):
        station = {"role": "x", "pages": [{"id": "p-1", "state": "new", "verdict": "abc"}], "counts": {"new": 1}}
        station["action"] = ["p-1"]
        with mock.patch.object(roles.research, "status_lines", return_value=["new p-1 abc", "1 pages"]):
            lines = roles.inbox_lines({"role": "lead", "active": True, "stations": {"pages": station}, "action": ["x"]})
        self.assertEqual(lines, ["pages: new p-1 abc", "pages: 1 pages", "1 need lead"])


class ZoneStatus(Roles):
    def ws_for(self, **paths):
        ws = self.ws()
        ws.data["zones"] = {"public": {"account": "pub-x"}, "analyst": {"account": "ana-x"}}
        where = {"roles": self.dir, **paths}
        ws.where = lambda key: where[key]
        return ws

    def test_role_inbox_and_walls(self):
        checks = [{"ok": True}, {"ok": False}, {"ok": False, "legacy": True}]  # a legacy copy is not a wall
        doc = {"time": "T", "checks": checks, "legacy": ["C:\\zone\\notes"]}
        (self.dir / "check.json").write_text(json.dumps(doc))
        ws = self.ws_for(check_result=self.dir / "check.json", breach_flag=self.dir / "none.json")
        with mock.patch.object(roles, "pages_in_main", return_value=7):
            doc = roles.zone_status(ws, "PUB-X", self.providers, now=0)
        self.assertEqual((doc["zone"], doc["role"], doc["pages_in_main"]), ("public", "lead", 7))
        self.assertIsNone(doc["idle_wake_minutes"])
        self.assertEqual(doc["inbox"]["action"], ["pages:p-1", "pages:p-2"])
        self.assertEqual(
            doc["walls"],
            {
                "checked": "T",
                "passed": 1,
                "checks": 2,
                "breach": False,
                "breach_folders": [],
                "legacy_folders": ["C:\\zone\\notes"],
            },
        )

    def test_breach_flag_and_unknown_account(self):
        (self.dir / "flag.json").write_text(json.dumps({"folders": ["C:\\x"]}))
        ws = self.ws_for(check_result=self.dir / "missing.json", breach_flag=self.dir / "flag.json")
        doc = roles.zone_status(ws, "someone-else", self.providers)
        self.assertEqual((doc["zone"], doc["role"], doc["inbox"]), (None, None, None))
        self.assertIsNone(doc["idle_wake_minutes"])
        self.assertEqual((doc["walls"]["breach"], doc["walls"]["breach_folders"]), (True, ["C:\\x"]))

    def test_a_broken_inbox_is_an_error_not_empty(self):
        def boom(ws):
            raise OSError("cannot read the gate")

        ws = self.ws_for(check_result=self.dir / "m", breach_flag=self.dir / "n")
        ws.data["roles"]["helper"]["active"] = True
        doc = roles.zone_status(ws, "ana-x", {"helper": [("pages", boom)]})
        self.assertEqual(doc["inbox"], None)
        self.assertIn("cannot read the gate", doc["error"])

    def test_idle_wake_minutes_reaches_the_mod(self):
        ws = self.ws_for(check_result=self.dir / "m", breach_flag=self.dir / "n")
        ws.data["roles"]["lead"]["idle_wake_minutes"] = 30
        with mock.patch.object(roles, "pages_in_main", return_value=0):
            doc = roles.zone_status(ws, "pub-x", self.providers)
        self.assertEqual(doc["idle_wake_minutes"], 30)

    def test_more_than_one_looping_role_is_started_by_hand(self):
        ws = self.ws_for()
        ws.data["roles"]["second"] = {"zone": "public", "loop_minutes": 5, "active": True}
        self.assertIsNone(roles.looping_role(ws, "public"))


if __name__ == "__main__":
    unittest.main()
