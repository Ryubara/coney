"""Tests for `tether check`, `write` and `show`. Made-up zones and settings only."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from coney_tools import tether

COMMIT = "0123456789abcdef0123456789abcdef01234567"


class FakeWs:
    def __init__(self, data, paths=None):
        self.data = data
        self.paths = paths or {}

    def where(self, key):
        return self.paths[key]

    def resolve(self, value, where=""):
        root, _, rest = value.partition(":")
        return Path("/roots", root, *rest.replace("\\", "/").split("/")) if rest else Path("/roots", root)


def workspace():
    return {
        "zones": {"open": {"folder": "pub:repo"}, "closed": {"folder": "ana:work"}},
        "roles": {
            "lead": {"zone": "open", "notes": "pub:repo\\HANDOFF.md"},
            "worker": {"zone": "closed", "notes": "ana:work\\notes\\HANDOFF.md"},
            "helper": {"zone": "closed"},
        },
        "mcp_allow": {"open": ["tether"], "closed": ["other", "tether"]},
        "tether": {
            "commit": COMMIT,
            "github_zones": ["open"],
            "base": {
                "network": False,
                "plainOutput": True,
                "fleet": {"retireContextTokens": 500000, "bigContextTokens": 60000},
                "watch": {"pollers": [{"id": "agents", "kind": "agents"}]},
                "cacheGuard": {"auto": True, "registerTools": False},
                "checkpoint": {"enabled": True, "contextTokens": 300000},
            },
            "zones": {
                "open": {
                    "network": True,
                    "plainOutput": False,
                    "cacheGuard": {"registerTools": True},
                    "watch": {"pollers": [{"id": "prs", "kind": "prs"}]},
                    "checkpoint": {"handoffFile": "HANDOFF.md"},
                },
                "closed": {"fleet": {"retireContextTokens": 300000}, "checkpoint": {"handoffFile": "notes/HANDOFF.md"}},
            },
            "container": {"checkpoint": {"enabled": False}},
            "checkpoint_exempt": {"container": "headless"},
        },
    }


class Render(unittest.TestCase):
    def test_sections_merge_field_by_field_and_lock(self):
        cfg = tether.render(FakeWs(workspace()), "closed")
        self.assertEqual(cfg["fleet"], {"retireContextTokens": 300000, "bigContextTokens": 60000})
        self.assertIs(cfg["locked"], True)
        self.assertIs(cfg["network"], False)

    def test_a_zone_may_replace_a_list_and_a_value(self):
        cfg = tether.render(FakeWs(workspace()), "open")
        self.assertEqual(cfg["watch"]["pollers"], [{"id": "prs", "kind": "prs"}])
        self.assertIs(cfg["network"], True)

    def test_the_container_config(self):
        cfg = tether.render(FakeWs(workspace()), tether.CONTAINER)
        self.assertEqual(cfg["checkpoint"], {"enabled": False, "contextTokens": 300000})
        self.assertIs(cfg["plainOutput"], True)

    def test_the_base_is_never_changed(self):
        data = workspace()
        tether.render(FakeWs(data), "closed")
        self.assertEqual(data["tether"]["base"]["fleet"]["retireContextTokens"], 500000)

    def test_unknown_zone_and_missing_table(self):
        with self.assertRaises(tether.TetherError):
            tether.render(FakeWs(workspace()), "nowhere")
        with self.assertRaises(tether.TetherError):
            tether.table(FakeWs({"zones": {}}))


class Check(unittest.TestCase):
    def test_a_good_table_passes(self):
        self.assertEqual(tether.check(FakeWs(workspace())), [])

    def test_every_part_stays_on_and_the_handoff_is_the_roles_notes(self):
        cases = [
            (lambda t: t["zones"]["closed"].update(fleet={"enabled": False}), "closed: fleet must be on"),
            (lambda t: t["zones"]["closed"].update(watch={"enabled": False}), "closed: watch must be on"),
            (lambda t: t["zones"]["open"].update(cacheGuard={"auto": False}), "open: the cache guard must act"),
            (lambda t: t["zones"]["closed"].update(checkpoint={"enabled": False}), "closed: checkpoint must be on"),
            (lambda t: t["zones"]["closed"].update(checkpoint={"handoffFile": "elsewhere.md"}), "closed: checkpoint."),
            (lambda t: t["zones"]["open"]["checkpoint"].pop("handoffFile"), "open: checkpoint.handoffFile must be"),
            (lambda t: t.pop("checkpoint_exempt"), "container: checkpoint must be on"),
            (lambda t: t["checkpoint_exempt"].update(closed="no notes"), "closed: checkpoint_exempt but its"),
            (lambda t: t["checkpoint_exempt"].update(container=""), "checkpoint_exempt must map"),
            (lambda t: t["checkpoint_exempt"].update(nowhere="x"), "checkpoint_exempt must map"),
        ]
        for change, want in cases:
            data = workspace()
            change(data["tether"])
            with self.subTest(want=want):
                self.assertIn(want, "\n".join(tether.check(FakeWs(data))))
        data = workspace()  # a handoff beside the zone's folder, as Tether resolves '..' from the session's folder
        data["roles"]["worker"]["notes"] = "ana:notes-worker\\HANDOFF.md"
        data["tether"]["zones"]["closed"]["checkpoint"] = {"handoffFile": "../notes-worker/HANDOFF.md"}
        self.assertEqual(tether.check(FakeWs(data)), [])
        data = workspace()  # an exempt zone without notes passes with its checkpoint off
        data["tether"]["zones"]["closed"]["checkpoint"] = {"enabled": False}
        data["tether"]["checkpoint_exempt"]["closed"] = "no notes folder yet"
        self.assertEqual(tether.check(FakeWs(data)), [])

    def test_each_gap_is_named(self):
        data = workspace()
        t = data["tether"]
        t["commit"] = "main"
        t["zones"]["closed"].update(network=True, plainOutput=False, watch={"pollers": [{"id": "p", "kind": "prs"}]})
        t["zones"]["closed"]["typo"] = 1
        t["container"]["network"] = True
        t["container"]["cacheGuard"] = {"registerTools": True}
        t["zones"]["closed"]["cacheGuard"] = {"auto": False}
        del t["base"]["cacheGuard"]["registerTools"]
        data["mcp_allow"]["closed"] = ["other"]
        text = "\n".join(tether.check(FakeWs(data)))
        self.assertIn("commit must be a full", text)
        self.assertIn("closed: network must be false", text)
        self.assertIn("closed: plainOutput must be true", text)
        self.assertIn("closed: a prs poller", text)
        self.assertIn("closed: unknown key 'typo'", text)
        self.assertIn("container: network must be false", text)
        # Tether registers its tools unless told not to: a missing value fails as an explicit true does
        self.assertIn("closed: cacheGuard.registerTools must be false", text)
        self.assertIn("container: cacheGuard.registerTools must be false", text)
        self.assertNotIn("open: cacheGuard", text)
        self.assertIn("closed: mcp_allow must list tether", text)

    def test_github_zones_must_be_zones(self):
        data = workspace()
        data["tether"]["github_zones"] = ["elsewhere"]
        problems = tether.check(FakeWs(data))
        self.assertIn("github_zones must list zones of the workspace", problems)
        # with no GitHub zone, the open zone's network is refused too
        self.assertTrue(any(p.startswith("open: network must be false") for p in problems))

    def test_a_zone_without_settings(self):
        data = workspace()
        data["zones"]["third"] = {}
        self.assertTrue(any("no settings for 'third'" in p for p in tether.check(FakeWs(data))))


class Installed(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        self.mod = self.dir / "mod"
        (self.mod / ".claude-plugin").mkdir(parents=True)
        (self.mod / ".claude-plugin" / "plugin.json").write_text("{}", encoding="utf-8")
        (self.mod / "VERSION").write_text(COMMIT + "\n", encoding="ascii")
        self.ws = FakeWs(workspace(), {"tether_configs": self.dir / "configs", "tether_mod": self.mod})

    def test_written_configs_match(self):
        written = tether.write(self.ws, self.dir / "configs")
        self.assertEqual(sorted(p.name for p in written), ["closed.json", "container.json", "open.json"])
        self.assertEqual(tether.check(self.ws, installed=True), [])

    def test_drift_and_a_missing_file_and_another_commit(self):
        tether.write(self.ws, self.dir / "configs")
        path = self.dir / "configs" / "closed.json"
        cfg = json.loads(path.read_text(encoding="utf-8"))
        cfg["network"] = True
        path.write_text(json.dumps(cfg), encoding="utf-8")
        (self.dir / "configs" / "open.json").unlink()
        (self.mod / "VERSION").write_text("f" * 40, encoding="ascii")
        text = "\n".join(tether.check(self.ws, installed=True))
        self.assertIn("closed.json differs from the workspace", text)
        self.assertIn("open: no readable config", text)
        self.assertIn("installed Tether is ffff", text)

    def test_not_installed(self):
        tether.write(self.ws, self.dir / "configs")
        (self.mod / ".claude-plugin" / "plugin.json").unlink()
        self.assertTrue(any("Tether is not installed" in p for p in tether.check(self.ws, installed=True)))


if __name__ == "__main__":
    unittest.main()
