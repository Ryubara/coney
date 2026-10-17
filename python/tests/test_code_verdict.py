"""Tests for the code gate's verdict check (`cleanroom code-verdict` and `code-verdict-sign`), on a made-up repository
with synthetic C++ sources and made-up keys generated in a temp folder: no real code, no real key."""

import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import codeverdict, research, signing
from coney_tools.cleanroom import scan
from coney_tools.cli import main

SSH_KEYGEN = scan.find_program("ssh-keygen")
GIT = scan.find_program("git")
SETTINGS = """[project]
name = "made-up"

[tool.coney-tools.code-gate]
engine_root = "engine"
source_extensions = [".cpp", ".hpp"]
game_behaviour = []
"""
SOURCE = "// @orig 0x00aa11bb open the store\n// @page research/pages/store/page.md\nint open_store() { return 1; }\n"
OTHER = "// @orig 0x00aa11cc close the store\n// @page research/pages/store/page.md\nint close_store() { return 0; }\n"


def run_cli(*argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main([str(a) for a in argv])
    assert "return" not in (out.getvalue() + err.getvalue())  # never a line of code
    return code, out.getvalue(), err.getvalue()


def pub(path):
    return " ".join(Path(f"{path}.pub").read_text(encoding="utf-8").split()[:2])


def verdict(files, functions, prefilter="pass", commit="ab" * 20):
    return {
        "schema": codeverdict.SCHEMA,
        "commit": commit,
        "files": [{"path": p, "blob": b} for p, b in sorted(files.items())],
        "functions": [{"path": p, "address": a, "verdict": v} for p, a, v in sorted(functions)],
        "prefilter": prefilter,
    }


class Form(unittest.TestCase):
    def test_well_formed_and_passing(self):
        doc = verdict({"engine/a.cpp": "1" * 40}, [("engine/a.cpp", "0x00aa11bb", "independent")])
        self.assertEqual(codeverdict.problems(doc), [])
        self.assertEqual(codeverdict.failing(doc), [])
        self.assertEqual(codeverdict.parse(research.canonical(doc)), (doc, []))

    def test_malformed(self):
        good = verdict({"engine/a.cpp": "1" * 40}, [("engine/a.cpp", "0x00aa11bb", "independent")])
        cases = {
            "unknown key": dict(good, note="free text"),
            "bad schema": dict(good, schema="coney-code-verdict/2"),
            "bad commit": dict(good, commit="HEAD"),
            "bad prefilter": dict(good, prefilter="maybe"),
            "no files": dict(good, files=[], functions=[]),
            "bad path": dict(good, files=[{"path": "../a.cpp", "blob": "1" * 40}], functions=[]),
            "unsorted": dict(good, files=[{"path": "engine/b", "blob": "1" * 40},
                                          {"path": "engine/a", "blob": "1" * 40}], functions=[]),
            "function file not listed": dict(good, functions=[{"path": "engine/b.cpp", "address": "0x00aa11bb",
                                                               "verdict": "independent"}]),
            "bad address": dict(good, functions=[{"path": "engine/a.cpp", "address": "0xAA",
                                                  "verdict": "independent"}]),
            "bad verdict": dict(good, functions=[{"path": "engine/a.cpp", "address": "0x00aa11bb", "verdict": "fine"}]),
        }  # fmt: skip
        for name, doc in cases.items():
            with self.subTest(name):
                self.assertTrue(codeverdict.problems(doc))
        self.assertEqual(codeverdict.parse(json.dumps(good).encode())[1][0][:24], "not in canonical form (s")

    def test_failing(self):
        doc = verdict({"engine/a.cpp": "1" * 40}, [("engine/a.cpp", "0x00aa11bb", "parallel")], prefilter="flag")
        self.assertEqual(codeverdict.failing(doc), ["the pre-filter flagged it", "engine/a.cpp 0x00aa11bb: parallel"])


@unittest.skipUnless(SSH_KEYGEN and GIT, "needs ssh-keygen and git on PATH")
class Repo(unittest.TestCase):
    """main holds allowed_signers and the settings; each test branches from it."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.tmp = Path(cls._tmp.name)
        keys = cls.tmp / "keys"
        keys.mkdir()
        cls.key = {}
        for name in ("pa", "pa2", "r1", "pg", "late"):
            cls.key[name] = keys / name
            subprocess.run(
                [SSH_KEYGEN, "-q", "-t", "ed25519", "-N", "", "-C", "", "-f", str(cls.key[name])],
                check=True,
                capture_output=True,
                creationflags=scan.NO_WINDOW,
            )
        v, g = 'namespaces="coney-verdict"', 'namespaces="git"'
        allowed = (
            f"pr-analyst-1 {v} {pub(cls.key['pa'])}\n"
            f"pr-analyst-2 {v} {pub(cls.key['pa2'])}\n"
            f"page-reviewer-1 {v} {pub(cls.key['r1'])}\n"
            f"pr-analyst-3 {g} {pub(cls.key['pg'])}\n"  # a commit key under a PR analyst's name
        )
        cls.repo = cls.tmp / "repo"
        cls.repo.mkdir()
        cls.git("init", "-q", "-b", "main")
        for k, val in (("user.name", "Test"), ("user.email", "test@example.invalid"), ("core.autocrlf", "false")):
            cls.git("config", k, val)
        cls.write(".gitattributes", "research/** -text\n")
        cls.write("allowed_signers", allowed)
        cls.write("python/pyproject.toml", SETTINGS)
        cls.write("engine/src/old.cpp", OTHER)
        cls.git("add", "-A")
        cls.git("commit", "-q", "-m", "Main")

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    @classmethod
    def git(cls, *args):
        r = subprocess.run(
            [GIT, "-C", str(cls.repo), *args],
            capture_output=True,
            env=signing.git_env(),
            creationflags=scan.NO_WINDOW,
            check=True,
        )
        return r.stdout.decode().strip()

    @classmethod
    def write(cls, rel, data):
        p = cls.repo / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, str):
            data = data.encode("utf-8")
        p.write_bytes(data)

    def setUp(self):
        self.git("checkout", "-q", "-B", f"t-{self._testMethodName}", "main")

    def tearDown(self):
        self.git("checkout", "-q", "--force", "main")
        self.git("clean", "-q", "-f", "-d", "-x")

    def commit(self, files, remove=()):
        for rel, data in files.items():
            self.write(rel, data)
        for rel in remove:
            self.git("rm", "-q", rel)
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", "Change")

    def blob(self, rel):
        return self.git("rev-parse", f"HEAD:{rel}")

    def signed(self, doc, name="v1", keys=(("pr-analyst-1", "pa"),), tamper=False):
        data = research.canonical(doc)
        out = {f"{codeverdict.FOLDER}/{name}.json": data}
        for principal, key in keys:
            sig = signing.sign(SSH_KEYGEN, self.key[key], data + (b" " if tamper else b""))
            out[f"{codeverdict.FOLDER}/{name}.{principal}.sig"] = sig
        return out

    def check(self, rng="main..HEAD"):
        return run_cli("cleanroom", "code-verdict", "--repo", self.repo, rng)

    def covered(self, path="engine/src/store.cpp", verdict_of="independent", **kw):
        """A change adding a source file, then a commit adding a verdict on it."""
        self.commit({path: SOURCE})
        doc = verdict({path: self.blob(path)}, [(path, "0x00aa11bb", verdict_of)], **kw)
        return doc

    def test_no_engine_change_passes(self):
        self.commit({"docs/a.md": "text\n"})
        code, out, err = self.check()
        self.assertEqual(code, 0, out + err)
        self.assertIn("0 engine source files changed", out)

    def test_a_covered_change_passes(self):
        doc = self.covered()
        self.commit(self.signed(doc))
        code, out, err = self.check()
        self.assertEqual(code, 0, out + err)
        self.assertIn("PASS v1: signed by pr-analyst-1", out)
        self.assertIn("1 engine source files changed in main..HEAD, 1 covered", out)

    def test_an_uncovered_file_and_a_stale_blob_fail(self):
        doc = self.covered()
        self.commit(self.signed(doc))
        self.commit({"engine/src/store.cpp": SOURCE + "int more() { return 2; }\n", "engine/src/new.hpp": "int x;\n"})
        code, out, err = self.check()
        self.assertEqual(code, 1, out + err)
        self.assertIn("engine/src/store.cpp: no passing verdict in this range lists it", out)
        self.assertIn("engine/src/new.hpp: no passing verdict", out)

    def test_a_tag_without_a_verdict_fails(self):
        self.commit({"engine/src/store.cpp": SOURCE + OTHER})
        path = "engine/src/store.cpp"
        doc = verdict({path: self.blob(path)}, [(path, "0x00aa11bb", "independent")])
        self.commit(self.signed(doc))
        code, out, _ = self.check()
        self.assertEqual(code, 1)
        self.assertIn("engine/src/store.cpp: 0x00aa11cc has no verdict", out)

    def test_not_independent_or_flagged_fails(self):
        for kw in ({"verdict_of": "parallel"}, {"verdict_of": "transcribed"}, {"prefilter": "flag"}):
            with self.subTest(kw):
                self.setUp()
                self.commit(self.signed(self.covered(**kw)))
                code, out, _ = self.check()
                self.assertEqual(code, 1, out)
                self.assertIn("no passing verdict", out)

    def test_signatures_that_do_not_count(self):
        cases = (
            ((("page-reviewer-1", "r1"),), False, "not a PR analyst"),
            ((("pr-analyst-3", "pg"),), False, "pr-analyst-3's signature"),  # a commit key
            ((("pr-analyst-1", "late"),), False, "pr-analyst-1's signature"),  # a key not listed
            ((("pr-analyst-1", "pa"),), True, "pr-analyst-1's signature"),  # other bytes
            ((), False, "v1: not signed"),
        )
        for keys, tamper, want in cases:
            with self.subTest(want):
                self.setUp()
                self.commit(self.signed(self.covered(), keys=keys, tamper=tamper))
                code, out, _ = self.check()
                self.assertEqual(code, 1, out)
                self.assertIn(want, out)

    def test_one_bad_signature_fails_even_beside_a_good_one(self):
        doc = self.covered()
        files = self.signed(doc, keys=(("pr-analyst-1", "pa"), ("page-reviewer-1", "r1")))
        self.commit(files)
        self.assertEqual(self.check()[0], 1)

    def test_non_canonical_unknown_field_and_stray_files_fail(self):
        doc = self.covered()
        files = self.signed(doc)
        name = f"{codeverdict.FOLDER}/v1.json"
        files[name] = json.dumps(doc).encode()
        self.commit(files)
        code, out, _ = self.check()
        self.assertEqual(code, 1)
        self.assertIn("v1: not in canonical form", out)
        self.setUp()
        self.commit(self.signed(dict(self.covered(), note="free text")))
        self.assertIn("v1: not an object with exactly", self.check()[1])
        self.setUp()
        files = self.signed(self.covered())
        files[f"{codeverdict.FOLDER}/notes.txt"] = b"text\n"
        files[f"{codeverdict.FOLDER}/v2.pr-analyst-1.sig"] = files[f"{codeverdict.FOLDER}/v1.pr-analyst-1.sig"]
        self.commit(files)
        code, out, _ = self.check()
        self.assertEqual(code, 1)
        self.assertIn("notes.txt: not a verdict or a signature", out)
        self.assertIn("v2: signatures without their verdict", out)

    def test_a_deleted_file_needs_nothing_and_old_verdicts_are_not_checked(self):
        self.commit({**self.signed(self.covered(), name="old")}, remove=())
        self.git("checkout", "-q", "-B", "main-later")
        self.commit({"engine/src/store.cpp": SOURCE}, remove=("engine/src/old.cpp",))
        code, out, err = self.check("t-test_a_deleted_file_needs_nothing_and_old_verdicts_are_not_checked..HEAD")
        self.assertEqual(code, 0, out + err)
        self.assertIn("0 engine source files changed", out)

    def test_work_merged_into_the_base_meanwhile_is_not_counted(self):
        doc = self.covered()
        self.commit(self.signed(doc))
        branch = self.git("rev-parse", "HEAD")
        self.git("checkout", "-q", "-B", "base-later", "main")
        self.commit({"engine/src/elsewhere.cpp": "int y;\n"})
        code, out, err = self.check(f"base-later..{branch}")
        self.assertEqual(code, 0, out + err)

    def test_settings_and_keys_come_from_the_ref(self):
        doc = self.covered()
        self.commit(self.signed(doc, keys=(("pr-analyst-4", "late"),)))
        with open(self.repo / "allowed_signers", "a", encoding="utf-8", newline="\n") as f:
            f.write(f'pr-analyst-4 namespaces="coney-verdict" {pub(self.key["late"])}\n')
        self.commit({"python/pyproject.toml": SETTINGS.replace('"engine"', '"elsewhere"')})
        code, out, _ = self.check()
        self.assertEqual(code, 1)  # the branch's own key and settings do not count
        self.assertIn("pr-analyst-4's signature", out)

    def test_bad_ranges_could_not_run(self):
        for rng in ("main", "main...HEAD", "nope..HEAD"):
            with self.subTest(rng):
                self.assertEqual(self.check(rng)[0], 2)

    def test_json(self):
        self.commit(self.signed(self.covered()))
        code, out, _ = run_cli("cleanroom", "code-verdict", "--repo", self.repo, "--json", "main..HEAD")
        doc = json.loads(out)
        self.assertEqual((code, doc["ok"], doc["covered"]), (0, True, ["engine/src/store.cpp"]))
        self.assertEqual(doc["verdicts"], {"v1": ["pr-analyst-1"]})

    # --- code-verdict-sign ----------------------------------------------------------------------------------------

    def result(self, doc, **change):
        """A pre-filter result file matching the verdict, with any field changed."""
        result = {
            "schema": codeverdict.PREFILTER_SCHEMA,
            "commit": doc["commit"],
            "outcome": doc["prefilter"],
            "functions": [{"path": f["path"], "address": f["address"], "outcome": "pass"} for f in doc["functions"]],
        }
        result.update(change)
        p = self.tmp / "in" / f"result-{self._testMethodName}-{len(list(self.tmp.glob('in/result-*')))}.json"
        p.parent.mkdir(exist_ok=True)
        p.write_bytes(research.canonical(result))
        return p

    def test_sign_writes_canonical_bytes_and_a_signature_that_passes(self):
        doc = self.covered()
        src, out = self.tmp / "in" / "v9.json", self.tmp / f"out-{self._testMethodName}"
        src.parent.mkdir(exist_ok=True)
        out.mkdir()
        src.write_text(json.dumps(doc), encoding="utf-8")
        args = ("cleanroom", "code-verdict-sign", src, "--key", self.key["pa"], "--principal", "pr-analyst-1")
        args += ("--prefilter", self.result(doc))
        code, text, err = run_cli(*args, "--out", out)
        self.assertEqual(code, 0, text + err)
        self.assertEqual((out / "v9.json").read_bytes(), research.canonical(doc))
        self.assertEqual(run_cli(*args, "--out", out)[0], 2)  # add-only
        files = {f"{codeverdict.FOLDER}/{p.name}": p.read_bytes() for p in out.iterdir()}
        self.commit(files)
        self.assertEqual(self.check()[0], 0)

    def test_sign_refuses_a_verdict_that_does_not_pass(self):
        src, out = self.tmp / "in" / "bad.json", self.tmp / f"out-{self._testMethodName}"
        src.parent.mkdir(exist_ok=True)
        out.mkdir()
        doc = verdict({"engine/a.cpp": "1" * 40}, [("engine/a.cpp", "0x00aa11bb", "parallel")])
        src.write_text(json.dumps(doc), encoding="utf-8")
        args = ("cleanroom", "code-verdict-sign", src, "--key", self.key["pa"], "--out", out)
        args += ("--prefilter", self.result(doc))
        code, text, _ = run_cli(*args, "--principal", "pr-analyst-1")
        self.assertEqual(code, 1)
        self.assertIn("not signed: only a passing verdict is", text)
        self.assertEqual(list(out.iterdir()), [])
        self.assertEqual(run_cli(*args, "--principal", "page-reviewer-1")[0], 2)

    def test_sign_refuses_a_prefilter_result_that_does_not_match(self):
        src, out = self.tmp / "in" / "v8.json", self.tmp / f"out-{self._testMethodName}"
        src.parent.mkdir(exist_ok=True)
        out.mkdir()
        doc = verdict({"engine/a.cpp": "1" * 40}, [("engine/a.cpp", "0x00aa11bb", "independent")])
        src.write_text(json.dumps(doc), encoding="utf-8")
        flagged = [{"path": "engine/a.cpp", "address": "0x00aa11bb", "outcome": "flag"}]
        other = [{"path": "engine/a.cpp", "address": "0x00aa11cc", "outcome": "pass"}]
        cases = {
            "another commit": (self.result(doc, commit="cd" * 20), "for another commit"),
            "flagged": (self.result(doc, outcome="flag", functions=flagged), "outcome is not the verdict's"),
            "other functions": (self.result(doc, functions=other), "judged other functions"),
            "fewer functions": (self.result(doc, functions=[]), "judged other functions"),
            "bad schema": (self.result(doc, schema="coney-prefilter/2"), "schema is not"),
            "extra key": (self.result(doc, note="x"), "not an object with exactly"),
            "outcome without a flag": (self.result(doc, outcome="flag"), "outcome is not flag exactly when"),
        }
        bad = self.tmp / "in" / "bad-result.json"
        bad.write_text("{", encoding="utf-8")
        cases["not json"] = (bad, "not UTF-8 JSON")
        args = ("cleanroom", "code-verdict-sign", src, "--key", self.key["pa"], "--out", out)
        for name, (result, want) in cases.items():
            with self.subTest(name):
                code, text, _ = run_cli(*args, "--principal", "pr-analyst-1", "--prefilter", result)
                self.assertEqual(code, 1, text)
                self.assertIn(want, text)
                self.assertEqual(list(out.iterdir()), [])
        missing = self.tmp / "in" / "missing.json"
        self.assertEqual(run_cli(*args, "--principal", "pr-analyst-1", "--prefilter", missing)[0], 2)
        with self.assertRaises(SystemExit) as raised, redirect_stderr(io.StringIO()):
            main([str(a) for a in (*args, "--principal", "pr-analyst-1")])  # --prefilter is required
        self.assertEqual(raised.exception.code, 2)


class FakeWs:
    def __init__(self, root):
        self.paths = {"from_analyst": root / "from-analyst", "containers": root / "containers"}

    def where(self, key):
        return self.paths[key]


def git(repo, *args):
    who = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@x", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@x"}
    env = signing.git_env(**who)
    r = subprocess.run([GIT, "-C", str(repo), *args], env=env, capture_output=True, check=True)
    return r.stdout.decode().strip()


@unittest.skipUnless(GIT, "needs git on PATH")
class Inboxes(unittest.TestCase):
    """The PR analyst's and the engine coordinator's stations, on made-up repositories and verdicts."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.ws = FakeWs(self.root)
        self.handover = self.root / "from-analyst" / codeverdict.HANDOVER
        self.repo = self.root / "repo"
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        (self.repo / "a.txt").write_text("a\n")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "base")
        self.head = git(self.repo, "rev-parse", "HEAD")

    def hand_over(self, name, commit, signed=True):
        self.handover.mkdir(parents=True, exist_ok=True)
        data = research.canonical(verdict({"engine/a.cpp": "1" * 40}, [], commit=commit))
        (self.handover / f"{name}.json").write_bytes(data)
        if signed:
            (self.handover / f"{name}.pr-analyst-1.sig").write_bytes(b"made-up signature\n")
        return data

    def test_analyst_station(self):
        outbox = self.ws.where("containers") / "outbox" / "coney.git"
        git(self.root, "clone", "-q", "--bare", str(self.repo), str(outbox))
        for ref in ("impl-1/engine/store", "impl-2/engine/shelf", "impl-1/smoke"):
            git(outbox, "branch", ref, "main")
        doc = codeverdict.analyst_status(self.ws)
        self.assertEqual([(p["id"], p["state"]) for p in doc["pages"]],
                         [("impl-1/engine/store", "to-judge"), ("impl-2/engine/shelf", "to-judge")])  # fmt: skip
        self.hand_over("store", self.head)
        doc = codeverdict.analyst_status(self.ws)
        self.assertEqual(doc["counts"], {"to-judge": 0, "judged": 2})  # both branches stand at the judged commit
        self.assertEqual(doc["action"], [])

    def test_no_outbox_is_an_empty_station(self):
        self.assertEqual(codeverdict.analyst_status(self.ws)["pages"], [])

    def test_public_station(self):
        data = self.hand_over("store", "cd" * 20)
        self.hand_over("unsigned", "cd" * 20, signed=False)
        (self.handover / "stray.json").write_bytes(b"{}")
        (self.handover / "stray.pr-analyst-1.sig").write_bytes(b"x")
        doc = codeverdict.public_status(self.ws, self.repo)
        self.assertEqual(doc["pages"], [{"id": "store", "state": "new", "verdict": "cd" * 6}])
        self.assertEqual(doc["action"], ["store"])
        target = self.repo / "research" / "code-verdicts"
        target.mkdir(parents=True)
        (target / "store.json").write_bytes(data)
        git(self.repo, "switch", "-q", "-c", "engine/store")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "verdict")
        self.assertEqual(codeverdict.public_status(self.ws, self.repo)["pages"][0]["state"], "copied")
        git(self.repo, "switch", "-q", "main")
        git(self.repo, "merge", "-q", "--ff-only", "engine/store")
        doc = codeverdict.public_status(self.ws, self.repo)
        self.assertEqual((doc["pages"][0]["state"], doc["action"]), ("merged", []))
        self.assertIn("1 verdicts: 1 merged", research.status_lines(doc)[-1])


if __name__ == "__main__":
    unittest.main()
