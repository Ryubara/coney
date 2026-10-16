"""Tests for `research status`: each role's inbox, on synthetic gate folders with made-up verdicts and empty signature
files (status never checks a signature), and a temp git repository for the public role.

Every page holds a word that appears only in page text; no output may carry it.
"""

import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import research, signing
from coney_tools.cleanroom import scan
from coney_tools.cli import main

WORD = "Quorbalnix"  # only ever in page text
PAGE = f"# Getting up\n\nA knocked-down Human gets up after 2.5 s. {WORD} names this rule.\n"
GIT = scan.find_program("git")


def fake_verdict(page_id, sections=("keep",), images=()):
    """A verdict file in canonical form with made-up hashes."""
    return research.canonical(
        {
            "schema": research.SCHEMA,
            "page": page_id,
            "page_sha256": "1" * 64,
            "checker_sha256": "2" * 64,
            "sections": [
                {"id": f"s{n}", "sha256": "3" * 64, "verdict": v, "kind": "fact"} for n, v in enumerate(sections)
            ],
            "images": [
                {"path": f"img/{n}.png", "sha256": "4" * 64, "capture_sha256": "5" * 64, "verdict": v}
                for n, v in enumerate(images)
            ],
        }
    )


class Fixture(unittest.TestCase):
    """A gate, an outbox and two workspaces in a temp folder."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.ana, self.pub = self.tmp / "ana", self.tmp / "pub"
        self.gate = self.ana / "gate"
        self.gate.mkdir(parents=True)
        self.from_ana = self.pub / "exchange" / "from-ana"
        self.outbox = self.from_ana / "pages"
        self.outbox.mkdir(parents=True)
        self.ws = self.workspace("ws.json", "public:exchange\\from-ana")
        self.no_outbox_ws = self.workspace("ws-no-outbox.json", "public:nowhere\\from-ana")

    def tearDown(self):
        self._tmp.cleanup()

    def workspace(self, name, from_analyst):
        zones = self.tmp / "zones.json"
        zones.write_text(
            json.dumps(
                {
                    "roots": [{"path": str(self.pub), "role": "public"}, {"path": str(self.ana), "role": "analyst"}],
                    "accounts": [],
                }
            ),
            encoding="utf-8",
        )
        ws = self.tmp / name
        ws.write_text(
            json.dumps(
                {
                    "version": 1,
                    "zones_file": "zones.json",
                    "paths": {"repo": "public:repo", "gate": "analyst:gate", "from_analyst": from_analyst},
                    "zones": {},
                    "analyst_ports": [],
                    "check": {"cache_minutes": 5, "schedule_minutes": 5},
                }
            ),
            encoding="utf-8",
        )
        return ws

    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = main([str(a) for a in argv])
        self.assertNotIn(WORD.lower(), (out.getvalue() + err.getvalue()).lower())
        return code, out.getvalue(), err.getvalue()

    def status(self, role, ws=None, *more):
        code, out, err = self.run_cli(
            "research", "status", "--role", role, "--json", "--workspace", ws or self.ws, *more
        )
        self.assertIn(code, (0, 1), err)
        doc = json.loads(out)
        self.assertEqual(code, 1 if doc["action"] else 0)
        return doc

    def states(self, doc):
        return {p["id"]: p["state"] for p in doc["pages"]}

    def page(self, page_id, verdict=None, sigs=(), declines=(), page=True):
        folder = self.gate / page_id
        folder.mkdir()
        if page:
            (folder / "page.md").write_text(PAGE, encoding="utf-8")
        if verdict is not None:
            (folder / "verdict.json").write_bytes(verdict)
        for p in sigs:
            (folder / f"verdict.json.{p}.sig").write_bytes(b"")
        for p in declines:
            (folder / f"review.{p}.txt").write_text(f"declined: {WORD}\n", encoding="utf-8")
        return folder


class Status(Fixture):
    def gate_with_every_state(self):
        self.page("p-new")
        self.page("p-one", fake_verdict("p-one"), ["page-reviewer-1"])
        self.page("p-one-by-2", fake_verdict("p-one-by-2"), ["page-reviewer-2"])
        self.page("p-declined", fake_verdict("p-declined"), ["page-reviewer-1"], ["page-reviewer-2"])
        self.page("p-rejected", fake_verdict("p-rejected", ("keep", "reject")), ["page-reviewer-1"])
        self.page("p-image-rejected", fake_verdict("p-image-rejected", images=("reject",)), ["page-reviewer-1"])
        self.page("p-bad-json", b"{not json " + WORD.encode(), ["page-reviewer-1"])
        self.page("p-other-id", fake_verdict("someone-else"), ["page-reviewer-1"])
        self.page("p-not-canonical", fake_verdict("p-not-canonical") + b"\n", ["page-reviewer-1"])
        self.page("p-unsigned", fake_verdict("p-unsigned"))
        self.page("p-three", fake_verdict("p-three"), ["page-reviewer-1", "page-reviewer-2", "page-reviewer-3"])
        both = ["page-reviewer-1", "page-reviewer-2"]
        self.page("p-ready", fake_verdict("p-ready"), both)
        v = fake_verdict("p-published")
        self.page("p-published", v, both)
        (self.outbox / f"p-published-{research.sha256(v)[:12]}").mkdir()
        # Left out: drills, folders without a page, files.
        self.page("gate-drill-1")
        self.page("gate-drill-2", fake_verdict("gate-drill-2"), both)
        self.page("p-no-page", page=False)
        (self.gate / "stray.txt").write_text("x", encoding="utf-8")
        self.page("Not_An_Id")

    # --- the research coordinator ----------------------------------------------------------------------------------

    def test_coordinator_states(self):
        self.gate_with_every_state()
        doc = self.status("research-coordinator")
        self.assertEqual(
            self.states(doc),
            {
                "Not_An_Id": "invalid-name",
                "p-bad-json": "invalid-verdict",
                "p-declined": "declined",
                "p-image-rejected": "rejected",
                "p-new": "awaiting-reviewer-1",
                "p-not-canonical": "invalid-verdict",
                "p-one": "awaiting-reviewer-2",
                "p-one-by-2": "invalid-signatures",  # reviewer 2 alone: in no reviewer's inbox
                "p-other-id": "invalid-verdict",
                "p-published": "published",
                "p-ready": "ready-to-publish",
                "p-rejected": "rejected",
                "p-three": "invalid-signatures",
                "p-unsigned": "invalid-signatures",
            },
        )
        self.assertEqual(
            doc["action"],
            [
                "p-bad-json",
                "p-declined",
                "p-image-rejected",
                "p-not-canonical",
                "p-one-by-2",
                "p-other-id",
                "p-ready",
                "p-rejected",
                "p-three",
                "p-unsigned",
            ],
        )
        self.assertEqual(doc["counts"]["rejected"], 2)
        self.assertEqual(doc["counts"]["invalid-verdict"], 3)
        self.assertEqual(sum(doc["counts"].values()), len(doc["pages"]))
        self.assertEqual(set(doc["counts"]), set(research.ROLES["research-coordinator"][0]))
        by_id = {p["id"]: p for p in doc["pages"]}
        self.assertIsNone(by_id["p-new"]["verdict"])
        sha = research.sha256(fake_verdict("p-ready"))[:12]
        self.assertEqual(by_id["p-ready"]["verdict"], sha)
        self.assertEqual(set(doc), {"role", "pages", "counts", "action"})
        self.assertEqual(doc["role"], "research-coordinator")

    def test_coordinator_text_and_exit(self):
        self.page("p-new")
        self.page("p-one", fake_verdict("p-one"), ["page-reviewer-1"])
        code, out, _ = self.run_cli("research", "status", "--role", "research-coordinator", "--workspace", self.ws)
        self.assertEqual(code, 0)
        sha = research.sha256(fake_verdict("p-one"))[:12]
        self.assertEqual(
            out.splitlines(),
            [
                "awaiting-reviewer-1 p-new",
                f"awaiting-reviewer-2 p-one {sha}",
                "2 pages: 1 awaiting-reviewer-1, 1 awaiting-reviewer-2; 0 need research-coordinator",
            ],
        )
        self.page("p-ready", fake_verdict("p-ready"), ["page-reviewer-1", "page-reviewer-2"])
        code, out, _ = self.run_cli("research", "status", "--role", "research-coordinator", "--workspace", self.ws)
        self.assertEqual(code, 1)
        self.assertIn("ready-to-publish p-ready", out)

    def test_crlf_verdict_hashes_as_publish_does(self):
        v = fake_verdict("p-crlf")
        self.page("p-crlf", v.replace(b"\n", b"\r\n"), ["page-reviewer-1", "page-reviewer-2"])
        (self.outbox / f"p-crlf-{research.sha256(v)[:12]}").mkdir()
        self.assertEqual(self.states(self.status("research-coordinator")), {"p-crlf": "published"})

    def test_empty_and_missing(self):
        self.outbox.rmdir()  # before the first publish there is no pages folder: an empty outbox
        doc = self.status("research-coordinator")
        self.assertEqual(doc["pages"], [])
        code, out, _ = self.run_cli("research", "status", "--role", "research-coordinator", "--workspace", self.ws)
        self.assertEqual((code, out), (0, "0 pages; 0 need research-coordinator\n"))
        for role in research.ROLES:
            if role == "public":
                continue
            code, _, err = self.run_cli("research", "status", "--role", role, "--workspace", self.no_outbox_ws)
            if role == "research-coordinator":
                self.assertEqual(code, 2)
                self.assertIn("no outbox folder", err)
            else:
                self.assertEqual(code, 0, err)
        self.gate.rmdir()
        for role in ("research-coordinator", "page-reviewer-1", "page-reviewer-2"):
            code, _, err = self.run_cli("research", "status", "--role", role, "--workspace", self.ws)
            self.assertEqual(code, 2)
            self.assertIn("no gate folder", err)

    def test_repo_only_for_public(self):
        for role in ("research-coordinator", "page-reviewer-1", "page-reviewer-2"):
            code, _, err = self.run_cli(
                "research", "status", "--role", role, "--workspace", self.ws, "--repo", self.tmp
            )
            self.assertEqual(code, 2)
            self.assertIn("--repo applies to --role public only", err)
        with self.assertRaises(SystemExit) as e, redirect_stderr(io.StringIO()):
            main(["research", "status", "--role", "nobody", "--workspace", str(self.ws)])
        self.assertEqual(e.exception.code, 2)

    # --- the page reviewers -----------------------------------------------------------------------------------------

    def test_reviewers_read_the_gate_only(self):
        self.gate_with_every_state()
        self.page("p-declined-by-1", fake_verdict("p-declined-by-1"), ["page-reviewer-1"], ["page-reviewer-1"])
        # The reviewers' workspace points at an outbox that does not exist: they never look there.
        doc = self.status("page-reviewer-1", self.no_outbox_ws)
        self.assertEqual(self.states(doc), {"p-new": "awaiting-reviewer-1"})
        self.assertEqual(doc["action"], ["p-new"])
        self.assertEqual(doc["counts"], {"awaiting-reviewer-1": 1})
        doc = self.status("page-reviewer-2", self.no_outbox_ws)
        self.assertEqual(self.states(doc), {"p-one": "awaiting-reviewer-2", "p-declined-by-1": "awaiting-reviewer-2"})
        self.assertEqual(doc["action"], ["p-declined-by-1", "p-one"])
        self.assertEqual(doc["pages"][1]["verdict"], research.sha256(fake_verdict("p-one"))[:12])

    def test_reviewers_with_nothing_to_do(self):
        self.page("p-ready", fake_verdict("p-ready"), ["page-reviewer-1", "page-reviewer-2"])
        for role in ("page-reviewer-1", "page-reviewer-2"):
            code, out, _ = self.run_cli("research", "status", "--role", role, "--workspace", self.no_outbox_ws)
            self.assertEqual(code, 0)
            self.assertEqual(out, f"0 pages; 0 need {role}\n")


@unittest.skipUnless(GIT, "needs git on PATH")
class PublicStatus(Fixture):
    """The public role: bundles in the outbox against main and page/<id> of a temp repository."""

    def setUp(self):
        super().setUp()
        self.repo = self.pub / "repo"
        self.repo.mkdir()
        self.git("init", "-q", "-b", "main")
        for k, val in (("user.name", "Test"), ("user.email", "test@example.invalid"), ("commit.gpgsign", "false")):
            self.git("config", k, val)
        (self.repo / "README").write_text("made-up repository\n", encoding="utf-8")
        self.git("add", "README")
        self.git("commit", "-q", "-m", "Start")

    def git(self, *args):
        r = subprocess.run(
            [GIT, "-C", str(self.repo), *args],
            capture_output=True,
            env=signing.git_env(),
            creationflags=scan.NO_WINDOW,
            check=True,
        )
        return r.stdout.decode().strip()

    def bundle(self, page_id, verdict):
        folder = self.outbox / f"{page_id}-{research.sha256(verdict)[:12]}"
        folder.mkdir()
        (folder / "verdict.json").write_bytes(verdict)
        (folder / "page.md").write_text(PAGE, encoding="utf-8")
        return folder

    def commit_verdict(self, branch, page_id, verdict):
        self.git("checkout", "-q", "-B", branch, "main")
        path = self.repo / "research" / "verdicts" / f"{page_id}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(verdict)
        self.git("add", "research")
        self.git("commit", "-q", "-m", f"Add {page_id}")
        self.git("checkout", "-q", "main")

    def test_public_states(self):
        for name in ("p-new", "p-imported", "p-merged", "p-old"):
            self.bundle(name, fake_verdict(name))
        stale = self.bundle("p-stale", fake_verdict("p-stale"))
        self.commit_verdict("page/p-imported", "p-imported", fake_verdict("p-imported"))
        self.commit_verdict("page/p-merged", "p-merged", fake_verdict("p-merged"))  # imported, then merged
        self.commit_verdict("main", "p-merged", fake_verdict("p-merged"))
        # An older verdict of p-old on main, and a bundle published after it: a new revision.
        self.commit_verdict("main", "p-old", fake_verdict("p-old", ("reject",)))
        # A newer verdict of p-stale on main, and a bundle published before it was committed: superseded.
        self.commit_verdict("main", "p-stale", fake_verdict("p-stale", ("reject",)))
        committed = int(self.git("log", "-1", "--format=%ct", "main"))
        os.utime(self.outbox / f"p-old-{research.sha256(fake_verdict('p-old'))[:12]}", (committed + 60, committed + 60))
        os.utime(stale, (committed - 60, committed - 60))
        # Left out: drills, publish's temporary folders, strays, a bundle without a verdict.
        self.bundle("gate-drill-1", fake_verdict("gate-drill-1"))
        (self.outbox / ".p-tmp-0123456789abcdef").mkdir()
        (self.outbox / "Not_An_Id-0123456789ab").mkdir()
        (self.outbox / "p-noverdict-0123456789ab").mkdir()
        (self.outbox / "p-file-0123456789ab").write_text("x", encoding="utf-8")
        branch = self.git("symbolic-ref", "--short", "HEAD")
        status = self.git("status", "--porcelain")
        doc = self.status("public", None, "--repo", self.repo)
        self.assertEqual(
            self.states(doc),
            {"p-new": "new", "p-imported": "imported", "p-merged": "merged", "p-old": "new", "p-stale": "superseded"},
        )
        self.assertEqual(doc["action"], ["p-imported", "p-new", "p-old"])
        self.assertEqual(
            doc["counts"],
            {"new": 2, "imported": 1, "merged": 1, "superseded": 1, "withdrawn": 0, "withdrawn-imported": 0},
        )
        self.assertEqual(doc["pages"][0]["verdict"], research.sha256(fake_verdict("p-imported"))[:12])
        self.assertEqual(
            (self.git("symbolic-ref", "--short", "HEAD"), self.git("status", "--porcelain")), (branch, status)
        )
        code, out, _ = self.run_cli(
            "research", "status", "--role", "public", "--workspace", self.ws, "--repo", self.repo
        )
        self.assertEqual(code, 1)
        self.assertIn(f"merged p-merged {research.sha256(fake_verdict('p-merged'))[:12]}", out.splitlines())
        self.assertEqual(out.splitlines()[-1], "5 pages: 2 new, 1 imported, 1 merged, 1 superseded; 3 need public")

    def test_public_reads_git_objects_not_the_working_tree(self):
        self.bundle("p-wt", fake_verdict("p-wt"))
        path = self.repo / "research" / "verdicts" / "p-wt.json"
        path.parent.mkdir(parents=True)
        path.write_bytes(fake_verdict("p-wt"))  # only in the working tree
        self.assertEqual(self.states(self.status("public", None, "--repo", self.repo)), {"p-wt": "new"})

    def test_public_all_merged_exits_zero(self):
        self.bundle("p-merged", fake_verdict("p-merged"))
        self.commit_verdict("main", "p-merged", fake_verdict("p-merged"))
        code, _, _ = self.run_cli("research", "status", "--role", "public", "--workspace", self.ws, "--repo", self.repo)
        self.assertEqual(code, 0)

    def test_public_cannot_run(self):
        code, _, err = self.run_cli(
            "research", "status", "--role", "public", "--workspace", self.no_outbox_ws, "--repo", self.repo
        )
        self.assertEqual(code, 2)
        self.assertIn("no outbox folder", err)
        not_a_repo = self.tmp / "plain"
        not_a_repo.mkdir()
        code, _, err = self.run_cli(
            "research", "status", "--role", "public", "--workspace", self.ws, "--repo", not_a_repo
        )
        self.assertEqual(code, 2)
        self.git("branch", "-m", "main", "trunk")
        code, _, err = self.run_cli(
            "research", "status", "--role", "public", "--workspace", self.ws, "--repo", self.repo
        )
        self.assertEqual(code, 2)
        self.assertIn("no branch main", err)


if __name__ == "__main__":
    unittest.main()
