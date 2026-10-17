"""Tests for the code gate's scripted pre-filter (`cleanroom prefilter`), on a made-up repository with synthetic C++
sources, a synthetic page and synthetic facts built in a temp folder: no real code, no real page, no real address."""

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

from coney_tools import codegate, codeverdict, prefilter, research, signing
from coney_tools.cleanroom import scan
from coney_tools.cli import main

GIT = scan.find_program("git")
SETTINGS = """[project]
name = "made-up"

[tool.coney-tools.code-gate]
engine_root = "engine"
source_extensions = [".cpp", ".hpp"]
game_behaviour = []
"""
PAGE = "# Store\n\nThe store holds 2,048 items, waits 2.5 s and has 0x40 shelves at 0x00aa11bb.\n"
LINK = "research/pages/store/page.md"
# Two tagged helpers on main, and one untagged.
HELPERS = (
    "// @orig 0x00aa0001 fill a shelf\n// @page research/pages/store/page.md\nvoid fill() {}\n"
    "// @orig 0x00aa0002 count a shelf\n// @page research/pages/store/page.md\nint count(int n) { return n; }\n"
    "void helper() {}\n"
)
FILL, COUNT, UNTAGGED = "0x00aa0001", "0x00aa0002", "0x00ff0001"
JUDGED = "0x00aa11bb"
PATH = "engine/src/store.cpp"


def tagged(body, address=JUDGED, head="int open_store(int x)"):
    return f"// @orig {address} open the store\n// @page {LINK}\n{head} {{\n{body}\n}}\n"


def run_cli(*argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


class Numbers(unittest.TestCase):
    def test_literal_values(self):
        cases = {
            "0": 0, "42": 42, "0x10": 16, "0X1Fu": 31, "1'000": 1000, "0b101": 5, "017": 15, "10u": 10, "3ULL": 3,
            "7uz": 7, "2.5f": 2.5, "1e3": 1000.0, ".5": 0.5, "1.f": 1.0, "0x1p4": 16.0, "0x1.8p1f": 3.0,
            "10_ms": 10, "2.5_s": 2.5, "1.5L": 1.5, "09": None,
        }  # fmt: skip
        for token, want in cases.items():
            with self.subTest(token):
                self.assertEqual(prefilter.literal_value(token), want)

    def test_literals_in_code_and_unary_minus(self):
        code = 'x = -1; y = a - 1; z = f(-2, 3); s = "77"; // 88\n w = b-- - 4; return -5;'
        code, _ = codegate.lex(code)
        self.assertEqual(prefilter.literals(code, 0, len(code)), [-1, 1, -2, 3, 4, -5])

    def test_page_values(self):
        values = prefilter.page_values("2,048 items, 2.5 s, 0x40 and -3, range 10-20, 1,234.5")
        for v in (2048, 2, 48, 2.5, 64, 3, -3, 10, 20, 1234.5, 1, 234):
            self.assertIn(v, values)
        self.assertNotIn(-2048, values)

    def test_branch_points(self):
        body = (
            "if (a) {} else if (b) {} else {}\n for (;;) {} while (c) {} do {} while (d);\n"
            "x = a ? 1 : 2; y = a && b || c; switch (x) { case 1: break; default: break; }\n"
            'try {} catch (...) {} s = "if (x) && y ?"; // if for while\n'
        )
        code, _ = codegate.lex(body)
        # if, if, for, while, do, while, ?, &&, ||, case, default, catch
        self.assertEqual(prefilter.branches(code, 0, len(code)), 12)


@unittest.skipUnless(GIT, "needs git on PATH")
class Repo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.tmp = Path(cls._tmp.name)
        cls.repo = cls.tmp / "repo"
        cls.repo.mkdir()
        cls.git("init", "-q", "-b", "main")
        for k, val in (("user.name", "Test"), ("user.email", "test@example.invalid"), ("core.autocrlf", "false")):
            cls.git("config", k, val)
        cls.write("python/pyproject.toml", SETTINGS)
        cls.write(LINK, PAGE)
        cls.write("engine/src/helpers.cpp", HELPERS)
        cls.git("add", "-A")
        cls.git("commit", "-q", "-m", "Main")
        cls.n = 0

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
        p.write_bytes(data.encode("utf-8"))

    def setUp(self):
        self.git("checkout", "-q", "-B", f"t-{self._testMethodName}", "main")

    def tearDown(self):
        self.git("checkout", "-q", "--force", "main")
        self.git("clean", "-q", "-f", "-d", "-x")

    def commit(self, files):
        for rel, data in files.items():
            self.write(rel, data)
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", "Change")

    def facts(self, functions=None, raw=None):
        Repo.n += 1
        p = self.tmp / f"facts-{Repo.n}.json"
        doc = {"schema": prefilter.FACTS_SCHEMA, "functions": functions or {JUDGED: {"callees": [], "branches": 0}}}
        p.write_text(raw if raw is not None else json.dumps(doc), encoding="utf-8")
        return p

    def run_pf(self, facts=None, *extra):
        code, out, err = run_cli(
            "cleanroom", "prefilter", "main..HEAD", "--repo", self.repo, "--facts", facts or self.facts(), *extra
        )
        return code, out + err

    def outcome(self, body, functions=None, **kw):
        self.commit({PATH: tagged(body, **kw)})
        return self.run_pf(self.facts(functions))

    # --- constant-not-on-page -------------------------------------------------------------------------------------

    def test_a_clean_function_passes_and_writes_its_result(self):
        self.commit({PATH: tagged("int n = x + 1; fill(); return n * 2;")})
        out = self.tmp / f"result-{self._testMethodName}.json"
        code, text = self.run_pf(None, "--out", out)
        self.assertEqual(code, 0, text)
        self.assertIn(f"PASS {PATH} {JUDGED}\n", text)
        self.assertIn("PASS: 1 tagged functions in 1 engine source files changed", text)
        doc = json.loads(out.read_bytes())
        self.assertEqual(out.read_bytes(), research.canonical(doc))
        self.assertEqual(codeverdict.prefilter_problems(doc), [])
        want = {
            "schema": codeverdict.PREFILTER_SCHEMA,
            "commit": self.git("rev-parse", "HEAD"),
            "outcome": "pass",
            "functions": [{"path": PATH, "address": JUDGED, "outcome": "pass"}],
        }
        self.assertEqual(doc, want)
        self.assertEqual(self.run_pf(None, "--out", out)[0], 2)  # never overwritten

    def test_a_constant_not_on_the_page_flags_without_its_value(self):
        code, text = self.outcome("int n = x * 7731; return n;")
        self.assertEqual(code, 1, text)
        self.assertIn(f"FLAG {PATH} {JUDGED}: constant-not-on-page", text)
        self.assertNotIn("7731", text)

    def test_page_numbers_match_in_hex_or_decimal(self):
        for body in ("return x % 0x800;", "return x * 2'048;", "return x + 64;", "return x / 0x40u;",
                     "float t = 2.5f; return x;", "return x + 0x00aa11bb;"):  # fmt: skip
            with self.subTest(body):
                self.setUp()
                self.assertEqual(self.outcome(body)[0], 0)

    def test_comments_and_strings_are_not_constants(self):
        body = 'const char* s = "9999"; // 8888\n /* 7777 */ return x;'
        self.assertEqual(self.outcome(body)[0], 0)

    def test_the_trivial_set(self):
        self.assertEqual(self.outcome("return x * 2 + 1 - 0 + -1;")[0], 0)
        for body in ("return -5;", "return 3;"):
            with self.subTest(body):
                self.setUp()
                self.assertEqual(self.outcome(body)[0], 1)
        self.setUp()
        self.commit({"python/pyproject.toml": SETTINGS + "prefilter_trivial_constants = [3]\n"})
        self.assertEqual(self.outcome("return 3;")[0], 0)
        self.setUp()
        self.commit({"python/pyproject.toml": SETTINGS + "prefilter_trivial_constants = [3]\n"})
        self.assertEqual(self.outcome("return -1;")[0], 1)  # the table replaces the default set

    # --- same-call-order ------------------------------------------------------------------------------------------

    def test_the_same_call_order_flags_without_addresses(self):
        facts = {JUDGED: {"callees": [FILL, UNTAGGED, COUNT], "branches": 0}}
        code, text = self.outcome("fill(); helper(); count(x); return x;", facts)
        self.assertEqual(code, 1, text)
        self.assertIn(f"FLAG {PATH} {JUDGED}: same-call-order", text)
        for address in (FILL, COUNT, UNTAGGED):
            self.assertNotIn(address, text)

    def test_another_call_order_passes(self):
        facts = {JUDGED: {"callees": [FILL, COUNT], "branches": 0}}
        self.assertEqual(self.outcome("count(x); fill(); return x;", facts)[0], 0)
        self.setUp()
        self.assertEqual(self.outcome("fill(); count(x); fill(); return x;", facts)[0], 0)

    def test_an_argument_is_called_first(self):
        facts = {JUDGED: {"callees": [COUNT, FILL], "branches": 0}}
        code, text = self.outcome("store(fill_with(count(x))); return x;", facts)
        self.assertEqual(code, 0, text)  # fill_with is not fill
        self.setUp()
        self.commit({"engine/src/two.cpp": "// @orig 0x00aa0003 x\n// @page research/pages/store/page.md\n"
                     "int wrap(int v) { return v; }\n"})  # fmt: skip
        facts = {
            JUDGED: {"callees": [COUNT, "0x00aa0003"], "branches": 0},
            "0x00aa0003": {"callees": [], "branches": 0},
        }
        code, text = self.outcome("return wrap(count(x));", facts)
        self.assertEqual(code, 1, text)

    def test_the_minimum_calls(self):
        facts = {JUDGED: {"callees": [FILL], "branches": 0}}
        self.assertEqual(self.outcome("fill(); return x;", facts)[0], 0)  # one call is below the default of 2
        self.setUp()
        self.commit({"python/pyproject.toml": SETTINGS + "prefilter_min_calls = 1\n"})
        self.assertEqual(self.outcome("fill(); return x;", facts)[0], 1)
        self.setUp()
        self.commit({"python/pyproject.toml": SETTINGS + "prefilter_min_calls = 3\n"})
        facts = {JUDGED: {"callees": [FILL, COUNT], "branches": 0}}
        self.assertEqual(self.outcome("fill(); count(x); return x;", facts)[0], 0)

    # --- same-branch-count ----------------------------------------------------------------------------------------

    def test_the_same_branch_count(self):
        body = "if (x) { x = x * 2; } else if (x > 2) {} for (;;) { break; } return x && x;"
        code, text = self.outcome(body, {JUDGED: {"callees": [], "branches": 4}})
        self.assertEqual(code, 1, text)
        self.assertIn(f"FLAG {PATH} {JUDGED}: same-branch-count", text)
        self.setUp()
        self.assertEqual(self.outcome(body, {JUDGED: {"callees": [], "branches": 5}})[0], 0)
        self.setUp()
        body3 = "if (x) {} for (;;) { break; } return x && x;"
        self.assertEqual(self.outcome(body3, {JUDGED: {"callees": [], "branches": 3}})[0], 0)  # below the minimum
        self.setUp()
        self.commit({"python/pyproject.toml": SETTINGS + "prefilter_min_branches = 3\n"})
        self.assertEqual(self.outcome(body3, {JUDGED: {"callees": [], "branches": 3}})[0], 1)

    def test_every_rule_at_once_and_json(self):
        body = "fill(); count(x); if (x) {} if (x) {} if (x) {} if (x) {} return 4321;"
        self.commit({PATH: tagged(body)})
        facts = self.facts({JUDGED: {"callees": [FILL, COUNT], "branches": 4}})
        code, text = self.run_pf(facts)
        self.assertEqual(code, 1)
        self.assertIn(f"FLAG {PATH} {JUDGED}: constant-not-on-page, same-call-order, same-branch-count", text)
        code, text = self.run_pf(facts, "--json")
        doc = json.loads(text)
        self.assertEqual((code, doc["outcome"]), (1, "flag"))
        self.assertEqual(doc["rules"][f"{PATH} {JUDGED}"], [r for r, _ in prefilter.RULES])
        self.assertNotIn("4321", text)

    # --- what is judged -------------------------------------------------------------------------------------------

    def test_only_changed_files_are_judged_and_untagged_functions_are_not(self):
        self.commit({"engine/src/plain.cpp": "int plain() { return 9876; }\n", "docs/a.md": "77\n"})
        code, text = self.run_pf(self.facts({}))
        self.assertEqual(code, 0, text)
        self.assertIn("PASS: 0 tagged functions in 1 engine source files changed", text)

    def test_settings_come_from_b(self):
        self.commit({"python/pyproject.toml": SETTINGS.replace('"engine"', '"elsewhere"')})
        self.commit({PATH: tagged("return 7731;")})
        self.assertEqual(self.run_pf()[0], 0)  # the engine is elsewhere at B: nothing judged

    # --- could not run --------------------------------------------------------------------------------------------

    def test_outbox_judges_in_the_workspaces_collecting_repo(self):
        self.commit({PATH: tagged("return x;")})
        containers = self.tmp / f"containers-{self._testMethodName}"
        ws = mock.Mock(where=lambda key: {"containers": containers}[key])
        outbox = codeverdict.outbox(ws)
        self.git("clone", "-q", "--bare", str(self.repo), str(outbox))  # HEAD is this test's branch
        argv = ("cleanroom", "prefilter", "main..HEAD", "--facts", self.facts(), "--outbox", "--workspace", "w.json")
        with mock.patch("coney_tools.workspace.load", return_value=ws) as load:
            code, out, err = run_cli(*argv)
        self.assertEqual(code, 0, out + err)
        self.assertIn(f"PASS {PATH} {JUDGED}\n", out)
        load.assert_called_once_with("w.json")
        with self.assertRaises(SystemExit), redirect_stderr(io.StringIO()):
            main([str(a) for a in (*argv, "--repo", self.repo)])  # one repository or the other

    def test_a_missing_address_could_not_run(self):
        self.commit({PATH: tagged("return x;")})
        code, text = self.run_pf(self.facts({"0x00aa11cc": {"callees": [], "branches": 0}}))
        self.assertEqual(code, 2)
        self.assertIn(f"no entry for {JUDGED}", text)
        self.assertNotIn("0x00aa11cc", text)

    def test_malformed_facts_could_not_run(self):
        self.commit({PATH: tagged("return x;")})
        good = {"callees": [], "branches": 0}
        cases = {
            "not json": "{",
            "extra key": json.dumps({"schema": prefilter.FACTS_SCHEMA, "functions": {}, "note": "x"}),
            "bad schema": json.dumps({"schema": "coney-prefilter-facts/2", "functions": {}}),
            "functions a list": json.dumps({"schema": prefilter.FACTS_SCHEMA, "functions": []}),
            "upper-case key": json.dumps({"schema": prefilter.FACTS_SCHEMA, "functions": {"0x00AA11BB": good}}),
            "short key": json.dumps({"schema": prefilter.FACTS_SCHEMA, "functions": {"0xaa11bb": good}}),
            "extra entry key": json.dumps({"schema": prefilter.FACTS_SCHEMA,
                                           "functions": {JUDGED: dict(good, name="x")}}),
            "bad callee": json.dumps({"schema": prefilter.FACTS_SCHEMA,
                                      "functions": {JUDGED: {"callees": ["0x1234"], "branches": 0}}}),
            "callees not a list": json.dumps({"schema": prefilter.FACTS_SCHEMA,
                                              "functions": {JUDGED: {"callees": FILL, "branches": 0}}}),
            "negative branches": json.dumps({"schema": prefilter.FACTS_SCHEMA,
                                             "functions": {JUDGED: {"callees": [], "branches": -1}}}),
            "bool branches": json.dumps({"schema": prefilter.FACTS_SCHEMA,
                                         "functions": {JUDGED: {"callees": [], "branches": True}}}),
            "float branches": json.dumps({"schema": prefilter.FACTS_SCHEMA,
                                          "functions": {JUDGED: {"callees": [], "branches": 1.0}}}),
        }  # fmt: skip
        for name, raw in cases.items():
            with self.subTest(name):
                code, text = self.run_pf(self.facts(raw=raw))
                self.assertEqual(code, 2, text)
                self.assertIn("facts file", text)
                self.assertNotIn("0x1234", text)

    def test_bad_settings_and_ranges_could_not_run(self):
        self.commit({"python/pyproject.toml": SETTINGS + "prefilter_min_calls = 0\n"})
        self.assertEqual(self.run_pf()[0], 2)
        self.setUp()
        self.commit({"python/pyproject.toml": SETTINGS + 'prefilter_trivial_constants = ["1"]\n'})
        self.assertEqual(self.run_pf()[0], 2)
        self.setUp()
        self.commit({"python/pyproject.toml": SETTINGS + "prefilter_other = 1\n"})
        self.assertEqual(self.run_pf()[0], 2)
        for rng in ("main", "nope..HEAD"):
            code, _, _ = run_cli("cleanroom", "prefilter", rng, "--repo", self.repo, "--facts", self.facts())
            self.assertEqual(code, 2)

    def test_a_tag_without_a_page_could_not_run(self):
        self.commit({PATH: tagged("return x;").replace(LINK, "research/pages/gone/page.md")})
        code, text = self.run_pf()
        self.assertEqual(code, 2)
        self.assertIn("research/pages/gone/page.md is not in HEAD", text)


class Settings(unittest.TestCase):
    def test_defaults_when_the_table_leaves_them_out(self):
        s = codegate.Settings("engine", (".cpp",), ())
        self.assertEqual((s.trivial, s.min_calls, s.min_branches), ((0, 1, -1, 2), 2, 4))


if __name__ == "__main__":
    unittest.main()
