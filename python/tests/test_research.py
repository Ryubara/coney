"""Tests for the page gate (`research verdict`, `publish`, `import` and `cleanroom verify`), on synthetic pages and
made-up keys generated in a temp folder: no real research, no real key.

Every command's output is checked for a word that appears only in a page's text: no message may carry page text.
markdownlint is a stand-in (FakeLint) that reads the rules it is given and puts the page's text in its output, as the
real one does; tests/test_lint.py has the real one behind CONEY_TEST_NPX=1.
Code-like test strings are assembled from pieces, so this file itself never matches the page rules.
"""

import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import lint, research, signing
from coney_tools.cleanroom import page_check, scan
from coney_tools.cli import main

WORD = "Zanzibarquux"  # only ever in page text
PAGE = f"""# Getting up

A knocked-down Human gets up after 2.5 s unless hit again. {WORD} is the made-up name of this rule.

## Stamina

| Field | Size |
|---|---|
| stamina | 4 |

![shot](img/shot.png)
"""
IMAGE = b"\x89PNG\r\n\x1a\nmade-up image bytes"
CAPTURE = 'source = "made-up capture"\ncaptured = "2026-10-16"\nby = "test"\n'
KEEP = {
    "sections": {
        "getting-up": {"verdict": "keep", "kind": "behaviour"},
        "stamina": {"verdict": "keep", "kind": "fact"},
    },
    "images": {"img/shot.png": "keep"},
}
SSH_KEYGEN = scan.find_program("ssh-keygen")
GIT = scan.find_program("git")


class FakeLint:
    """markdownlint's stand-in: MD013 for every line over the line_length of the rules it is given, with the line in
    its context; `mode` "garbage" makes it fail without a finding it can parse."""

    def __init__(self):
        self.rules = b"default: true\nMD013:\n  line_length: 120\n"
        self.mode = "lint"
        self.calls = []

    def source(self, ref, checkout):
        return "0.23.2", self.rules

    def run(self, cmd, cwd):
        self.calls.append(cmd)
        if self.mode == "garbage":
            return 1, f"Error: something about {WORD}"
        limit = int(re.search(rb"line_length: (\d+)", (Path(cwd) / cmd[cmd.index("--config") + 1]).read_bytes())[1])
        name = cmd[-1]
        out = [f"Finding: {name}", "Linting: 1 file"]
        for n, line in enumerate((Path(cwd) / name).read_text(encoding="utf-8").split("\n"), 1):
            if len(line) > limit:
                out.append(
                    f"{name}:{n}:{limit + 1} error MD013/line-length Line length [Expected: {limit}; Actual: "
                    f'{len(line)}] [Context: "{line[:40]}"]'
                )
        return (1 if len(out) > 2 else 0), "\n".join(out)


def pub(path):
    return " ".join(Path(f"{path}.pub").read_text(encoding="utf-8").split()[:2])


@unittest.skipUnless(SSH_KEYGEN and GIT, "needs ssh-keygen and git on PATH")
class Gate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.tmp = Path(cls._tmp.name)
        keys = cls.tmp / "keys"
        keys.mkdir()
        cls.key = {}
        for name in ("r1", "r2", "r3", "pa", "gk"):
            cls.key[name] = keys / name
            subprocess.run(
                [SSH_KEYGEN, "-q", "-t", "ed25519", "-N", "", "-C", "", "-f", str(cls.key[name])],
                check=True,
                capture_output=True,
                creationflags=scan.NO_WINDOW,
            )
        v = 'namespaces="coney-verdict"'
        allowed = (
            "# made-up keys for tests\n"
            f"page-reviewer-1 {v} {pub(cls.key['r1'])}\n"
            f"page-reviewer-2 {v} {pub(cls.key['r2'])}\n"
            f"page-reviewer-9 {v} {pub(cls.key['r1'])}\n"  # the same key as page-reviewer-1
            f"pr-analyst-1 {v} {pub(cls.key['pa'])}\n"
            f'page-reviewer-8 namespaces="git" {pub(cls.key["gk"])}\n'  # a commit key
        )
        cls.pubroot, cls.anaroot = cls.tmp / "pub", cls.tmp / "ana"
        cls.repo = cls.pubroot / "repo"
        cls.repo.mkdir(parents=True)
        (cls.anaroot / "gate").mkdir(parents=True)
        cls.git("init", "-q", "-b", "main")
        for k, val in (("user.name", "Test"), ("user.email", "test@example.invalid"), ("commit.gpgsign", "false")):
            cls.git("config", k, val)
        (cls.repo / "allowed_signers").write_text(allowed, encoding="utf-8", newline="\n")
        cls.git("add", "allowed_signers")
        cls.git("commit", "-q", "-m", "Add allowed signers")
        cls.git("checkout", "-q", "-b", "work")  # main stays free to move
        # A key that only the working tree lists: it must never count.
        with open(cls.repo / "allowed_signers", "a", encoding="utf-8", newline="\n") as f:
            f.write(f"page-reviewer-3 {v} {pub(cls.key['r3'])}\n")
        (cls.tmp / "zones.json").write_text(
            json.dumps(
                {
                    "roots": [
                        {"path": str(cls.pubroot), "role": "public"},
                        {"path": str(cls.anaroot), "role": "analyst"},
                    ],
                    "accounts": [],
                }
            ),
            encoding="utf-8",
        )
        cls.ws = cls.tmp / "ws.json"
        cls.ws.write_text(
            json.dumps(
                {
                    "version": 1,
                    "zones_file": "zones.json",
                    "paths": {
                        "repo": "public:repo",
                        "gate": "analyst:gate",
                        "from_analyst": "public:exchange\\from-ana",
                    },
                    "zones": {},
                    "analyst_ports": [],
                    "check": {"cache_minutes": 5, "schedule_minutes": 5},
                }
            ),
            encoding="utf-8",
        )
        cls.n = 0
        cls.lint = FakeLint()
        real_npx = lint.npx_program
        cls.patches = [
            mock.patch.object(lint, "source", cls.lint.source),
            mock.patch.object(lint, "run", cls.lint.run),
            mock.patch.object(lint, "npx_program", lambda given=None: real_npx(given) if given else "fake-npx"),
            mock.patch.dict(os.environ, {"CONEY_WORKSPACE": str(cls.ws)}),  # import reads its withdrawn list
        ]
        for p in cls.patches:
            p.start()

    @classmethod
    def tearDownClass(cls):
        for p in cls.patches:
            p.stop()
        cls._tmp.cleanup()

    def setUp(self):
        self.lint.rules = FakeLint().rules
        self.lint.mode = "lint"

    @classmethod
    def git(cls, *args, cwd=None):
        r = subprocess.run(
            [GIT, "-C", str(cwd or cls.repo), *args],
            capture_output=True,
            env=signing.git_env(),
            creationflags=scan.NO_WINDOW,
            check=True,
        )
        return r.stdout.decode().strip()

    # --- helpers ---------------------------------------------------------------------------------------------------

    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = main([str(a) for a in argv])
        self.assertNotIn(WORD.lower(), (out.getvalue() + err.getvalue()).lower())
        return code, out.getvalue(), err.getvalue()

    def page(self, text=PAGE, crlf=False, image=True):
        type(self).n += 1
        page_id = f"{self._testMethodName.replace('_', '-')[:40]}-{self.n}"
        folder = self.anaroot / "gate" / page_id
        (folder / "img").mkdir(parents=True)
        (folder / "page.md").write_bytes(text.replace("\n", "\r\n" if crlf else "\n").encode("utf-8"))
        if image:
            (folder / "img" / "shot.png").write_bytes(IMAGE)
            (folder / "img" / "shot.png.capture.toml").write_text(CAPTURE, encoding="utf-8")
        return folder

    def decisions(self, doc=KEEP):
        type(self).n += 1
        p = self.tmp / f"decisions-{self.n}.json"
        p.write_text(json.dumps(doc), encoding="utf-8")
        return p

    def first(self, folder, doc=KEEP, reviewer="page-reviewer-1", key="r1"):
        return self.run_cli(
            "research", "verdict", folder, "--reviewer", reviewer, "--key", self.key[key],
            "--decisions", self.decisions(doc),
        )  # fmt: skip

    def second(self, folder, reviewer="page-reviewer-2", key="r2"):
        return self.run_cli(
            "research", "verdict", folder, "--countersign", "--reviewer", reviewer, "--key", self.key[key]
        )

    def signed(self, **kw):
        folder = self.page(**kw)
        self.assertEqual(self.first(folder)[0], 0)
        self.assertEqual(self.second(folder)[0], 0)
        return folder

    def publish(self, folder):
        return self.run_cli("research", "publish", folder.name, "--workspace", self.ws)

    def bundle_of(self, folder):
        out = list((self.pubroot / "exchange" / "from-ana" / "pages").glob(f"{folder.name}-*"))
        self.assertEqual(len(out), 1)
        return out[0]

    def worktree(self, ref):
        type(self).n += 1
        wt = self.tmp / f"wt-{self.n}"
        self.git("worktree", "add", "-q", "--detach", str(wt), ref)
        return wt

    def sig_by(self, key, data):
        return signing.sign(SSH_KEYGEN, self.key[key], data)

    # --- the whole way -----------------------------------------------------------------------------------------------

    def test_happy_path(self):
        head, status = self.git("rev-parse", "HEAD"), self.git("status", "--porcelain")
        folder = self.page()
        code, out, err = self.first(folder)
        self.assertEqual(code, 0, err)
        doc = json.loads((folder / "verdict.json").read_text(encoding="utf-8"))
        self.assertEqual([s["id"] for s in doc["sections"]], ["getting-up", "stamina"])
        self.assertEqual(doc["images"][0]["path"], "img/shot.png")
        self.assertEqual(doc["checker_sha256"], page_check.checker_sha256())
        self.assertEqual((folder / "verdict.json").read_bytes(), research.canonical(doc))
        self.assertEqual(code := self.second(folder)[0], 0)
        code, out, err = self.publish(folder)
        self.assertEqual(code, 0, out + err)
        bundle = self.bundle_of(folder)
        sha = research.sha256((folder / "verdict.json").read_bytes())
        self.assertEqual(bundle.name, f"{folder.name}-{sha[:12]}")
        names = sorted(p.relative_to(bundle).as_posix() for p in bundle.rglob("*") if p.is_file())
        self.assertEqual(
            names,
            [
                "img/shot.png",
                "img/shot.png.capture.toml",
                "page.md",
                "verdict.json",
                "verdict.json.page-reviewer-1.sig",
                "verdict.json.page-reviewer-2.sig",
            ],
        )
        self.assertEqual([p.name for p in bundle.parent.iterdir() if p.name.startswith(".")], [])
        code, out, err = self.run_cli("research", "import", bundle, "--repo", self.repo)
        self.assertEqual(code, 0, out + err)
        self.assertEqual(self.git("rev-parse", "HEAD"), head)
        self.assertEqual(self.git("status", "--porcelain"), status)
        self.assertEqual(self.git("symbolic-ref", "--short", "HEAD"), "work")
        msg = self.git("log", "-1", "--format=%B", f"page/{folder.name}")
        self.assertEqual(msg, f"research: Add the {folder.name} page\n\nVerdict: {sha}")
        files = self.git("ls-tree", "-r", "--name-only", f"page/{folder.name}").splitlines()
        pid = folder.name
        for want in (
            f"research/pages/{pid}/page.md",
            f"research/pages/{pid}/img/shot.png",
            f"research/pages/{pid}/img/shot.png.capture.toml",
            f"research/verdicts/{pid}.json",
            f"research/verdicts/{pid}.page-reviewer-1.sig",
            f"research/verdicts/{pid}.page-reviewer-2.sig",
        ):
            self.assertIn(want, files)
        wt = self.worktree(f"page/{pid}")
        code, out, err = self.run_cli("cleanroom", "verify", "--repo", wt)
        self.assertEqual(code, 0, out + err)
        self.assertIn(f"PASS {pid}", out)
        # A Windows checkout turns text into CRLF: still the same page, verdict and signatures.
        for p in (wt / "research").rglob("*"):
            if p.is_file() and p.suffix != ".png":
                p.write_bytes(signing.lf(p.read_bytes()).replace(b"\n", b"\r\n"))
        self.assertEqual(self.run_cli("cleanroom", "verify", "--repo", wt)[0], 0)
        # A second import of the same page refuses: the branch exists.
        code, out, err = self.run_cli("research", "import", bundle, "--repo", self.repo)
        self.assertEqual(code, 2)
        self.assertIn("exists already", err)

    def test_crlf_page_and_update(self):
        folder = self.signed(crlf=True)
        self.assertEqual(self.publish(folder)[0], 0)
        self.assertEqual(self.run_cli("research", "import", self.bundle_of(folder), "--repo", self.repo)[0], 0)
        pid = folder.name
        blob = self.git("cat-file", "blob", f"page/{pid}:research/pages/{pid}/page.md")
        self.assertNotIn("\r", blob)
        # Merged: the page is on main. A new review of a changed page replaces its files.
        self.git("update-ref", "refs/heads/main", f"page/{pid}")
        self.git("update-ref", "-d", f"refs/heads/page/{pid}")
        shutil.rmtree(folder)
        shutil.rmtree(self.bundle_of(folder))
        text = PAGE.replace("2.5 s", "3 s").replace("![shot](img/shot.png)\n", "")
        folder.mkdir(parents=True)
        (folder / "page.md").write_text(text, encoding="utf-8", newline="\n")
        self.assertEqual(self.first(folder, {"sections": KEEP["sections"]})[0], 0)
        self.assertEqual(self.second(folder)[0], 0)
        self.assertEqual(self.publish(folder)[0], 0)
        self.assertEqual(self.run_cli("research", "import", self.bundle_of(folder), "--repo", self.repo)[0], 0)
        self.assertTrue(self.git("log", "-1", "--format=%s", f"page/{pid}").startswith("research: Update the "))
        files = self.git("ls-tree", "-r", "--name-only", f"page/{pid}").splitlines()
        self.assertNotIn(f"research/pages/{pid}/img/shot.png", files)
        self.assertEqual(self.run_cli("cleanroom", "verify", "--repo", self.worktree(f"page/{pid}"))[0], 0)

    # --- what the gate refuses ----------------------------------------------------------------------------------------

    def test_edited_page_after_signing_fails(self):
        folder = self.signed()
        with open(folder / "page.md", "a", encoding="utf-8") as f:
            f.write("\nOne more fact.\n")
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("hash differs", out)
        folder = self.signed()
        (folder / "img" / "shot.png").write_bytes(IMAGE + b"!")
        (folder / "img" / "shot.png.capture.toml").write_text(CAPTURE, encoding="utf-8")
        self.assertEqual(self.publish(folder)[0], 1)

    def test_one_signature_fails(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("1 signatures", out)

    def test_same_principal_twice_fails(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        code, _, err = self.second(folder, "page-reviewer-1", "r1")
        self.assertEqual(code, 2)
        self.assertIn("signed this verdict already", err)
        # The first signature copied under the second reviewer's name does not verify for them.
        shutil.copy(folder / "verdict.json.page-reviewer-1.sig", folder / "verdict.json.page-reviewer-2.sig")
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("does not verify for page-reviewer-2", out)

    def test_same_key_under_two_principals_fails(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        self.assertEqual(self.second(folder, "page-reviewer-9", "r1")[0], 0)
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("same key", out)

    def test_pr_analyst_never_counts(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        self.assertEqual(self.second(folder, "pr-analyst-1", "pa")[0], 2)  # refused outright
        data = (folder / "verdict.json").read_bytes()
        (folder / "verdict.json.pr-analyst-1.sig").write_bytes(self.sig_by("pa", data))
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("not from a page reviewer", out)
        (folder / "verdict.json.pr-analyst-1.sig").unlink()
        (folder / "verdict.json.page-reviewer-2.sig").write_bytes(self.sig_by("pa", data))
        self.assertEqual(self.publish(folder)[0], 1)

    def test_commit_key_fails(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        self.assertEqual(self.second(folder, "page-reviewer-8", "gk")[0], 0)  # signing itself cannot tell
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("does not verify for page-reviewer-8", out)

    def test_key_only_in_working_tree_fails(self):
        self.assertIn("page-reviewer-3", (self.repo / "allowed_signers").read_text(encoding="utf-8"))
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        self.assertEqual(self.second(folder, "page-reviewer-3", "r3")[0], 0)
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("page-reviewer-3.sig: its key is not in allowed_signers", out)

    def test_three_signatures_fail(self):
        folder = self.signed()
        data = (folder / "verdict.json").read_bytes()
        (folder / "verdict.json.page-reviewer-9.sig").write_bytes(self.sig_by("r1", data))
        self.assertEqual(self.publish(folder)[0], 1)

    def test_reject_blocks_publish(self):
        doc = json.loads(json.dumps(KEEP))
        doc["sections"]["stamina"]["verdict"] = "reject"
        folder = self.page()
        self.assertEqual(self.first(folder, doc)[0], 0)
        self.assertEqual(self.second(folder)[0], 0)
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("section 2 is rejected", out)
        self.assertEqual(list((self.pubroot / "exchange" / "from-ana").rglob(f"{folder.name}-*")), [])
        doc = json.loads(json.dumps(KEEP))
        doc["images"]["img/shot.png"] = "reject"
        folder = self.page()
        self.assertEqual(self.first(folder, doc)[0], 0)
        self.assertEqual(self.second(folder)[0], 0)
        self.assertIn("image img/shot.png is rejected", self.publish(folder)[1])

    def test_decisions_must_fit_the_page(self):
        for doc in (
            {"sections": {"getting-up": KEEP["sections"]["getting-up"]}, "images": KEEP["images"]},
            {
                "sections": {**KEEP["sections"], "elsewhere": {"verdict": "keep", "kind": "fact"}},
                "images": KEEP["images"],
            },
            {"sections": KEEP["sections"], "images": {}},
            {"sections": KEEP["sections"], "images": {**KEEP["images"], "img/other.png": "keep"}},
            {
                "sections": {**KEEP["sections"], "stamina": {"verdict": "maybe", "kind": "fact"}},
                "images": KEEP["images"],
            },
            {
                "sections": {**KEEP["sections"], "stamina": {"verdict": "keep", "kind": "fact", "note": "x"}},
                "images": KEEP["images"],
            },
            {"sections": KEEP["sections"], "images": KEEP["images"], "comment": "free text"},
        ):
            folder = self.page()
            code, _, err = self.first(folder, doc)
            self.assertEqual(code, 2, (doc, err))
            self.assertFalse((folder / "verdict.json").exists())
            self.assertEqual([p.name for p in folder.glob("*.sig")], [])

    def test_existing_verdict_refused(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        before = (folder / "verdict.json").read_bytes()
        code, _, err = self.first(folder, reviewer="page-reviewer-2", key="r2")
        self.assertEqual(code, 2)
        self.assertIn("add-only", err)
        self.assertEqual((folder / "verdict.json").read_bytes(), before)

    def test_countersign_over_changed_page_refused(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        (folder / "page.md").write_text(PAGE.replace("2.5 s", "4 s"), encoding="utf-8")
        code, out, _ = self.second(folder)
        self.assertEqual(code, 1)
        self.assertIn("section 1 differs", out)
        self.assertFalse((folder / "verdict.json.page-reviewer-2.sig").exists())
        # Nor over a verdict changed after the first signature, even one still in canonical form.
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        doc = json.loads((folder / "verdict.json").read_text(encoding="utf-8"))
        doc["images"] = []
        (folder / "verdict.json").write_bytes(research.canonical(doc))
        self.assertEqual(self.second(folder)[0], 1)
        # And a verdict that is not canonical (room for hidden text) is refused.
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        (folder / "verdict.json").write_bytes((folder / "verdict.json").read_bytes() + b"\n")
        code, out, _ = self.second(folder)
        self.assertEqual(code, 1)
        self.assertIn("canonical", out)

    def test_failing_page_gets_no_verdict(self):
        bad = "\n## Heading " + WORD + "\n\nx = " + "foo(a, b);\n"
        folder = self.page(PAGE + bad)
        code, out, _ = self.first(folder, {"sections": {}, "images": {}})
        self.assertEqual(code, 1)
        self.assertIn("page-check fails in 1 of 3 sections", out)
        self.assertFalse((folder / "verdict.json").exists())

    # --- markdownlint ------------------------------------------------------------------------------------------------

    def long_page(self):
        long = f"{WORD} " + " ".join(["made-up words"] * 12)
        self.assertGreater(len(long), 120)
        return self.page(PAGE.replace("## Stamina", f"{long}\n\n## Stamina"))

    def test_lint_blocks_verdict(self):
        folder = self.long_page()
        code, out, err = self.first(folder)
        self.assertEqual(code, 1, err)
        want = f"FAIL {folder.name}: markdownlint finds 1 problems; no verdict can be given"
        self.assertEqual(out.splitlines(), [want, "  line 5: MD013/line-length"])
        self.assertNotIn("Context", out + err)
        self.assertFalse((folder / "verdict.json").exists())
        cmd = ["--yes", "markdownlint-cli2@0.23.2", "--config", ".markdownlint.yaml", f"{folder.name}.md"]
        self.assertEqual(self.lint.calls[-1][1:], cmd)

    def test_lint_blocks_countersign_publish_and_verify(self):
        folder = self.page()
        self.assertEqual(self.first(folder)[0], 0)
        self.lint.rules = b"default: true\nMD013:\n  line_length: 60\n"  # the rules changed after the first signature
        code, out, _ = self.second(folder)
        self.assertEqual(code, 1)
        self.assertIn("markdownlint finds 1 problems; not signed", out)
        self.assertIn("line 3: MD013/line-length", out)
        self.assertFalse((folder / "verdict.json.page-reviewer-2.sig").exists())
        self.lint.rules = FakeLint().rules
        self.assertEqual(self.second(folder)[0], 0)
        self.lint.rules = b"default: true\nMD013:\n  line_length: 60\n"
        code, out, _ = self.publish(folder)
        self.assertEqual(code, 1)
        self.assertIn("markdownlint line 3: MD013/line-length", out)
        self.assertEqual(list((self.pubroot / "exchange" / "from-ana").rglob(f"{folder.name}-*")), [])
        self.lint.rules = FakeLint().rules
        self.assertEqual(self.publish(folder)[0], 0)
        bundle = self.bundle_of(folder)
        self.lint.rules = b"default: true\nMD013:\n  line_length: 60\n"
        code, out, _ = self.run_cli("research", "import", bundle, "--repo", self.repo)
        self.assertEqual(code, 1)
        self.assertIn("markdownlint line 3", out)
        self.lint.rules = FakeLint().rules
        self.assertEqual(self.run_cli("research", "import", bundle, "--repo", self.repo)[0], 0)
        wt = self.worktree(f"page/{folder.name}")
        self.assertEqual(self.run_cli("cleanroom", "verify", "--repo", wt)[0], 0)
        self.lint.rules = b"default: true\nMD013:\n  line_length: 60\n"
        code, out, _ = self.run_cli("cleanroom", "verify", "--repo", wt)
        self.assertEqual(code, 1)
        self.assertIn(f"FAIL {folder.name}: markdownlint line 3: MD013/line-length", out)

    def test_lint_could_not_run(self):
        folder = self.page()
        self.lint.mode = "garbage"
        code, _, err = self.first(folder)
        self.assertEqual(code, 2)
        self.assertIn("could not lint the page (exit 1)", err)
        self.assertFalse((folder / "verdict.json").exists())
        self.lint.mode = "lint"
        code, _, err = self.run_cli(
            "research", "verdict", folder, "--reviewer", "page-reviewer-1", "--key", self.key["r1"],
            "--decisions", self.decisions(), "--npx", self.tmp / "no-such-npx.cmd",
        )  # fmt: skip
        self.assertEqual(code, 2)
        self.assertIn("npx not found at", err)
        self.assertFalse((folder / "verdict.json").exists())

    def test_tools_missing(self):
        folder = self.page()
        code, _, err = self.run_cli(
            "research", "verdict", folder, "--reviewer", "page-reviewer-1", "--key", self.key["r1"],
            "--decisions", self.decisions(), "--ssh-keygen", self.tmp / "no-such-ssh-keygen.exe",
        )  # fmt: skip
        self.assertEqual(code, 2)
        self.assertIn("ssh-keygen not found", err)
        self.assertFalse((folder / "verdict.json").exists())

    def test_import_checks_folder_name(self):
        folder = self.signed()
        self.assertEqual(self.publish(folder)[0], 0)
        bundle = self.bundle_of(folder)
        renamed = bundle.with_name(folder.name + "-000000000000")
        bundle.rename(renamed)
        code, out, _ = self.run_cli("research", "import", renamed, "--repo", self.repo)
        self.assertEqual(code, 1)
        self.assertIn("folder's name", out)
        self.assertNotIn(f"page/{folder.name}", self.git("branch", "--list", f"page/{folder.name}"))

    def test_import_all(self):
        # An outbox of its own, so other tests' bundles are not in it.
        type(self).n += 1
        rel = f"exchange-{self.n}\\from-ana"
        ws = self.tmp / f"ws-{self.n}.json"
        doc = json.loads(self.ws.read_text(encoding="utf-8"))
        doc["paths"]["from_analyst"] = f"public:{rel}"
        ws.write_text(json.dumps(doc), encoding="utf-8")
        outbox = self.pubroot / rel.replace("\\", "/") / "pages"
        outbox.mkdir(parents=True)
        good, bad = self.signed(), self.signed()
        names = {}
        for folder in (good, bad):
            self.assertEqual(self.publish(folder)[0], 0)
            names[folder] = self.bundle_of(folder).name
            shutil.move(self.bundle_of(folder), outbox)
        with open(outbox / names[bad] / "page.md", "a", encoding="utf-8") as f:
            f.write("\nOne more fact.\n")  # changed after publishing: its import fails a check
        status = ("research", "status", "--role", "public", "--json", "--workspace", ws, "--repo", self.repo)
        code, out, _ = self.run_cli(*status)
        self.assertEqual(code, 1)
        self.assertEqual({p["id"]: p["state"] for p in json.loads(out)["pages"]}, {good.name: "new", bad.name: "new"})
        code, out, err = self.run_cli("research", "import", "--all", "--workspace", ws, "--repo", self.repo)
        self.assertEqual(code, 1, err)
        self.assertIn(f"PASS {good.name}", out)
        self.assertIn(f"FAIL {bad.name}: the page's hash differs", out)
        self.assertEqual(out.splitlines()[-1], "imported 1 of 2 new bundles")
        code, out, _ = self.run_cli(*status)
        self.assertEqual(
            {p["id"]: p["state"] for p in json.loads(out)["pages"]}, {good.name: "imported", bad.name: "new"}
        )
        # Merged: nothing new but the failing bundle; with it gone, nothing to do.
        self.git("update-ref", "refs/heads/main", f"page/{good.name}")
        shutil.rmtree(outbox / names[bad])
        code, out, _ = self.run_cli(*status)
        self.assertEqual((code, json.loads(out)["pages"][0]["state"]), (0, "merged"))
        code, out, _ = self.run_cli("research", "import", "--all", "--workspace", ws, "--repo", self.repo)
        self.assertEqual((code, out), (0, "imported 0 of 0 new bundles\n"))
        # A bundle or --all, not both or neither.
        self.assertEqual(self.run_cli("research", "import", "--repo", self.repo)[0], 2)
        self.assertEqual(self.run_cli("research", "import", outbox, "--all", "--workspace", ws)[0], 2)

    def test_import_refuses_withdrawn(self):
        type(self).n += 1
        rel = f"exchange-{self.n}\\from-ana"
        outbox = self.pubroot / rel.replace("\\", "/") / "pages"
        outbox.mkdir(parents=True)
        by_verdict, by_page = self.signed(), self.signed()
        bundles = {}
        for folder in (by_verdict, by_page):
            self.assertEqual(self.publish(folder)[0], 0)
            bundles[folder] = outbox / self.bundle_of(folder).name
            shutil.move(self.bundle_of(folder), outbox)

        def hashes(folder):
            data = (bundles[folder] / "verdict.json").read_bytes()
            return hashlib.sha256(data).hexdigest(), json.loads(data)["page_sha256"]

        ws = self.tmp / f"ws-{self.n}.json"
        doc = json.loads(self.ws.read_text(encoding="utf-8"))
        doc["paths"]["from_analyst"] = f"public:{rel}"
        doc["withdrawn"] = [
            {"verdict_sha256": hashes(by_verdict)[0], "why": "made up"},
            {"page_sha256": hashes(by_page)[1], "why": "made up"},
        ]
        ws.write_text(json.dumps(doc), encoding="utf-8")
        status = ("research", "status", "--role", "public", "--json", "--workspace", ws, "--repo", self.repo)
        code, out, _ = self.run_cli(*status)
        self.assertEqual(code, 0)
        self.assertEqual({p["id"]: p["state"] for p in json.loads(out)["pages"]}, dict.fromkeys(
            (by_verdict.name, by_page.name), "withdrawn"
        ))  # fmt: skip
        code, out, _ = self.run_cli("research", "import", "--all", "--workspace", ws, "--repo", self.repo)
        self.assertEqual((code, out), (0, "imported 0 of 0 new bundles\n"))
        for folder in (by_verdict, by_page):
            code, out, _ = self.run_cli("research", "import", bundles[folder], "--workspace", ws, "--repo", self.repo)
            self.assertEqual(code, 1)
            self.assertIn(f"FAIL {bundles[folder].name}: withdrawn", out)
            self.assertNotIn(f"page/{folder.name}", self.git("branch", "--list", f"page/{folder.name}"))
        # Imported before it was withdrawn: the public role must act on it.
        doc["withdrawn"] = []
        ws.write_text(json.dumps(doc), encoding="utf-8")
        code, _, err = self.run_cli("research", "import", bundles[by_page], "--workspace", ws, "--repo", self.repo)
        self.assertEqual(code, 0, err)
        doc["withdrawn"] = [{"page_sha256": hashes(by_page)[1], "why": "made up"}]
        ws.write_text(json.dumps(doc), encoding="utf-8")
        code, out, _ = self.run_cli(*status)
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(out)["action"], [by_page.name])
        self.assertEqual(json.loads(out)["counts"]["withdrawn-imported"], 1)
        # A malformed entry: the workspace cannot load.
        doc["withdrawn"] = [{"page_sha256": "ABC", "why": "made up"}]
        ws.write_text(json.dumps(doc), encoding="utf-8")
        code, _, err = self.run_cli(*status)
        self.assertEqual(code, 2)
        self.assertIn("withdrawn[0].page_sha256", err)

    def test_fingerprint_is_the_keys(self):
        listed = subprocess.run(
            [SSH_KEYGEN, "-l", "-E", "sha256", "-f", f"{self.key['r1']}.pub"],
            capture_output=True,
            check=True,
            creationflags=scan.NO_WINDOW,
        ).stdout.decode()
        self.assertEqual(signing.key_fingerprint(self.sig_by("r1", b"x\n")), listed.split()[1])

    # --- cleanroom verify --------------------------------------------------------------------------------------------

    def test_verify_catches_strays_and_missing_verdicts(self):
        folder = self.signed()
        self.assertEqual(self.publish(folder)[0], 0)
        self.assertEqual(self.run_cli("research", "import", self.bundle_of(folder), "--repo", self.repo)[0], 0)
        pid = folder.name
        wt = self.worktree(f"page/{pid}")
        verdicts = wt / "research" / "verdicts"
        (verdicts / "ghost.json").write_text("{}", encoding="utf-8")
        code, out, _ = self.run_cli("cleanroom", "verify", "--repo", wt)
        self.assertEqual(code, 1)
        self.assertIn(f"PASS {pid}", out)
        self.assertIn("research/verdicts/ghost.json: stray", out)
        (verdicts / "ghost.json").unlink()
        lonely = wt / "research" / "pages" / "lonely"
        lonely.mkdir()
        (lonely / "page.md").write_text(PAGE.replace("![shot](img/shot.png)\n", ""), encoding="utf-8")
        code, out, _ = self.run_cli("cleanroom", "verify", "--repo", wt)
        self.assertEqual(code, 1)
        self.assertIn("FAIL lonely: no verdict", out)
        shutil.rmtree(lonely)
        (wt / "research" / "pages" / pid / "extra.png").write_bytes(IMAGE)
        code, out, _ = self.run_cli("cleanroom", "verify", "--repo", wt)
        self.assertEqual(code, 1)
        self.assertIn("stray file extra.png", out)
        (wt / "research" / "pages" / pid / "extra.png").unlink()
        (verdicts / f"{pid}.page-reviewer-2.sig").unlink()
        self.assertEqual(self.run_cli("cleanroom", "verify", "--repo", wt)[0], 1)

    def test_verify_reads_allowed_signers_from_the_ref(self):
        folder = self.signed()
        self.assertEqual(self.publish(folder)[0], 0)
        self.assertEqual(self.run_cli("research", "import", self.bundle_of(folder), "--repo", self.repo)[0], 0)
        wt = self.worktree(f"page/{folder.name}")
        (wt / "allowed_signers").write_text("# nothing\n", encoding="utf-8")  # the working tree's copy never counts
        self.assertEqual(self.run_cli("cleanroom", "verify", "--repo", wt)[0], 0)
        code, _, err = self.run_cli("cleanroom", "verify", "--repo", wt, "--ref", "no-such-ref")
        self.assertEqual(code, 2)
        self.assertIn("no allowed_signers", err)


class Pieces(unittest.TestCase):
    """The parts that need no tools."""

    def test_fingerprint_matches_ssh_keygen_format(self):
        with self.assertRaises(signing.BadSignature):
            signing.key_fingerprint(b"not a signature")

    def test_image_paths(self):
        for good in ("a.png", "img/a.png"):
            self.assertTrue(research.image_path_ok(good))
        for bad in ("../a.png", "/a.png", "img\\a.png", "C:/a.png", "./a.png", 3):
            self.assertFalse(research.image_path_ok(bad))

    def test_schema_refuses_extra_fields(self):
        doc = {"schema": research.SCHEMA, "page": "p", "page_sha256": "0" * 64, "checker_sha256": "0" * 64,
               "sections": [], "images": []}  # fmt: skip
        self.assertEqual(research.schema_problems(doc), [])
        self.assertTrue(research.schema_problems({**doc, "note": "free text"}))
        bad = {**doc, "sections": [{"id": "a", "sha256": "0" * 64, "verdict": "keep", "kind": "fact", "why": "x"}]}
        self.assertTrue(research.schema_problems(bad))


if __name__ == "__main__":
    unittest.main()
