# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pr open`: release a unit as a pull request (docs/guides/pr.md#open).

The coordinator runs it once a unit has passed Review. It trusts nothing it can recompute: it checks the caller and the
unit, runs the checks of `pr verify` and `pr text` on the unit as it is (a doomed run costs seconds and leaves the
engineer's branch alone), rebases onto the remote's default branch, runs them again on the tip that will be pushed,
records the release as one `pr-opened` stamp in the local log, pushes with a lease, opens the pull request or updates
the one the branch already has, turns on auto-merge, reads the result back and records what it published. It makes no
commit and publishes no proof: the stamp log stays in the scratch folder.

Every step can be run twice: a run that failed after the push finds its pull request again and edits it, and writes no
second stamp. Every refusal names the stage to redo; the unit goes back to its engineer.

Exit codes: 0 opened or updated and read back (with `--dry-run`: every check passed); 1 refused (a stage or text
check, the claim, or a merged or closed pull request); 2 cannot run (wrong caller, `gh` missing or old, no unit, a
dirty worktree, a lock, unreadable rules: nothing was touched); 3 a rebase conflict (the branch is as it was); 4 a git
or GitHub step failed once the branch had been touched (rerun: every step is idempotent). `gh` and git are injected
(`Env`), so the tests use a local bare remote and a fake `gh`.

The code is in four modules: `pr_open_run` (the request, the machine and the state), `pr_open_unit` (the
preconditions), `pr_open_publish` (the text, the pull request, the read-back) and this one (the proof, the stamp, the
release and the command line).
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import hashlib
import json
import tempfile
from collections.abc import Sequence
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import identity, pr_open_publish, pr_open_unit, pr_proof, pr_publish, pr_text, stamps
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Claims, load_conventions
from coney_tools.issue_claim_remote import GhRemote
from coney_tools.pr_open_run import (
    EXIT_CONFLICT,
    EXIT_FAILED,
    EXIT_REFUSED,
    EXIT_UNABLE,
    PROGRESS,
    Env,
    Proof,
    Request,
    Run,
    Stop,
)
from coney_tools.pr_proof import Refusal
from coney_tools.pr_publish import GhError, PullRequest
from coney_tools.repo_checks import branch_trailer_models

__all__ = ["Env", "Request", "Stop", "add_parser", "default_env", "run", "run_open"]


# --- the machine --------------------------------------------------------------------------------------------------


def _claims_runner(args: Sequence[str]) -> str:
    """`gh` for the claim cards: `GhRemote` handles `OSError`, so a failing `gh` is raised as one, as `run_gh` does."""
    try:
        return pr_publish.real_gh(args, None)
    except GhError as error:
        raise OSError(str(error)) from error


def _claims_remote(rules: Claims) -> GhRemote:
    """The claim cards through `gh`, with the bot login and markers of `rules`."""
    return GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, _claims_runner)


def default_env() -> Env:
    """The real machine: `gh`, git, the claim cards through `gh`, the clock and the identity the hook vouched for."""
    root = find_repo_root(Path.cwd())
    rules = load_conventions(root).claims
    return Env(
        gh=pr_publish.real_gh,
        git=pr_publish.real_git,
        # Replaced by `remote_from` once the default branch's rules are read; this one only stands in until then.
        remote=_claims_remote(rules),
        remote_from=lambda base: _claims_remote(base.claims),
        clock=lambda: datetime.now(UTC),
        say=print,
        who=identity.current,
    )


# --- steps 1-5: the proof and the text --------------------------------------------------------------------------------


def _without_opened(report: stamps.Report) -> stamps.Report:
    """The log as the facts show it: without `pr-opened` stamps, so a rerun makes the same facts as the first run."""
    return stamps.Report(stamps=[s for s in report.stamps if s.step != "pr-opened"], problems=list(report.problems))


def _prove(run: Run, root: Path) -> Proof:
    """Run steps 1-5 on the worktree `root` (the unit's, or a throwaway one)."""
    assert run.unit is not None and run.task_file is not None
    subject = pr_proof.load_subject(root, run.unit, run.owner, run.task_file.kind, remote=run.request.remote)
    try:
        hashes: pr_proof.TextHashes | None = pr_text.hashes_of(run.text_path)
    except ConfigError:
        hashes = None  # the text check below says what is wrong with the file
    result = pr_proof.verify(subject, hashes)
    refusals = list(result.refusals)
    generated: pr_text.Generated | None = None
    pictures: list[tuple[str, Path, str]] = []
    models: list[str] = []
    try:
        form = pr_text.read_engineer_file(run.text_path, subject.conventions)
        models = branch_trailer_models(root, subject.base, subject.conventions.commits.trailers)
        shown = dataclasses.replace(subject, report=_without_opened(subject.report))
        generated = pr_text.generate(shown, run.task_file, form, keys=run.keys, trailer_models=models)
        refusals += generated.refusals
        pictures = [
            (image.ref, image.path, hashlib.sha256(image.path.read_bytes()).hexdigest())
            for image in form.images
            if image.path.is_file()
        ]
    except ConfigError as error:
        refusals.append(Refusal("text", f"text: {error}", "engineer"))
    return Proof(subject, refusals, generated, pictures, models, list(result.notes))


def _refuse(run: Run, proof: Proof, tip: str) -> Stop:
    """The refusals of a proof as a Stop: every problem listed under the stage to redo."""
    count = len(proof.refusals)
    return Stop(
        EXIT_REFUSED,
        f"refused: {count} problem{'s' if count != 1 else ''} in {run.task} at {tip[:12]}",
        proof.refusals,
    )


# --- step 7: the pr-opened stamp -------------------------------------------------------------------------------------


def _engineer(report: stamps.Report) -> dict[str, str]:
    """The id, type and model of the agent that wrote the `claimed` stamp."""
    claimed = report.latest("claimed")
    agent = claimed.who.get("agent") if claimed is not None else None
    return {k: str(v) for k, v in agent.items()} if isinstance(agent, dict) else {}


def _record_release(run: Run, proof: Proof, rebased_from: str, opener: str) -> stamps.Stamp:
    """Append the `pr-opened` stamp, or reuse the newest stamp when it already says exactly this."""
    assert run.unit is not None and run.task_file is not None
    subject, generated = proof.subject, proof.generated
    assert generated is not None
    covers = {
        "tip": subject.tip,
        "base": subject.base,
        stamps.CHANGE: subject.diff_sha256,
        "title_sha256": generated.text_hashes.title_sha256,
        "paragraph_sha256": generated.text_hashes.paragraph_sha256,
        "facts_sha256": generated.facts_sha256,
        "images_sha256": generated.text_hashes.images_sha256,
    }
    assert run.rules is not None
    rules = run.rules.stamps
    report = stamps.verify(run.unit.stamps, rules, task=run.owner)
    newest = report.stamps[-1] if report.stamps else None
    if (
        newest is not None
        and newest.step == "pr-opened"
        and {k: v for k, v in newest.covers.items() if k != "chain_head"} == covers
    ):
        return newest
    detail = {
        "engineer": _engineer(report),
        "opener": opener,
        "kind": subject.kind,
        "issues": list(run.task_file.issues),
        "profile": subject.profile,
        "rebased_from": rebased_from,
        "fix": run.task if run.task != run.owner else "",
    }
    return stamps.append(
        run.unit.stamps,
        rules=rules,
        task=run.owner,
        step="pr-opened",
        subject="",
        result="pass",
        commit=subject.tip,
        covers=covers,
        detail=detail,
        who=run.env.stamp_who(),
        clock=run.env.clock,
    )


# --- the run -----------------------------------------------------------------------------------------------------


def _counts(proof: Proof) -> tuple[int, int]:
    """How many tests failed first and how many `@orig` functions the release carries."""
    tests = {s.subject for s in proof.subject.report.stamps if s.step == "fails-first" and s.result == "pass"}
    return len(tests), len(pr_proof.changed_addresses(proof.subject.changed))


def _conflict(run: Run, result: pr_publish.RebaseResult, upstream: str, *, write: bool) -> Stop:
    """Stop with exit 3; `REBASE.md` (paths only) is written in the task's folder unless this is a dry run."""
    shown = ", ".join(result.paths[:5]) + (" ..." if len(result.paths) > 5 else "")
    where = ""
    if write:
        assert run.unit is not None
        lines = [
            f"# Rebase conflict: {run.task}",
            "",
            f"`git rebase {upstream}` stopped at commit `{result.commit[:12]}`.",
            "",
            "The paths git could not merge:",
            "",
            *[f"- `{path}`" for path in result.paths],
            "",
            "The branch is as it was. The engineer rebases in its worktree and resolves; the stamps the resolution",
            "made stale are then redone (`pr verify` says which).",
            "",
        ]
        (run.unit.dir / pr_publish.REBASE_FILE).write_bytes("\n".join(lines).encode("utf-8"))
        where = f" (paths in {pr_publish.REBASE_FILE})"
    return Stop(
        EXIT_CONFLICT,
        f"rebase onto {upstream} conflicts at {result.commit[:12]} in {shown}; the branch is as it was{where}",
    )


def _dry_run(run: Run, upstream: str, first: Proof, existing: PullRequest | None) -> None:
    """Try the rebase in a throwaway worktree, check the result there and say what each later step would do."""
    git = run.env.git
    folder = Path(tempfile.mkdtemp(prefix="coney-pr-open-"))
    tree = folder / "tree"
    pr_publish.out(git, run.root, "worktree", "add", "--detach", "-q", str(tree), "HEAD")
    try:
        result = pr_publish.rebase(git, tree, upstream)
        if result.status == "conflict":
            raise _conflict(run, result, upstream, write=False)
        run.say(f"dry run: rebase onto {upstream}: {result.status}")
        second = _prove(run, tree) if result.status == "rebased" else first
        if second.refusals:
            raise _refuse(run, second, result.new_tip)
    finally:
        git(run.root, ["worktree", "remove", "--force", str(tree)])
        git(run.root, ["worktree", "prune"])
        with contextlib.suppress(OSError):
            folder.rmdir()
    assert run.record is not None
    run.say(f"dry run: would append a pr-opened stamp for {second.subject.tip[:12]} and push {run.record.branch}")
    if existing is None:
        run.say(f"dry run: would open the pull request with {len(second.pictures)} picture(s)")
    else:
        verb = "reopen and update" if existing.state == "CLOSED" else "update"
        run.say(f"dry run: would {verb} #{existing.number} with {len(second.pictures)} picture(s)")
    if pr_open_publish.wants_auto_merge(run, second):
        run.say("dry run: would turn on auto-merge")
    else:
        run.say("dry run: would leave auto-merge off")
    run.say(f"dry run: would post {PROGRESS} on each issue")


def _fix_scope(run: Run, proof: Proof) -> None:
    """For a fix task, say how many files it touches since the last release, so the reviewer can judge its scope."""
    if run.task == run.owner:
        return
    last = proof.subject.report.latest("pr-opened")
    previous = last.covers.get("tip", "") if last is not None else ""
    if not previous:
        return
    # Each tip's own change (against its merge base with the default branch), so that what main changed in a rebase
    # since the last release is not counted as the fix's.
    git, upstream = run.env.git, f"{run.request.remote}/{run.default_branch}"
    before = git(run.root, ["diff", "--name-only", f"{upstream}...{previous}"])
    after = git(run.root, ["diff", "--name-only", f"{upstream}...{proof.subject.tip}"])
    if before.returncode != 0 or after.returncode != 0:
        return
    # A file the fix changes is one whose change differs between the two tips.
    differing = [
        path
        for path in sorted(set(before.stdout.split()) | set(after.stdout.split()))
        if git(run.root, ["diff", "--quiet", previous, proof.subject.tip, "--", path]).returncode != 0
    ]
    count = len(differing)
    run.say(f"  fix touches {count} file{'s' if count != 1 else ''} since the last release ({previous[:12]})")


def _release(run: Run, upstream: str, first: Proof, existing: PullRequest | None) -> None:
    """Steps 6-10 for real: rebase, check again, stamp, push, open or update, read back, record.

    Any git or `gh` failure from the rebase on is exit 4: the branch or GitHub may have been touched, so the run is
    repeated rather than started over.
    """
    try:
        _release_steps(run, upstream, first, existing)
    except GhError as error:
        raise Stop(EXIT_FAILED, f"gh: {error}; run pr open again: every step is idempotent") from error
    except ConfigError as error:
        raise Stop(EXIT_FAILED, f"{error}; run pr open again: every step is idempotent") from error


def _release_steps(run: Run, upstream: str, first: Proof, existing: PullRequest | None) -> None:
    """The body of `_release`."""
    assert run.record is not None and run.unit is not None
    env = run.env
    rebased = pr_publish.rebase(env.git, run.root, upstream)
    if rebased.status == "conflict":
        raise _conflict(run, rebased, upstream, write=True)
    proof = first
    if rebased.status == "rebased":
        run.say(f"rebased onto {upstream}: {rebased.old_tip[:12]} -> {rebased.new_tip[:12]}")
        proof = _prove(run, run.root)
        if proof.refusals:
            raise _refuse(run, proof, rebased.new_tip)
    _fix_scope(run, proof)
    opener = env.remote.current_user()
    stamp = _record_release(run, proof, rebased.old_tip if rebased.status == "rebased" else "", opener)
    tip = proof.subject.tip
    folder = pr_open_publish.task_folder(run)
    published = pr_publish.read_published(folder)
    pushed = pr_publish.push(env.git, run.root, run.request.remote, run.record.branch, tip, published.tip)
    if not pushed.ok:
        raise Stop(EXIT_FAILED, f"push: {pushed.message}")
    published.tip = tip
    pr_publish.write_published(folder, published)
    try:
        pr = pr_open_publish.publish(run, proof, existing, published)
        pr_publish.write_published(folder, published)
        want_auto = pr_open_publish.auto_merge(run, proof, pr.number)
        view = pr_open_publish.read_back(run, proof, pr, tip, want_auto)
    finally:
        # What is known is kept even when a step fails, so a rerun finds the pull request and its uploads.
        pr_publish.write_published(folder, published)
    pr_open_publish.images_from(view, proof, published)
    published.engineer = _engineer(proof.subject.report)
    published.time = env.clock().strftime("%Y-%m-%dT%H:%M:%SZ")
    pr_open_publish.progress(run, published)
    pr_publish.write_published(folder, published)
    tests, functions = _counts(proof)
    engineer = published.engineer
    who = f"{engineer.get('id', '?')}/{pr_text.human_model(engineer.get('model', '?'))}"
    verb = "updated" if existing is not None else "opened"
    run.say(
        f"{verb} #{pr.number} {pr.url} (task {run.task}, engineer {who}, {tests} tests, {functions} functions, "
        f"auto-merge {'on' if want_auto else 'off'})"
    )
    run.result.update(
        {
            "pr": {"number": pr.number, "url": pr.url, "updated": existing is not None},
            "tip": tip,
            "stamp": stamp.hash,
            "auto_merge": want_auto,
        }
    )
    clips = run.unit.dir / "clips"
    if clips.is_dir() and any(clips.iterdir()):
        run.say(f"  note: clips are waiting in {clips}; `clips publish --pr {pr.number}` posts them")


def _run(run: Run) -> None:
    """The steps in order; a Stop ends the run."""
    pr_open_unit.caller(run)
    pr_open_unit.gh_ready(run)
    pr_open_unit.find_unit(run)
    assert run.unit is not None
    # A dry run changes nothing in the unit, so it takes no lock (the lock file is in the unit's folder).
    lock = (
        contextlib.nullcontext()
        if run.request.dry_run
        else pr_open_unit.exclusive(run.unit.dir / "pr-open.lock", run.task)
    )
    with lock:
        pr_open_unit.git_state(run)
        upstream = pr_open_unit.fetch(run)
        if run.env.remote_from is not None:
            assert run.rules is not None
            run.env.remote = run.env.remote_from(run.rules)
        claims = pr_open_unit.claims(run)
        if claims:
            raise Stop(EXIT_REFUSED, f"refused: the claim is not yours to release in {run.task}", claims)
        try:
            run.repo = pr_publish.resolve_repo(run.env.gh, run.env.git, run.root, run.request.remote, run.request.repo)
            existing = pr_open_publish.find(run)
        except GhError as error:
            raise Stop(EXIT_UNABLE, f"gh: {error}") from error
        pr_open_publish.check_existing(run, existing)
        first = _prove(run, run.root)
        if first.refusals:
            raise _refuse(run, first, first.subject.tip)
        if run.request.dry_run:
            _dry_run(run, upstream, first, existing)
        else:
            _release(run, upstream, first, existing)


def _report(run: Run, code: int, message: str, refusals: Sequence[Refusal]) -> None:
    """Print the outcome: lines for a person, or one JSON object."""
    if run.request.json:
        data = {
            "ok": code == 0,
            "exit": code,
            "task": run.task,
            "dry_run": run.request.dry_run,
            "message": message,
            "refusals": [{"stage": r.stage, "text": r.text, "redo": r.redo} for r in refusals],
            "log": run.log,
            **run.result,
        }
        run.env.say(json.dumps(data, indent=2))
        return
    if message:
        run.env.say(f"pr open: {message}")
    for refusal in refusals:
        run.env.say(f"  {refusal.text}")
        run.env.say(f"      redo: {refusal.redo}")


def run_open(request: Request, env: Env) -> int:
    """Release the unit of the request; the exit code (see the module docstring)."""
    run = Run(request, env)
    try:
        _run(run)
    except Stop as stop:
        _report(run, stop.code, stop.message, stop.refusals)
        return stop.code
    except (ConfigError, GhError) as error:
        # Only the steps before the first write get here: the ones after it turn their errors into exit 4.
        _report(run, EXIT_UNABLE, str(error), ())
        return EXIT_UNABLE
    _report(run, 0, "", ())
    return 0


# --- the command line -------------------------------------------------------------------------------------------------


def add_parser(commands: Any) -> None:
    """Register `coney-tools pr open` in the `pr` group."""
    sub = commands.add_parser(
        "open",
        help="release a unit: check it, rebase, push with a lease, open or update the pull request, turn on "
        "auto-merge and read it back (docs/guides/pr.md#open)",
    )
    sub.add_argument("task", nargs="?", metavar="TASK", help="the task (default: CONEY_TASK, else the branch)")
    sub.add_argument("--worktree", type=Path, help="the unit's worktree (default: the one task start recorded)")
    sub.add_argument(
        "--text", type=Path, metavar="FILE", help="the engineer's file (default: PR.md in the task's folder)"
    )
    sub.add_argument("--remote", default="origin", help="the git remote the branch is pushed to")
    sub.add_argument(
        "--repo", metavar="OWNER/NAME", help="the repository to open it in (default: what gh makes of the remote)"
    )
    sub.add_argument("--reopen", action="store_true", help="reopen a closed, unmerged pull request of the branch")
    sub.add_argument("--no-auto-merge", action="store_true", help="open without turning on auto-merge")
    sub.add_argument(
        "--dry-run", action="store_true", help="try everything but the push and GitHub in a throwaway worktree"
    )
    sub.add_argument("--json", action="store_true", help="print the result as one JSON object")


def run(args: argparse.Namespace) -> int:
    """`pr open`: the command line of `run_open`."""
    request = Request(
        task=args.task,
        worktree=args.worktree,
        text=args.text,
        remote=args.remote,
        repo=args.repo,
        reopen=args.reopen,
        no_auto_merge=args.no_auto_merge,
        dry_run=args.dry_run,
        json=args.json,
    )
    try:
        env = default_env()
    except ConfigError as error:
        print(f"pr open: {error}")
        return EXIT_UNABLE
    return run_open(request, env)
