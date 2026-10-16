"""Tests for `coney-tools ci` (ci.py), on synthetic checkouts, workflows and archives only. No job's real tool runs:
a stand-in runner records what would run and answers with chosen exit codes."""

import hashlib
import io
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import ci, cli

SHA = "0123456789abcdef0123456789abcdef01234567"
DIGEST = "ab" * 32
PYPROJECT = '[project]\nname = "x"\nrequires-python = ">=3.42"\n'
HOOKS = "repos:\n  - repo: https://github.com/DavidAnson/markdownlint-cli2\n    rev: v9.8.7\n"


def dockerfile(node="1.2.3", node_sha=DIGEST, claude="4.5.6", uv="7.8.9"):
    return (
        f"ARG UV=ghcr.io/example/uv:{uv}@sha256:{'cd' * 32}\n"
        f"ARG NODE_VERSION={node}\nARG NODE_SHA256={node_sha}\nARG CLAUDE_CODE_VERSION={claude}\n"
    )


def checkout(root, files=None):
    """A synthetic checkout under root: the files ci reads its versions from, plus any given."""
    base = {
        "python/pyproject.toml": PYPROJECT,
        "containers/Dockerfile": dockerfile(),
        ".pre-commit-config.yaml": HOOKS,
    }
    base.update(files or {})
    for rel, text in base.items():
        path = Path(root, rel)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return Path(root)


def workflow(jobs, gather=True, permissions=True, uses=f"actions/checkout@{SHA}"):
    """A workflow text with one job per name, each running `ci --job <name>`, and the gathering job."""
    lines = ["name: ci", "on: [push]"]
    if permissions:
        lines += ["permissions:", "  contents: read"]
    lines.append("jobs:")
    for job, call in jobs:
        lines += [
            f"  {job}:",
            "    runs-on: ubuntu-24.04",
            "    steps:",
            f"      - uses: {uses} # v1",
            f"      - run: python -I python/coney-tools.py ci --job {call}",
        ]
    if gather:
        lines += [f"  {ci.GATHER}:", f"    needs: [{', '.join(j for j, _ in jobs)}]", "    steps:", "      - run: true"]
    return "\n".join(lines) + "\n"


class Versions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def test_each_from_its_own_file(self):
        repo = checkout(self.tmp.name)
        self.assertEqual(
            ci.versions(repo),
            {"python": "3.42", "node": "1.2.3", "uv": "7.8.9", "claude_code": "4.5.6", "markdownlint_cli2": "9.8.7"},
        )

    def test_lowest_python_of_a_range(self):
        repo = checkout(self.tmp.name, {"python/pyproject.toml": '[project]\nrequires-python = ">=3.12.1, <4"\n'})
        self.assertEqual(ci.python_version(repo), "3.12")

    def test_refusals(self):
        cases = [
            ({"python/pyproject.toml": "[project]\n"}, "requires-python"),
            ({"python/pyproject.toml": '[project]\nrequires-python = "~=3.11"\n'}, "lowest"),
            ({"containers/Dockerfile": dockerfile() + "ARG NODE_VERSION=9.9.9\n"}, "no single ARG NODE_VERSION"),
            ({"containers/Dockerfile": dockerfile(node="lts")}, "not a release"),
            ({"containers/Dockerfile": dockerfile().replace("@sha256:", ":latest@x:")}, "uv image"),
            ({".pre-commit-config.yaml": "repos: []\n"}, "markdownlint-cli2"),
        ]
        for files, words in cases:
            with self.subTest(words), tempfile.TemporaryDirectory() as d:
                with self.assertRaises(ci.CiError) as e:
                    ci.versions(checkout(d, files))
                self.assertIn(words, str(e.exception))


class Workflows(unittest.TestCase):
    NAMES = ("alpha", "beta")

    def problems(self, files):
        return ci.workflow_problems(files, list(self.NAMES))

    def test_in_step(self):
        self.assertEqual(self.problems({"ci.yml": workflow([("alpha", "alpha"), ("beta", "beta")])}), [])

    def test_jobs_may_be_spread_over_workflows(self):
        files = {"ci.yml": workflow([("alpha", "alpha")]), "pr.yml": workflow([("beta", "beta")], gather=False)}
        self.assertEqual(self.problems(files), [])

    def test_a_job_no_workflow_runs(self):
        self.assertEqual(self.problems({"ci.yml": workflow([("alpha", "alpha")])}), ["no workflow runs ci --job beta"])

    def test_a_workflow_job_named_otherwise(self):
        got = self.problems({"ci.yml": workflow([("alpha", "alpha"), ("other", "beta")])})
        self.assertEqual(got, ["ci.yml: job other runs ci --job beta; name the workflow job beta"])

    def test_an_unknown_job(self):
        got = self.problems({"ci.yml": workflow([("alpha", "alpha"), ("beta", "beta"), ("gamma", "gamma")])})
        self.assertEqual(got, ["ci.yml: job gamma runs ci --job gamma, which is not a job"])

    def test_gathering_job_needs_every_job(self):
        text = workflow([("alpha", "alpha"), ("beta", "beta")]).replace("needs: [alpha, beta]", "needs: [alpha, x]")
        got = self.problems({"ci.yml": text})
        self.assertIn(f"ci.yml: {ci.GATHER} does not need beta, so a failure there would not block a merge", got)
        self.assertIn(f"ci.yml: {ci.GATHER} needs x, which this workflow lacks", got)

    def test_no_gathering_job(self):
        got = self.problems({"ci.yml": workflow([("alpha", "alpha"), ("beta", "beta")], gather=False)})
        self.assertEqual(got, [f"no workflow has the {ci.GATHER} job"])

    def test_permissions_and_pins(self):
        text = workflow([("alpha", "alpha"), ("beta", "beta")], permissions=False, uses="actions/checkout@v7")
        got = self.problems({"ci.yml": text})
        self.assertIn("ci.yml: no top-level permissions: (give each workflow the least it needs)", got)
        self.assertEqual(sum("actions/checkout is not pinned to a full commit SHA" in p for p in got), 2)

    def test_local_actions_and_comments_are_left_alone(self):
        text = workflow([("alpha", "alpha"), ("beta", "beta")])
        text = text.replace("steps:\n", "steps:\n      - uses: ./local-action\n      # ci --job gamma\n", 1)
        text += "# python -I python/coney-tools.py ci --job gamma\n"
        self.assertEqual(self.problems({"ci.yml": text}), [])

    PERMS = "    permissions:\n      pages: write  # publish\n      id-token: write\n"
    DEPLOY = (
        "  deploy:\n"
        "    needs: alpha\n"
        f"{PERMS}"
        "    environment:\n"
        "      name: github-pages\n"
        "    steps:\n"
        f"      - uses: actions/deploy-pages@{SHA} # v1\n"
    )
    REFUSED = (
        "pages.yml: job deploy gets {}; only a job that runs nothing but actions/deploy-pages may get a write scope "
        "(id-token, pages at most), or one that gets issues only in a workflow started by schedule, "
        "workflow_dispatch, issues alone"
    )
    SWEEP = (
        "name: hygiene\n"
        "on:\n"
        "  schedule:\n"
        '    - cron: "0 6 * * 1"\n'
        "  workflow_dispatch:\n"
        "permissions:\n"
        "  contents: read\n"
        "jobs:\n"
        "  sweep:\n"
        "    permissions:\n"
        "      contents: read\n"
        "      issues: write\n"
        "    steps:\n"
        f"      - uses: actions/checkout@{SHA} # v7\n"
        "      - run: python groundwork.py hygiene --apply\n"
    )

    def test_an_issue_job_in_a_workflow_no_pull_request_starts(self):
        base = {"ci.yml": workflow([("alpha", "alpha"), ("beta", "beta")])}
        self.assertEqual(self.problems({**base, "hygiene.yml": self.SWEEP}), [])
        inline = self.SWEEP.replace("on:\n  schedule:\n    - cron: \"0 6 * * 1\"\n  workflow_dispatch:\n", "on: [issues]\n")
        self.assertEqual(self.problems({**base, "hygiene.yml": inline}), [])
        refused = self.REFUSED.replace("pages.yml: job deploy", "hygiene.yml: job sweep").format("issues: write")
        for on in ("  pull_request:\n", "  push:\n", "  pull_request_target:\n"):
            text = self.SWEEP.replace("  workflow_dispatch:\n", f"  workflow_dispatch:\n{on}")
            self.assertEqual(self.problems({**base, "hygiene.yml": text}), [refused], on)
        both = self.SWEEP.replace("      issues: write\n", "      issues: write\n      contents: write\n")
        want = self.REFUSED.replace("pages.yml: job deploy", "hygiene.yml: job sweep")
        self.assertEqual(self.problems({**base, "hygiene.yml": both}), [want.format("contents: write, issues: write")])

    def test_triggers(self):
        self.assertEqual(ci.triggers("on: push\n"), {"push"})
        self.assertEqual(ci.triggers('"on": [issues, schedule]  # x\n'), {"issues", "schedule"})
        self.assertEqual(ci.triggers(self.SWEEP), {"schedule", "workflow_dispatch"})
        self.assertEqual(ci.triggers("name: x\n"), set())

    def deploying(self, deploy, top="  contents: read\n"):
        """ci.yml with the gathering job, and pages.yml with the alpha and beta jobs, then the given deploy job."""
        text = workflow([("alpha", "alpha"), ("beta", "beta")], gather=False)
        text = text.replace("permissions:\n  contents: read\n", f"permissions:\n{top}" if top else "permissions: {}\n")
        return {"ci.yml": workflow([], gather=True), "pages.yml": text + deploy}

    def test_a_deploy_job_may_write_to_pages(self):
        self.assertEqual(self.problems(self.deploying(self.DEPLOY, top=None)), [])
        inline = self.DEPLOY.replace(self.PERMS, "    permissions: { pages: write, id-token: write }\n")
        self.assertEqual(self.problems(self.deploying(inline)), [])

    def test_a_write_only_on_a_job_that_only_deploys(self):
        both = self.REFUSED.format("id-token: write, pages: write")
        script = self.DEPLOY + "      - run: ./publish.sh\n"
        self.assertEqual(self.problems(self.deploying(script)), [both])
        other = self.DEPLOY + f"      - uses: actions/upload-artifact@{SHA}\n"
        self.assertEqual(self.problems(self.deploying(other)), [both])
        contents = self.DEPLOY.replace("pages: write", "contents: write")
        want = self.REFUSED.format("contents: write, id-token: write")
        self.assertEqual(self.problems(self.deploying(contents)), [want])
        everything = self.DEPLOY.replace(self.PERMS, "    permissions: write-all\n")
        self.assertEqual(self.problems(self.deploying(everything)), [self.REFUSED.format("*: write")])

    def test_top_level_permissions_grant_no_write(self):
        got = self.problems(self.deploying(self.DEPLOY, top="  contents: read\n  pages: write\n"))
        want = "pages.yml: the top-level permissions grant pages: write; grant it on the job needing it"
        self.assertEqual(got, [want])
        text = workflow([("alpha", "alpha"), ("beta", "beta")])
        text = text.replace("permissions:\n  contents: read\n", "permissions: write-all\n")
        got = self.problems({"ci.yml": text})
        self.assertEqual(got, ["ci.yml: the top-level permissions grant *: write; grant it on the job needing it"])

    def test_the_repository_workflows(self):
        """The workflows this checkout ships pass against the real job list (the `workflow` job, as a test)."""
        root = Path(__file__).resolve().parents[2]
        folder = root.joinpath(*ci.WORKFLOWS)
        if not folder.is_dir():
            self.skipTest("no workflows in this checkout")
        files = {p.name: p.read_text(encoding="utf-8") for p in folder.glob("*.yml")}
        self.assertEqual(ci.workflow_problems(files, list(ci.JOBS)), [])


class Locks(unittest.TestCase):
    LOCK = "# header\nalpha-pkg==1.0 \\\n    --hash=sha256:00\nbeta==2.0 ; python_version < '4'\n"

    def test_direct_pins_held(self):
        self.assertEqual(ci.lock_problems("# c\nAlpha_Pkg==1.0\nbeta==2.0\n", self.LOCK), [])

    def test_changed_or_missing_pin(self):
        got = ci.lock_problems("alpha-pkg==1.1\ngamma==3\n", self.LOCK)
        self.assertEqual(
            got,
            ["alpha-pkg==1.1 is 1.0 in the lock; regenerate it", "gamma==3 is missing in the lock; regenerate it"],
        )


class Runner:
    """Stands in for subprocess.run: records each command and answers with the next code for its program."""

    def __init__(self, codes=None, output=b""):
        self.calls = []
        self.codes = codes or {}
        self.output = output  # what a command whose output is read prints

    def __call__(self, argv, **kw):
        self.calls.append((list(argv), kw))
        name = Path(argv[0]).name
        out = self.output if kw.get("stdout") == subprocess.PIPE else None
        for key, code in self.codes.items():
            if key in argv or key == name:
                return SimpleNamespace(returncode=code, stdout=out)
        return SimpleNamespace(returncode=0, stdout=out)


class Jobs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = checkout(self.tmp.name)
        self.ctx = ci.Context(tools=self.repo, repo=self.repo, python="py")
        out = mock.patch("sys.stdout", new_callable=io.StringIO)
        self.out = out.start()
        self.addCleanup(out.stop)

    def test_every_job_has_steps_and_a_description(self):
        self.assertEqual(list(ci.JOBS), ["workflow", "tests", "disclosure", "markdownlint", "conventions", "site",
                                         "mod", "verify", "orig-check", "commit-sigs"])  # fmt: skip
        for job in ci.JOBS.values():
            self.assertTrue(job.what)

    def test_tests_run_unittest_in_python(self):
        run = Runner()
        self.assertEqual(ci.run_job(self.ctx, ci.JOBS["tests"], run), 0)
        argv, kw = run.calls[0]
        self.assertEqual(argv, ["py", "-I", "-B", "-m", "unittest", "discover", "-s", "tests"])
        self.assertEqual(kw["cwd"], self.repo / "python")

    def test_verify_reads_the_given_ref(self):
        run = Runner()
        ctx = ci.Context(tools=self.repo, repo=self.repo / "pr", ref="origin/main", python="py")
        ci.run_job(ctx, ci.JOBS["verify"], run)
        argv = run.calls[0][0]
        self.assertEqual(argv[3:], ["cleanroom", "verify", "--repo", str(self.repo / "pr"), "--ref", "origin/main"])
        self.assertEqual(argv[2], str(self.repo / "python" / "coney-tools.py"))

    def test_disclosure_checks_the_range_and_the_texts(self):
        run = Runner()
        ctx = ci.Context(tools=self.repo, repo=self.repo, range="a..b", texts=["t.txt", "b.md"], python="py")
        self.assertEqual(ci.run_job(ctx, ci.JOBS["disclosure"], run), 0)
        self.assertEqual(run.calls[0][0][3:], ["cleanroom", "disclosure-check", "--commits", "a..b"])
        self.assertEqual(run.calls[1][0][3:], ["cleanroom", "disclosure-check", "t.txt", "b.md"])

    def test_disclosure_default_range_and_bad_range(self):
        run = Runner()
        ci.run_job(self.ctx, ci.JOBS["disclosure"], run)
        self.assertEqual(run.calls[0][0][-1], "main..HEAD")
        self.assertEqual(len(run.calls), 1)
        ctx = ci.Context(tools=self.repo, repo=self.repo, range="--all", python="py")
        self.assertEqual(ci.run_job(ctx, ci.JOBS["disclosure"], Runner()), 2)

    def test_markdownlint_uses_the_hook_version(self):
        run = Runner(output=b"Finding: **/*.md\nLinting: 3 files\nSummary: 0 issues in 0 files\n")
        with mock.patch.object(ci.lint, "npx_program", return_value="npx"):
            self.assertEqual(ci.run_job(self.ctx, ci.JOBS["markdownlint"], run), 0)
        self.assertEqual(run.calls[0][0], ["npx", "--yes", "markdownlint-cli2@9.8.7", "**/*.md"])
        self.assertIn("Linting: 3 files", self.out.getvalue())

    def test_site_builds_into_the_given_folder(self):
        checkout(self.repo, {"requirements-docs.in": "pkg==1\n", "requirements-docs.txt": "pkg==1 \\\n  --hash=x\n"})
        for site_dir, want in ((None, self.repo / "build" / "ci" / "site"), (self.repo / "out", self.repo / "out")):
            run = Runner()
            self.ctx.site_dir = site_dir
            with mock.patch.object(ci, "environment", return_value=lambda ctx, run: (0, [])):
                self.assertEqual(ci.run_job(self.ctx, ci.JOBS["site"], run), 0)
            argv = run.calls[-1][0]
            self.assertEqual(argv[argv.index("--site-dir") + 1], str(want))

    def test_markdownlint_that_read_no_file_fails(self):
        for output in (b"Linting: 0 files\nSummary: 0 issues in 0 files\n", b""):
            with self.subTest(output), mock.patch.object(ci.lint, "npx_program", return_value="npx"):
                self.assertEqual(ci.run_job(self.ctx, ci.JOBS["markdownlint"], Runner(output=output)), 1)
        self.assertIn("it read no file, so it checked nothing", self.out.getvalue())

    def test_code_gate_jobs_read_the_given_ref(self):
        run = Runner()
        ctx = ci.Context(tools=self.repo, repo=self.repo / "pr", ref="origin/main", range="a..b", python="py")
        self.assertEqual(ci.run_job(ctx, ci.JOBS["orig-check"], run), 0)
        self.assertEqual(ci.run_job(ctx, ci.JOBS["commit-sigs"], run), 0)
        pr = str(self.repo / "pr")
        self.assertEqual(run.calls[0][0][3:], ["cleanroom", "orig-check", "--repo", pr, "--ref", "origin/main"])
        self.assertEqual(
            run.calls[1][0][3:], ["cleanroom", "commit-sigs", "--repo", pr, "--ref", "origin/main", "a..b"]
        )
        run = Runner()
        ci.run_job(self.ctx, ci.JOBS["commit-sigs"], run)
        self.assertEqual(run.calls[0][0][-1], "main..HEAD")
        self.assertEqual(ci.run_job(self.ctx, ci.JOBS["commit-sigs"], Runner({"commit-sigs": 2})), 2)

    def test_a_missing_tool_could_not_run(self):
        with mock.patch.object(ci.lint, "npx_program", side_effect=ci.lint.LintError("npx not found on PATH")):
            self.assertEqual(ci.run_job(self.ctx, ci.JOBS["markdownlint"], Runner()), 2)
        self.assertIn("could not run: npx not found on PATH", self.out.getvalue())

    def test_mod_runs_each_mod_on_the_pinned_claude_code(self):
        Path(self.repo, "mods", "one", ".claude-plugin").mkdir(parents=True)
        Path(self.repo, "mods", "not-a-mod").mkdir(parents=True)
        run = Runner()
        with mock.patch.object(ci.lint, "npx_program", return_value="npx"):
            self.assertEqual(ci.run_job(self.ctx, ci.JOBS["mod"], run), 0)
        self.assertEqual([c[0][-2:] for c in run.calls], [["validate", "mods/one"], ["test", "mods/one"]])
        argv, kw = run.calls[0]
        self.assertEqual(argv[:4], ["npx", "--yes", "@anthropic-ai/claude-code@4.5.6", "plugin"])
        self.assertEqual(kw["env"]["CLAUDE_CONFIG_DIR"], str(self.repo / "build" / "ci" / "claude-config"))

    def test_no_mod_could_not_run(self):
        with mock.patch.object(ci.lint, "npx_program", return_value="npx"):
            self.assertEqual(ci.run_job(self.ctx, ci.JOBS["mod"], Runner()), 2)

    def test_exit_codes(self):
        # a failing check fails; exit 2 of a coney-tools command is "could not run"; of another program, a failure
        for codes, want in (({"unittest": 1}, 1), ({"unittest": 2}, 1)):
            with self.subTest(codes):
                self.assertEqual(ci.run_job(self.ctx, ci.JOBS["tests"], Runner(codes)), want)
        self.assertEqual(ci.run_job(self.ctx, ci.JOBS["verify"], Runner({"verify": 2})), 2)
        self.assertEqual(ci.run_job(self.ctx, ci.JOBS["verify"], Runner({"verify": 1})), 1)

    def test_a_job_stops_at_its_first_failing_step(self):
        run = Runner({"--commits": 1})
        ctx = ci.Context(tools=self.repo, repo=self.repo, texts=["t"], python="py")
        self.assertEqual(ci.run_job(ctx, ci.JOBS["disclosure"], run), 1)
        self.assertEqual(len(run.calls), 1)

    def test_a_program_that_cannot_start(self):
        def run(argv, **kw):
            raise FileNotFoundError(argv[0])

        self.assertEqual(ci.run_job(self.ctx, ci.JOBS["tests"], run), 2)

    def test_run_jobs_summary(self):
        run = Runner({"unittest": 1, "verify": 2})
        code, lines = ci.run_jobs(self.ctx, ["tests", "verify"], run)
        self.assertEqual(code, 2)
        self.assertEqual(lines, ["FAIL          tests", "COULD NOT RUN verify"])
        code, lines = ci.run_jobs(self.ctx, ["tests"], Runner())
        self.assertEqual((code, lines), (0, ["PASS          tests"]))

    def test_site_refuses_a_stale_lock_before_installing(self):
        checkout(self.repo, {"requirements-docs.in": "pkg==2\n", "requirements-docs.txt": "pkg==1 \\\n  --hash=x\n"})
        run = Runner()
        self.assertEqual(ci.run_job(self.ctx, ci.JOBS["site"], run), 1)
        self.assertEqual(run.calls, [])
        self.assertIn("pkg==2 is 1 in the lock", self.out.getvalue())


class Environment(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = checkout(self.tmp.name, {"req.txt": "pkg==1 \\\n  --hash=sha256:00\n"})
        self.ctx = ci.Context(tools=self.repo, repo=self.repo, python="py")
        self.venv = self.repo / "build" / "ci" / "x-env"
        no_uv = mock.patch.object(ci, "uv_program", return_value=None)
        no_uv.start()
        self.addCleanup(no_uv.stop)

    def runner(self):
        """Makes the virtual environment's python when `venv` runs, as the real one would."""
        run = Runner()
        real = run.__call__

        def call(argv, **kw):
            if argv[1:4] == ["-I", "-m", "venv"]:
                py = ci.venv_python(argv[4])
                py.parent.mkdir(parents=True, exist_ok=True)
                py.write_bytes(b"")
            return real(argv, **kw)

        return call, run

    def test_made_once_then_reused_until_the_lock_changes(self):
        step = ci.environment("x", ("req.txt",))
        call, run = self.runner()
        self.assertEqual(step(self.ctx, call)[0], 0)
        self.assertEqual(run.calls[0][0][1:4], ["-I", "-m", "venv"])
        install = run.calls[1][0]
        self.assertEqual(install[0], str(ci.venv_python(self.venv)))
        self.assertIn("--require-hashes", install)
        self.assertEqual(install[-1], str(self.repo / "req.txt"))
        self.assertEqual(step(self.ctx, call), (0, ["x-env: up to date"]))
        self.assertEqual(len(run.calls), 2)
        (self.repo / "req.txt").write_text("pkg==2 \\\n  --hash=sha256:11\n", encoding="utf-8")
        step(self.ctx, call)
        self.assertEqual(len(run.calls), 4)

    def test_uv_when_it_is_on_path(self):
        step = ci.environment("x", ("req.txt",))
        run = Runner()
        with mock.patch.object(ci, "uv_program", return_value="uv"):
            self.assertEqual(step(self.ctx, run)[0], 2)  # the stand-in uv made no environment: no python in it
        self.assertEqual(run.calls[0][0], ["uv", "venv", "--quiet", "--python", "py", str(self.venv)])
        install = run.calls[1][0]
        self.assertEqual(install[:5], ["uv", "pip", "install", "--python", str(ci.venv_python(self.venv))])
        self.assertIn("--require-hashes", install)

    def test_a_failed_install_could_not_run_and_leaves_no_stamp(self):
        step = ci.environment("x", ("req.txt",))
        code, lines = step(self.ctx, Runner({"pip": 1}))
        self.assertEqual(code, 2)
        self.assertIn("exited 1", lines[0])
        self.assertFalse((self.venv / ci.STAMP).exists())


def node_archive(files, top="node-v1.2.3-linux-x64"):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:xz") as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(f"{top}/{name}" if top else name)
            info.size = len(data)
            info.mode = 0o755
            tar.addfile(info, io.BytesIO(data))
    return buf.getvalue()


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


LINUX = ("linux", "x86_64")


class InstallNode(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def repo_for(self, data):
        return checkout(Path(self.tmp.name, "repo"), {"containers/Dockerfile": dockerfile(node_sha=sha(data))})

    def test_checked_and_unpacked(self):
        data = node_archive({"bin/node": b"#!fake", "lib/x.js": b"x"})
        seen = []

        def opener(url, timeout):
            seen.append(url)
            return Response(data)

        dest = Path(self.tmp.name, "node")
        got = ci.install_node(self.repo_for(data), dest, opener, LINUX)
        self.assertEqual(got, dest / "bin")
        self.assertEqual((dest / "bin" / "node").read_bytes(), b"#!fake")
        self.assertEqual(seen, ["https://nodejs.org/dist/v1.2.3/node-v1.2.3-linux-x64.tar.xz"])

    def test_a_wrong_checksum_unpacks_nothing(self):
        data = node_archive({"bin/node": b"#!fake"})
        repo = checkout(Path(self.tmp.name, "repo"))  # the Dockerfile's checksum is for another archive
        dest = Path(self.tmp.name, "node")
        with self.assertRaises(ci.CiError) as e:
            ci.install_node(repo, dest, lambda url, timeout: Response(data), LINUX)
        self.assertIn("does not match the SHA-256", str(e.exception))
        self.assertFalse(dest.exists())

    def test_an_escaping_member_is_refused(self):
        data = node_archive({"x/../../../evil": b"x", "x/bin/node": b"n"}, top=None)
        with self.assertRaises(ci.CiError):
            ci.install_node(self.repo_for(data), Path(self.tmp.name, "node"), lambda u, timeout: Response(data), LINUX)
        self.assertFalse(Path(self.tmp.name, "evil").exists())

    def test_other_platforms_and_an_existing_folder(self):
        repo = checkout(Path(self.tmp.name, "repo"))
        with self.assertRaises(ci.CiError):
            ci.install_node(repo, Path(self.tmp.name, "n"), None, ("win32", "AMD64"))
        with self.assertRaises(ci.CiError):
            ci.install_node(repo, Path(self.tmp.name), None, LINUX)

    def test_a_failed_download(self):
        def opener(url, timeout):
            raise OSError("offline")

        with self.assertRaises(ci.CiError) as e:
            ci.install_node(checkout(Path(self.tmp.name, "repo")), Path(self.tmp.name, "n"), opener, LINUX)
        self.assertIn("cannot download", str(e.exception))


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Command(unittest.TestCase):
    SCRIPT = str(Path(__file__).resolve().parent.parent / "coney-tools.py")

    def cli(self, *args):
        return subprocess.run(
            [sys.executable, "-I", self.SCRIPT, "ci", *args], capture_output=True, text=True, check=False
        )

    def test_list(self):
        r = self.cli("--list")
        self.assertEqual(r.returncode, 0)
        self.assertEqual([line.split()[0] for line in r.stdout.splitlines()], list(ci.JOBS))

    def test_an_unknown_job_could_not_run(self):
        r = self.cli("--job", "nope")
        self.assertEqual(r.returncode, 2)
        self.assertIn("no job nope", r.stderr)

    def test_versions_of_a_synthetic_checkout(self):
        with tempfile.TemporaryDirectory() as d:
            checkout(d)
            with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
                self.assertEqual(cli.main(["ci", "--versions", "--repo", d]), 0)
        self.assertIn("python=3.42\n", out.getvalue())
        self.assertIn("claude_code=4.5.6\n", out.getvalue())


if __name__ == "__main__":
    unittest.main()
