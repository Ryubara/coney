"""Tests for research requests (`request file`, `request status`, the two `requests` stations): made-up questions,
pages, folders and repositories only."""

import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import needs, research, roles, signing
from coney_tools.cleanroom import scan
from coney_tools.cli import main

GIT = scan.find_program("git")


def question(qid="door-timer", page="door", topic="How long a door stays open", text="How many seconds pass?"):
    return {"id": qid, "page": page, "topic": topic, "question": text}


def needs_doc(*questions):
    return {"schema": needs.NEEDS, "questions": list(questions)}


def git(repo, *args):
    who = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@x", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@x"}
    r = subprocess.run([GIT, "-C", str(repo), *args], env=signing.git_env(**who), capture_output=True, check=True)
    return r.stdout.decode().strip()


class FakeWs:
    def __init__(self, root):
        self.paths = {k: root / k.replace("_", "-") for k in ("to_analyst", "from_analyst", "repo")}

    def where(self, key):
        return self.paths[key]


class Form(unittest.TestCase):
    def test_a_well_formed_needs_file(self):
        doc = needs_doc(question(), question("door-sound", None))
        self.assertEqual(needs.needs_problems(doc), [])
        self.assertEqual(needs.needs_problems(doc, pages={"door"}), [])
        self.assertEqual(needs.needs_problems(needs_doc()), [])

    def test_malformed_needs_files(self):
        cases = [
            ([], "not an object with exactly questions, schema"),
            ({**needs_doc(), "extra": 1}, "not an object with exactly"),
            ({"schema": "coney-needs/2", "questions": []}, "schema is not coney-needs/1"),
            ({"schema": needs.NEEDS, "questions": {}}, "questions is not a list"),
            (needs_doc({**question(), "why": "x"}), "questions[0]: not an object with exactly"),
            (needs_doc(question("Door")), "questions[0]: id is not a slug"),
            (needs_doc(question("d" * 49)), "id is not a slug"),
            (needs_doc(question(page="Door Page")), "page is not a page id or null"),
            (needs_doc(question(page="")), "page is not a page id or null"),
            (needs_doc(question(topic="t" * 81)), "topic is longer than 80 characters"),
            (needs_doc(question(topic="  ")), "topic is not text"),
            (needs_doc(question(text="q" * 601)), "question is longer than 600 characters"),
            (needs_doc(question(text="two\nlines")), "question is not one line of printable text"),
            (needs_doc(question(text="a\tb")), "question is not one line of printable text"),
            (needs_doc(question(text=7)), "question is not text"),
            (needs_doc(question(), question()), "an id is given to two questions"),
        ]
        for doc, want in cases:
            with self.subTest(want=want):
                self.assertTrue(any(want in p for p in needs.needs_problems(doc)), needs.needs_problems(doc))

    def test_limits_are_inclusive_and_text_is_never_echoed(self):
        self.assertEqual(needs.needs_problems(needs_doc(question(topic="t" * 80, text="q" * 600))), [])
        bad = needs.needs_problems(needs_doc(question(text="secret words\n")))
        self.assertFalse(any("secret" in p for p in bad))

    def test_a_page_must_exist_when_the_pages_are_known(self):
        self.assertIn("page names no research page", needs.needs_problems(needs_doc(question()), pages=set())[0])

    def test_parse_refuses_duplicate_keys_and_non_json(self):
        self.assertEqual(needs.parse(b'{"a": 1, "a": 2}')[1], ["not UTF-8 JSON with each key once"])
        self.assertEqual(needs.parse(b"\xff")[1], ["not UTF-8 JSON with each key once"])
        self.assertIn("larger than", needs.parse(b" " * (needs.MAX_FILE + 1))[1][0])

    def test_collect_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.assertEqual(needs.collect_line("impl-1", folder), "impl-1: no needs.json")
            (folder / "needs.json").write_text(json.dumps(needs_doc(question())), encoding="utf-8")
            self.assertEqual(needs.collect_line("impl-1", folder, {"door"}), "impl-1: needs: 1 questions")
            self.assertIn("malformed: questions[0]: page names", needs.collect_line("impl-1", folder, set()))
            (folder / "needs.json").write_text("[]", encoding="utf-8")
            self.assertEqual(needs.collect_line("impl-1", folder), "impl-1: needs: 0 questions")
            (folder / "needs.json").write_text('[{"page": "", "question": "q"}]', encoding="utf-8")
            self.assertIn("impl-1: needs.json malformed: not an object", needs.collect_line("impl-1", folder))

    def test_request_ids_follow_the_question(self):
        rid = needs.request_id(question())
        self.assertRegex(rid, r"^door-timer-[0-9a-f]{8}$")
        self.assertEqual(rid, needs.request_id(dict(reversed(list(question().items())))))
        self.assertNotEqual(rid, needs.request_id(question(text="How many frames pass?")))


@unittest.skipUnless(GIT, "needs git on PATH")
class Requests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.ws = FakeWs(self.root)
        for key in ("to_analyst", "from_analyst"):
            self.ws.where(key).mkdir()
        self.repo = self.ws.where("repo")
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        self.add_page("door")
        self.requests = self.ws.where("to_analyst") / needs.FOLDER
        self.answers = self.ws.where("from_analyst") / needs.FOLDER

    def add_page(self, page_id):
        folder = self.repo / "research" / "pages" / page_id
        folder.mkdir(parents=True)
        (folder / "page.md").write_text(f"# {page_id}\n", encoding="utf-8")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", page_id)

    def write_needs(self, *questions, raw=None):
        path = self.root / "needs.json"
        path.write_bytes(raw if raw is not None else json.dumps(needs_doc(*questions)).encode())
        return path

    def cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with (
            mock.patch("coney_tools.workspace.load", lambda arg=None: self.ws),
            redirect_stdout(out),
            redirect_stderr(err),
        ):
            code = main([str(a) for a in argv])
        return code, out.getvalue(), err.getvalue()

    def file_two(self):
        second = question("door-what", None, "The sound", "Which sound plays?")
        code, _out, err = self.cli("request", "file", self.write_needs(question(), second), "--task", "task-07.md")
        self.assertEqual(code, 0, err)
        return [needs.request_id(question()), needs.request_id(second)]

    def test_filing_writes_canonical_requests(self):
        rid, other = self.file_two()
        data = (self.requests / f"{rid}.json").read_bytes()
        doc = json.loads(data)
        self.assertEqual(data, research.canonical(doc))
        self.assertEqual(needs.request_problems(doc), [])
        want = (needs.REQUEST, rid, "task-07.md", "door")
        self.assertEqual((doc["schema"], doc["id"], doc["task"], doc["page"]), want)
        self.assertRegex(doc["filed"], needs.TIME)
        self.assertIsNone(json.loads((self.requests / f"{other}.json").read_bytes())["page"])

    def test_refiling_is_a_no_op_and_ids_choose(self):
        rid, _ = self.file_two()
        before = (self.requests / f"{rid}.json").read_bytes()
        code, out, _ = self.cli("request", "file", self.root / "needs.json", "--id", "door-timer")
        self.assertEqual(code, 0)
        self.assertIn(f"{rid}: filed before; left as it is", out)
        self.assertIn("filed 0 of 1 questions", out)
        self.assertEqual((self.requests / f"{rid}.json").read_bytes(), before)

    def test_only_the_chosen_ids_are_filed(self):
        path = self.write_needs(question(), question("door-sound", None))
        code, _out, _ = self.cli("request", "file", path, "--id", "door-sound")
        self.assertEqual(code, 0)
        self.assertEqual([p.name.rsplit("-", 1)[0] for p in self.requests.iterdir()], ["door-sound"])

    def test_refusals_file_nothing(self):
        for raw, argv, want in [
            (json.dumps(needs_doc(question(page="no-such-page"))).encode(), (), "page names no research page"),
            (b"[]", (), "not an object with exactly"),
            (json.dumps(needs_doc(question())).encode(), ("--id", "nope"), "--id nope: no such question"),
        ]:
            with self.subTest(want=want):
                code, out, _ = self.cli("request", "file", self.write_needs(raw=raw), *argv)
                self.assertEqual(code, 1)
                self.assertIn(want, out)
                self.assertFalse(self.requests.exists() and any(self.requests.iterdir()))

    def test_could_not_run(self):
        self.assertEqual(self.cli("request", "file", self.root / "missing.json")[0], 2)
        self.assertEqual(self.cli("request", "file", self.write_needs(question()), "--task", "../t.md")[0], 2)
        self.ws.where("to_analyst").rmdir()
        self.assertEqual(self.cli("request", "file", self.write_needs(question()))[0], 2)

    def test_status_moves_a_request_on(self):
        rid, _ = self.file_two()
        self.assertEqual(self.cli("request", "status", rid, "--state", "accepted")[0], 0)
        code, out, _ = self.cli("request", "status", rid, "--state", "accepted")
        self.assertEqual((code, out.strip()), (0, f"{rid}: accepted already; nothing written"))
        self.assertEqual(self.cli("request", "status", rid, "--state", "answered", "--page", "door")[0], 0)
        data = (self.answers / f"{rid}.answered.json").read_bytes()
        doc = json.loads(data)
        self.assertEqual(data, research.canonical(doc))
        self.assertEqual(needs.status_problems(doc), [])
        self.assertEqual(set(doc), needs.STATUS_KEYS)  # no free text: ids, a state, a page and a time
        self.assertEqual(needs.state_of(self.answers, rid), ("answered", "door"))
        code, out, _ = self.cli("request", "status", rid, "--state", "declined")
        self.assertEqual(code, 1)
        self.assertIn("it is answered; declined does not move it on", out)
        code, _, _ = self.cli("request", "status", rid, "--state", "answered", "--page", "other")
        self.assertEqual(code, 1)
        self.assertFalse((self.answers / f"{rid}.declined.json").exists())

    def test_status_refusals(self):
        rid, _ = self.file_two()
        code, out, _ = self.cli("request", "status", "door-timer-00000000", "--state", "accepted")
        self.assertEqual(code, 1)
        self.assertIn("no such request", out)
        self.assertEqual(self.cli("request", "status", "Not An Id", "--state", "accepted")[0], 1)
        self.assertEqual(self.cli("request", "status", rid, "--state", "answered")[0], 2)  # no --page
        self.assertEqual(self.cli("request", "status", rid, "--state", "declined", "--page", "door")[0], 2)
        self.assertEqual(self.cli("request", "status", rid, "--state", "answered", "--page", "Bad Page")[0], 2)
        self.assertFalse(self.answers.exists() and any(self.answers.iterdir()))

    def test_stations(self):
        rid, other = self.file_two()
        (self.requests / "stray.txt").write_bytes(b"x")
        doc = needs.analyst_status(self.ws)
        self.assertEqual(
            doc["pages"],
            [
                {"id": rid, "state": "new", "verdict": None},
                {"id": other, "state": "new", "verdict": None},
                {"id": "stray.txt", "state": "invalid", "verdict": None},
            ],
        )
        self.assertEqual(doc["action"], [rid, other])
        self.assertEqual(set(doc), {"role", "noun", "pages", "counts", "action"})
        self.assertEqual(list(doc["counts"]), list(needs.ANALYST_STATES))
        pub = needs.public_status(self.ws, self.repo)
        self.assertEqual([p["state"] for p in pub["pages"]], ["open", "open", "invalid"])
        self.assertEqual(pub["action"], [])

        self.cli("request", "status", rid, "--state", "accepted")
        self.cli("request", "status", other, "--state", "declined")
        doc = needs.analyst_status(self.ws)
        self.assertEqual([p["state"] for p in doc["pages"]], ["accepted", "declined", "invalid"])
        self.assertEqual(doc["action"], [rid])
        pub = needs.public_status(self.ws, self.repo)
        self.assertEqual([p["state"] for p in pub["pages"]], ["open", "declined", "invalid"])
        self.assertEqual(pub["action"], [other])

        self.cli("request", "status", rid, "--state", "answered", "--page", "hinge")
        doc = needs.analyst_status(self.ws)
        self.assertEqual(doc["pages"][0], {"id": rid, "state": "answered", "verdict": "hinge"})
        self.assertEqual(doc["action"], [])
        pub = needs.public_status(self.ws, self.repo)
        self.assertEqual(pub["pages"][0]["state"], "answered")
        self.assertEqual(pub["action"], [rid, other])
        git(self.repo, "switch", "-q", "-c", "page/hinge")
        self.add_page("hinge")  # on a branch only: not closed yet
        self.assertEqual(needs.public_status(self.ws, self.repo)["pages"][0]["state"], "answered")
        git(self.repo, "switch", "-q", "main")
        git(self.repo, "merge", "-q", "--ff-only", "page/hinge")
        pub = needs.public_status(self.ws, self.repo)
        self.assertEqual(pub["pages"][0], {"id": rid, "state": "closed", "verdict": "hinge"})
        self.assertEqual(pub["action"], [other])
        self.assertIn("3 requests: 1 declined, 1 closed, 1 invalid", research.status_lines(pub)[-1])

    def test_a_tampered_status_does_not_count(self):
        rid, _ = self.file_two()
        self.answers.mkdir()
        doc = {"schema": needs.STATUS, "request": rid, "state": "accepted", "page": None}
        doc["time"] = "2026-01-01T00:00:00Z"
        (self.answers / f"{rid}.accepted.json").write_bytes(json.dumps(doc).encode())  # not canonical
        self.assertEqual(needs.state_of(self.answers, rid), ("new", None))
        (self.answers / f"{rid}.declined.json").write_bytes(research.canonical({**doc, "state": "accepted"}))
        self.assertEqual(needs.state_of(self.answers, rid), ("new", None))  # its name and its state disagree

    def test_no_requests_folder_is_an_empty_station(self):
        self.assertEqual(needs.analyst_status(self.ws)["pages"], [])
        self.assertEqual(needs.public_status(self.ws, self.repo)["pages"], [])

    def test_the_roles_have_the_stations(self):
        self.assertIn("requests", dict(roles.PROVIDERS["research-coordinator"]))
        self.assertIn("requests", dict(roles.PROVIDERS["engine-coordinator"]))


if __name__ == "__main__":
    unittest.main()
