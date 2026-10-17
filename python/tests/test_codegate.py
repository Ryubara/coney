"""Tests for the public side of the code gate (`cleanroom orig-check` and `cleanroom commit-sigs`), on synthetic
pages, synthetic C++ sources and made-up keys generated in a temp folder: no real research, no real key.

Every command's output is checked for a word that appears only in a job: no message may carry a job or a line of
code.
"""

import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import codegate, research, signing
from coney_tools.cleanroom import scan
from coney_tools.cli import main

WORD = "Quuxlewidget"  # only ever in a job
SSH_KEYGEN = scan.find_program("ssh-keygen")
GIT = scan.find_program("git")
SETTINGS = """[project]
name = "made-up"

[tool.coney-tools.code-gate]
engine_root = "engine"
source_extensions = [".cpp", ".hpp"]
game_behaviour = ["engine/src/game"]
"""


def page_text(rows, extra=""):
    table = "".join(f"| `{a}` | {job} | Opening |\n" for a, job in rows)
    return (
        "# Store\n\nA made-up store.\n\n## Entry points\n\n| Address | Job | Outcomes |\n| --- | --- | --- |\n"
        f"{table}\n## Opening\n\nAn empty store opens with no entries.{extra}\n"
    )


def gated(entries, unverified=()):
    pages = codegate.Pages()
    pages.entries = {k: {a: research.page_check.normal_job(j) for a, j in v.items()} for k, v in entries.items()}
    pages.unverified = list(unverified)
    return pages


PAGES = gated(
    {
        "store": {"0x00aa11bb": f"open the {WORD}", "0x00aa11cc": "close the store"},
        "till": {"0x00bb0000": "count the takings"},
    },
    unverified=["broken"],
)


def tagged(address="0x00aa11bb", job=f"open the {WORD}", link="research/pages/store/page.md", body="int open_store()"):
    return f"// @orig {address} {job}\n// @page {link}\n{body} {{ return 0; }}\n"


class Reading(unittest.TestCase):
    """The C++ reader: comments, strings and function definitions."""

    def names(self, text):
        code, _ = codegate.lex(text)
        lines = text.split("\n")
        return [lines[f.name_line - 1].strip() for f in codegate.functions(code)]

    def test_functions_at_every_scope_are_found(self):
        text = """namespace coney::game {
class Store : public Base<decltype(make(1))> {
public:
    Store(int n) : count_{n}, other_(2) { }
    bool operator==(const Store&) const = default;
    bool operator()(int) const { return true; }
    Store& operator=(const Store& o) { return *this; }
    int count_{0};
    static constexpr int table[] = {1, 2};
private:
    void tick() noexcept override { auto l = [](int x) { return x; }; }
};
template <typename T, typename = int> T twice(T t) { return t; }
auto lam = [](int x) { return x; };
extern "C" { void c_fn(void) { } }
struct alignas(16) V { float x; };
enum class E : int { A, B };
int digits = 1'000;
const char* s = "{ not a brace";
char q = '{';
const char* r = R"x({ raw })x";
#define BLOCK { }
/* void commented() { } */
Store::~Store() { }
}"""
        self.assertEqual(
            self.names(text),
            [
                "Store(int n) : count_{n}, other_(2) { }",
                "bool operator()(int) const { return true; }",
                "Store& operator=(const Store& o) { return *this; }",
                "void tick() noexcept override { auto l = [](int x) { return x; }; }",
                "template <typename T, typename = int> T twice(T t) { return t; }",
                'extern "C" { void c_fn(void) { } }',
                "Store::~Store() { }",
            ],
        )

    def test_comments_are_read_and_code_is_blanked(self):
        text = '// one\nint x = 1; /* two\n * three */\nconst char* s = "// not a comment";\n'
        code, comments = codegate.lex(text)
        self.assertEqual([c for _, c in comments], ["// one", "/* two\n * three */"])
        self.assertEqual(len(code), len(text))
        self.assertEqual(code.count("\n"), text.count("\n"))
        self.assertNotIn("not a comment", code)


class Sources(unittest.TestCase):
    """One source file against a set of gated pages."""

    def check(self, text, behaviour=True, pages=PAGES):
        findings, _ = codegate.check_source("engine/src/game/store.cpp", text.encode(), behaviour, pages)
        for f in findings:
            self.assertNotIn(WORD.lower(), f.message.lower())
        return [(f.line, f.rule) for f in findings]

    def test_a_good_tag_passes(self):
        self.assertEqual(self.check(tagged()), [])
        self.assertEqual(self.check(tagged(job=f"  Open   THE {WORD} ")), [])  # spacing and case aside
        two = tagged() + "\n" + tagged("0x00aa11cc", "close the store", body="void close_store()")
        self.assertEqual(self.check(two), [])
        block = f"/*\n * @orig 0x00aa11bb open the {WORD}\n * @page research/pages/store/page.md\n */\nint f() {{ }}\n"
        self.assertEqual(self.check(block), [])

    def test_each_problem_is_named(self):
        cases = [
            (tagged(address="0x00AA11BB"), [(1, "orig-malformed")]),
            (tagged(address="0xaa11bb"), [(1, "orig-malformed")]),
            (tagged(job=""), [(1, "orig-malformed")]),
            (tagged().replace("// @page", "// see"), [(1, "orig-no-page")]),
            (tagged().replace("// @page", "//\n// @page"), [(1, "orig-no-page"), (3, "orig-malformed")]),
            (tagged(link="research/pages/store"), [(2, "orig-page")]),
            (tagged(link="../research/pages/store/page.md"), [(2, "orig-page")]),
            (tagged(link="research/pages/shop/page.md"), [(2, "orig-page")]),
            (tagged(link="research/pages/broken/page.md"), [(2, "orig-page")]),
            (tagged(address="0x00bb0000"), [(1, "orig-address")]),
            (tagged(address="0x00ffffff"), [(1, "orig-address")]),
            (tagged(job="open the till"), [(1, "orig-job")]),
            ("// @page research/pages/store/page.md\nint f() { }\n", [(1, "orig-malformed"), (2, "orig-untagged")]),
            ("// @orig\nint f() { }\n", [(1, "orig-malformed")]),
            ("// see @orig below\nint f() { }\n", [(1, "orig-malformed"), (2, "orig-untagged")]),
            ("int f() { }\n", [(1, "orig-untagged")]),
        ]
        for text, want in cases:
            with self.subTest(text=text):
                self.assertEqual(self.check(text), want)

    def test_a_tag_sits_just_above_a_definition(self):
        declaration = tagged(body="int open_store();\nint later()").replace(" { return 0; }", "")
        self.assertIn((1, "orig-placement"), self.check(declaration + " { }\n"))
        inside = "int f() {\n" + tagged().replace("int open_store() { return 0; }\n", "") + "}\n"
        self.assertEqual(self.check(inside), [(1, "orig-untagged"), (2, "orig-placement")])

    def test_only_game_behaviour_files_need_tags(self):
        self.assertEqual(self.check("int f() { }\n", behaviour=False), [])
        self.assertEqual(self.check(tagged(address="0x00ffffff"), behaviour=False), [(1, "orig-address")])

    def test_a_tag_in_a_string_is_code(self):
        self.assertEqual(self.check('const char* s = "@orig 0x00aa11bb x";\n'), [])

    def test_unreadable_sources_fail(self):
        findings, _ = codegate.check_source("engine/x.cpp", b"\xff\xfe int f() { }", True, PAGES)
        self.assertEqual([f.rule for f in findings], ["orig-not-text"])


def run_cli(*argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main([str(a) for a in argv])
    assert WORD.lower() not in (out.getvalue() + err.getvalue()).lower()
    return code, out.getvalue(), err.getvalue()


def pub(path):
    return " ".join(Path(f"{path}.pub").read_text(encoding="utf-8").split()[:2])


@unittest.skipUnless(SSH_KEYGEN and GIT, "needs ssh-keygen and git on PATH")
class Repo(unittest.TestCase):
    """A made-up repository: main holds allowed_signers, the settings and gated pages; branches hold engine code."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.tmp = Path(cls._tmp.name)
        keys = cls.tmp / "keys"
        keys.mkdir()
        cls.key = {}
        for name in ("r1", "r2", "i1", "i2", "vk", "gk", "late"):
            cls.key[name] = keys / name
            subprocess.run(
                [SSH_KEYGEN, "-q", "-t", "ed25519", "-N", "", "-C", "", "-f", str(cls.key[name])],
                check=True,
                capture_output=True,
                creationflags=scan.NO_WINDOW,
            )
        v, g = 'namespaces="coney-verdict"', 'namespaces="git"'
        cls.allowed = (
            "# made-up keys for tests\n"
            f"page-reviewer-1 {v} {pub(cls.key['r1'])}\n"
            f"page-reviewer-2 {v} {pub(cls.key['r2'])}\n"
            f"impl-1 {g} {pub(cls.key['i1'])}\n"
            f"impl-2 {g} {pub(cls.key['i2'])}\n"
            f"impl-7 {v} {pub(cls.key['vk'])}\n"  # a verdict key under an implementer's name
            f"page-reviewer-8 {g} {pub(cls.key['gk'])}\n"  # a commit key under a reviewer's name
            f"impl-9 {pub(cls.key['gk'])}\n"  # no namespaces: never a commit key
        )
        cls.repo = cls.tmp / "repo"
        cls.repo.mkdir()
        cls.git("init", "-q", "-b", "main")
        for k, val in (
            ("user.name", "Test"),
            ("user.email", "test@example.invalid"),
            ("commit.gpgsign", "false"),
            ("gpg.format", "ssh"),
            ("gpg.ssh.program", SSH_KEYGEN),
            ("core.autocrlf", "false"),
        ):
            cls.git("config", k, val)
        cls.write(".gitattributes", "research/** -text\n")
        cls.write("allowed_signers", cls.allowed)
        cls.write("python/pyproject.toml", SETTINGS)
        cls.add_page("store", page_text([("0x00aa11bb", f"open the {WORD}"), ("0x00aa11cc", "close the store")]))
        cls.add_page("till", page_text([("0x00bb0000", "count the takings")]))
        cls.add_page("broken", page_text([("0x00cc0000", "lose the takings")]), break_it=True)
        cls.git("add", "-A")
        cls.git("commit", "-q", "-m", "Main")
        # Only on a branch: a page that is not merged, a key and settings the branch adds for itself.
        cls.git("checkout", "-q", "-b", "work")
        cls.add_page("shop", page_text([("0x00dd0000", "open the shop")]))
        with open(cls.repo / "allowed_signers", "a", encoding="utf-8", newline="\n") as f:
            f.write(f'impl-3 namespaces="git" {pub(cls.key["late"])}\n')
        cls.write("python/pyproject.toml", SETTINGS.replace('["engine/src/game"]', "[]"))
        cls.git("add", "-A")
        cls.git("commit", "-q", "-m", "Branch setup")

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
    def write(cls, rel, text):
        p = cls.repo / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8", newline="\n")

    @classmethod
    def add_page(cls, page_id, text, break_it=False):
        cls.write(f"research/pages/{page_id}/page.md", text)
        folder = cls.repo / "research" / "pages" / page_id
        page = research.read_page(page_id, folder, folder / "page.md")
        assert page.ok
        decisions = {s.id: {"verdict": "keep", "kind": "fact"} for s in page.sections}
        data = research.canonical(research.decide(page, decisions, {}))
        verdicts = cls.repo / "research" / "verdicts"
        verdicts.mkdir(parents=True, exist_ok=True)
        (verdicts / f"{page_id}.json").write_bytes(data)
        for n, key in ((1, "r1"), (2, "r2")):
            signed = data + b" " if break_it and n == 2 else data
            (verdicts / f"{page_id}.page-reviewer-{n}.sig").write_bytes(signing.sign(SSH_KEYGEN, cls.key[key], signed))

    def setUp(self):
        self.git("checkout", "-q", "-B", f"t-{self._testMethodName}", "work")

    def tearDown(self):
        self.git("checkout", "-q", "--force", "work")
        self.git("clean", "-q", "-f", "-d", "-x")

    def commit(self, files, key=None, message="Change\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"):
        for rel, text in files.items():
            self.write(rel, text)
        self.git("add", "-A")
        sign = ["-c", f"user.signingkey={self.key[key]}", "commit", "-S"] if key else ["commit"]
        self.git(*sign, "-q", "-m", message)
        return self.git("rev-parse", "HEAD")

    # --- orig-check ------------------------------------------------------------------------------------------------

    def orig(self, *extra):
        return run_cli("cleanroom", "orig-check", "--repo", self.repo, *extra)

    def test_orig_check_passes_good_tags_and_reads_pages_from_main(self):
        code, out, err = self.orig()
        self.assertEqual(code, 0, out + err)  # no engine yet
        self.assertIn("0 tags in 0 source files", out)
        till = tagged("0x00bb0000", "count the takings", "research/pages/till/page.md", "int count()")
        self.write("engine/src/game/store.cpp", tagged() + till)
        self.write("engine/src/platform/io.hpp", "inline int helper() { return 1; }\n")
        self.write("engine/notes.txt", "// @orig 0xzz not a source file\n")
        code, out, err = self.orig()
        self.assertEqual(code, 0, out + err)
        self.assertIn("2 tags in 2 source files", out)
        self.assertIn("note: page broken at main does not verify", out)
        # The working tree's page counts for nothing: pages are read from main.
        self.write("research/pages/store/page.md", page_text([("0x00ee0000", "a new job")]))
        self.assertEqual(self.orig()[0], 0)

    def test_orig_check_failures_and_json(self):
        self.write(
            "engine/src/game/store.cpp",
            tagged(link="research/pages/shop/page.md")  # only on the branch
            + tagged(link="research/pages/broken/page.md", body="int b()")
            + tagged(address="0x00bb0000", body="int c()")
            + "int untagged() { }\n",
        )
        code, out, err = self.orig("--json")
        self.assertEqual(code, 1, err)
        doc = json.loads(out)
        self.assertFalse(doc["ok"])
        self.assertEqual(doc["pages"], ["store", "till"])
        self.assertEqual(doc["unverified_pages"], ["broken"])
        got = [(f["path"], f["line"], f["rule"]) for f in doc["findings"]]
        path = "engine/src/game/store.cpp"
        self.assertEqual(
            got,
            [(path, 2, "orig-page"), (path, 5, "orig-page"), (path, 7, "orig-address"), (path, 10, "orig-untagged")],
        )
        self.assertIn("is not merged", doc["findings"][0]["message"])
        self.assertIn("does not verify", doc["findings"][1]["message"])
        self.assertIn("it is on till", doc["findings"][2]["message"])

    def test_settings_come_from_the_ref(self):
        # The branch empties game_behaviour for itself; main's settings still flag the untagged function.
        self.write("engine/src/game/store.cpp", "int untagged() { }\n")
        self.assertEqual(self.orig()[0], 1)
        self.assertEqual(self.orig("--ref", "work")[0], 0)
        code, _, err = self.orig("--ref", "no-such-ref")
        self.assertEqual(code, 2)
        self.assertIn("pyproject.toml", err)
        code, _out, err = self.orig("--ref=--all")
        self.assertEqual(code, 2)
        self.assertIn("not a git ref", err)

    def test_bad_settings_cannot_run(self):
        for bad in (
            SETTINGS.replace('engine_root = "engine"', 'engine_root = "../engine"'),
            SETTINGS.replace('["engine/src/game"]', '["src/game"]'),
            SETTINGS.replace('[".cpp", ".hpp"]', '"cpp"'),
            SETTINGS.replace("[tool.coney-tools.code-gate]", "[tool.other]"),
        ):
            with self.subTest(bad=bad):
                self.commit({"python/pyproject.toml": bad})
                code, out, err = self.orig("--ref", "HEAD")
                self.assertEqual(code, 2, out)
                self.assertIn("pyproject.toml", err)

    def test_rules_are_listed(self):
        code, out, _ = run_cli("cleanroom", "orig-check", "--rules")
        self.assertEqual(code, 0)
        self.assertEqual([line.split()[0] for line in out.splitlines()], [r for r, _ in codegate.RULES])

    # --- commit-sigs -----------------------------------------------------------------------------------------------

    def sigs(self, rng, *extra):
        return run_cli("cleanroom", "commit-sigs", rng, "--repo", self.repo, *extra)

    def test_signed_engine_commits_pass(self):
        base = self.git("rev-parse", "HEAD")
        a = self.commit({"engine/src/a.cpp": "int a() { }\n"}, "i1")
        self.commit({"docs/x.md": "Unsigned, outside the engine.\n"})
        b = self.commit({"engine/src/b.cpp": "int b() { }\n"}, "i2")
        code, out, err = self.sigs(f"{base}..HEAD")
        self.assertEqual(code, 0, out + err)
        self.assertEqual(
            out.splitlines()[:2], [f"PASS {a[:12]} impl-1, Claude Opus 5.5", f"PASS {b[:12]} impl-2, Claude Opus 5.5"]
        )
        self.assertIn("1 others not checked", out)
        code, out, err = self.sigs(f"{base}..HEAD", "--json")
        doc = json.loads(out)
        self.assertEqual([(c["commit"], c["principal"]) for c in doc["commits"]], [(a, "impl-1"), (b, "impl-2")])
        self.assertEqual({c["model"] for c in doc["commits"]}, {"Claude Opus 5.5"})
        self.assertEqual(doc["skipped"], 1)

    def test_a_signed_engine_commit_must_name_its_model(self):
        base = self.git("rev-parse", "HEAD")
        trailer = "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
        messages = [
            "Change",  # no trailer
            "Change\n\nCo-Authored-By: Someone <someone@example.com>",  # not a model
            f"Change\n\n{trailer}\nCo-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>",  # two
            f"Change\n\n{trailer}\nCo-authored-by: A Person <person@example.com>",  # another co-author
        ]
        for n, message in enumerate(messages):
            self.commit({f"engine/src/m{n}.cpp": f"int m{n}() {{ }}\n"}, "i1", message)
        code, out, err = self.sigs(f"{base}..HEAD", "--json")
        self.assertEqual(code, 1, err)
        doc = json.loads(out)
        self.assertEqual(len(doc["commits"]), len(messages))
        for c in doc["commits"]:
            self.assertFalse(c["ok"])
            self.assertIn("does not name its model", c["problem"])

    def test_every_other_signature_fails(self):
        base = self.git("rev-parse", "HEAD")
        cases = [
            (None, "not signed"),
            ("vk", "not an implementer's commit key"),  # a verdict key, listed under impl-7
            ("gk", "not an implementer's commit key"),  # a commit key under a reviewer, and impl-9 with no namespace
            ("r1", "not an implementer's commit key"),
            ("late", "not an implementer's commit key"),  # listed only on the branch
        ]
        commits = []
        for n, (key, _) in enumerate(cases):
            commits.append(self.commit({f"engine/src/f{n}.cpp": f"int f{n}() {{ }}\n"}, key))
        code, out, err = self.sigs(f"{base}..HEAD", "--json")
        self.assertEqual(code, 1, err)
        doc = json.loads(out)
        self.assertEqual([c["commit"] for c in doc["commits"]], commits)
        for c, (_, why) in zip(doc["commits"], cases):
            with self.subTest(why=why):
                self.assertFalse(c["ok"])
                self.assertIn(why, c["problem"])

    def test_a_changed_signed_commit_fails(self):
        base = self.git("rev-parse", "HEAD")
        signed = self.commit({"engine/src/a.cpp": "int a() { }\n"}, "i1")
        raw = subprocess.run(
            [GIT, "-C", str(self.repo), "cat-file", "commit", signed], capture_output=True, check=True
        ).stdout
        forged = raw.replace(b"\n\nChange", b"\n\nChanged after signing")
        r = subprocess.run(
            [GIT, "-C", str(self.repo), "hash-object", "-t", "commit", "-w", "--stdin"],
            input=forged,
            capture_output=True,
            check=True,
        )
        self.git("update-ref", "HEAD", r.stdout.decode().strip())
        code, out, err = self.sigs(f"{base}..HEAD")
        self.assertEqual(code, 1, err)
        self.assertIn("does not verify for impl-1", out)

    def merge_main(self, main_files, branch_files, edit=None, conflict=False):
        """A main with its own engine commit, a branch with a signed one, and main merged into the branch unsigned,
        as a coordinator merges it; edit is written in the merge commit. The main's id."""
        base = self.git("rev-parse", "HEAD")
        self.git("checkout", "-q", "-B", f"m-{self._testMethodName}", base)
        main = self.commit(main_files, "i2")
        self.git("checkout", "-q", f"t-{self._testMethodName}")
        self.commit(branch_files, "i1")
        r = subprocess.run(
            [GIT, "-C", str(self.repo), "merge", "-q", "--no-ff", "--no-commit", main],
            capture_output=True,
            env=signing.git_env(),
            creationflags=scan.NO_WINDOW,
            check=False,
        )
        self.assertEqual(r.returncode, 1 if conflict else 0, r.stderr)
        if edit:
            self.write(*edit)
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "Merge main")
        return main

    def test_a_clean_merge_of_main_is_not_checked(self):
        main = self.merge_main({"engine/src/m.cpp": "int m() { }\n"}, {"engine/src/b.cpp": "int b() { }\n"})
        code, out, err = self.sigs(f"{main}..HEAD", "--json")
        self.assertEqual(code, 0, out + err)
        doc = json.loads(out)
        self.assertEqual([c["principal"] for c in doc["commits"]], ["impl-1"])
        self.assertEqual(doc["skipped"], 1)

    def test_a_merge_that_edits_the_engine_fails(self):
        main = self.merge_main(
            {"engine/src/m.cpp": "int m() { }\n"},
            {"engine/src/b.cpp": "int b() { }\n"},
            ("engine/src/b.cpp", "int b() { return 1; }\n"),
        )
        code, out, err = self.sigs(f"{main}..HEAD", "--json")
        self.assertEqual(code, 1, err)
        doc = json.loads(out)
        merge = self.git("rev-parse", "HEAD")
        self.assertEqual([(c["commit"], c["problem"]) for c in doc["commits"]][-1], (merge, "not signed"))

    def test_a_conflicted_merge_resolved_by_hand_fails(self):
        main = self.merge_main(
            {"engine/src/c.cpp": "int c() { return 1; }\n"},
            {"engine/src/c.cpp": "int c() { return 2; }\n"},
            ("engine/src/c.cpp", "int c() { return 3; }\n"),
            conflict=True,
        )
        code, out, err = self.sigs(f"{main}..HEAD", "--json")
        self.assertEqual(code, 1, err)
        doc = json.loads(out)
        merge = self.git("rev-parse", "HEAD")
        self.assertEqual([(c["commit"], c["problem"]) for c in doc["commits"]][-1], (merge, "not signed"))

    def test_a_bad_range_cannot_run(self):
        self.assertEqual(run_cli("cleanroom", "commit-sigs", "--repo", self.repo, "--", "--all")[0], 2)
        self.assertEqual(self.sigs("nothing..HEAD")[0], 2)

    def test_split_signed_gives_git_payload(self):
        signed = self.commit({"engine/src/a.cpp": "int a() { }\n"}, "i1")
        raw = subprocess.run(
            [GIT, "-C", str(self.repo), "cat-file", "commit", signed], capture_output=True, check=True
        ).stdout
        data, sig = codegate.split_signed(raw)
        self.assertNotIn(b"gpgsig", data)
        self.assertTrue(sig.startswith(signing.ARMOR[0]))
        self.assertEqual(codegate.split_signed(data), (data, None))


if __name__ == "__main__":
    unittest.main()
