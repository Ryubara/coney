# SPDX-License-Identifier: GPL-3.0-or-later
"""The publishing steps of `coney-tools pr open`: write the text, open or update the pull request, turn on auto-merge,
read it back and post the progress (docs/guides/pr.md#open).

The steps use the run's state (`pr_open_run.Run`) and the proof of the tip being pushed (`pr_open_run.Proof`). The
generated files and the record of what was published live in the task's folder; `gh` runs in the folder of the
engineer's file, where the pictures' `./shots/x.png` and the clips' `./clips/x.mp4` paths are.

A clip needs one step more than a picture (docs/guides/testing.md#clips). `gh --attach` swaps the reference for the
upload's address in place, but GitHub draws an MP4 as a player only from its bare address alone on its line, and what
`gh` leaves there (an image, a link or the bare address) is not to be relied on. So the body is read back, each clip's
line is put in its placed form (`place_clips`: the bare address for an MP4, an image for a GIF) and the body is edited
once more without `--attach`. Each clip is deleted only once the read-back has found it uploaded in place.
"""

from __future__ import annotations

import re
from collections.abc import Sequence
from pathlib import Path

from coney_tools import pr_publish
from coney_tools.config import ConfigError
from coney_tools.conventions import ClipRule, PullRequests
from coney_tools.pr_open_run import (
    EXIT_FAILED,
    EXIT_REFUSED,
    EXIT_UNABLE,
    MERGERS,
    PROGRESS,
    UPLOAD_BODY,
    Run,
    Stop,
)
from coney_tools.pr_publish import PullRequest
from coney_tools.pr_ready import Proof
from coney_tools.pr_text import BODY_FILE, TITLE_FILE, Clip
from coney_tools.repo_checks import check_body, clip_label, image_targets

#: An upload's address on a line, in whatever form `gh` left it: an image, a link or bare.
_ADDRESS = re.compile(r"https?://[^\s)<>\"']+")
#: A second-level heading of the body.
_HEADING = re.compile(r"^##[ \t]+(?P<name>\S.*?)[ \t]*$")


def placed_clip(clip: Clip, url: str) -> str:
    """The line that shows `clip` uploaded at `url`: its bare address for an MP4 (GitHub draws a player only for that),
    an image for a GIF (it plays inline and loops)."""
    return url if clip.video else f"![{clip.alt}]({url})"


def _clip_lines(body: str, rule: ClipRule) -> dict[str, int]:
    """The index of the line after each bare clip label line of the clips' section (the line its clip stands on, blank
    lines skipped), by label."""
    lines = body.split("\n")
    found: dict[str, int] = {}
    inside = False
    for index, line in enumerate(lines):
        heading = _HEADING.match(line)
        if heading:
            inside = heading.group("name").casefold() == rule.section.casefold()
            continue
        labelled = clip_label(line, rule) if inside else None
        if labelled is None or labelled[1]:
            continue
        follow = index + 1
        while follow < len(lines) and not lines[follow].strip():
            follow += 1
        if follow < len(lines):
            found[labelled[0]] = follow
    return found


def clip_addresses(body: str, clips: Sequence[Clip], rule: ClipRule) -> dict[str, str]:
    """The upload address on each clip's line of `body`, by the clip's reference; a clip still shown by its local path
    (not uploaded, or the upload appended elsewhere) has none."""
    lines = body.split("\n")
    where = _clip_lines(body, rule)
    found: dict[str, str] = {}
    for clip in clips:
        if clip.label in where:
            address = _ADDRESS.search(lines[where[clip.label]])
            if address is not None:
                found[clip.ref] = address.group(0)
    return found


def place_clips(body: str, clips: Sequence[Clip], rule: ClipRule) -> str:
    """`body` with each uploaded clip's line in its placed form (`placed_clip`), whatever form `gh` left it in."""
    lines = body.split("\n")
    where = _clip_lines(body, rule)
    addresses = clip_addresses(body, clips, rule)
    for clip in clips:
        if clip.ref in addresses:
            lines[where[clip.label]] = placed_clip(clip, addresses[clip.ref])
    return "\n".join(lines)


def clip_placement_problems(body: str, clips: Sequence[Clip], rule: ClipRule) -> list[str]:
    """What is wrong with the clips in a read-back `body`: a clip whose line holds no upload, or not in its placed
    form."""
    lines = body.split("\n")
    where = _clip_lines(body, rule)
    addresses = clip_addresses(body, clips, rule)
    problems = []
    for clip in clips:
        if clip.ref not in addresses:
            problems.append(f"the {clip.label} clip is not uploaded on its line")
        elif lines[where[clip.label]].strip() != placed_clip(clip, addresses[clip.ref]):
            problems.append(f"the {clip.label} clip is not {'alone on its line' if clip.video else 'an image'}")
    return problems


def task_folder(run: Run) -> Path:
    """The task's scratch folder: where the generated files and `pr-published.json` are kept."""
    assert run.unit is not None
    return run.unit.dir


def write_text(run: Run, proof: Proof, published: pr_publish.Published) -> tuple[Path, list[str]]:
    """Write the title and body in the task's folder; the body file to give `gh` and the pictures and clips to upload.

    A picture an earlier run uploaded is shown by its address and not uploaded again, and so is a clip whose file is
    the one uploaded (or is gone, because that run deleted it), in its placed form (`placed_clip`).
    """
    generated = proof.generated
    assert generated is not None
    folder = task_folder(run)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / TITLE_FILE).write_bytes((generated.title + "\n").encode("utf-8"))
    body = generated.body
    (folder / BODY_FILE).write_bytes(body.encode("utf-8"))
    attach: list[str] = []
    known = False
    for ref, _path, digest in proof.pictures:
        if digest in published.images:
            body = body.replace(f"]({ref})", f"]({published.images[digest]})")
            known = True
        else:
            attach.append(ref)
    for clip, digest in proof.clips:
        record = published.clips.get(clip.ref)
        if record is not None and digest in ("", record["sha256"]):
            body = body.replace(f"![{clip.alt}]({clip.ref})", placed_clip(clip, record["url"]))
            known = True
        else:
            attach.append(clip.ref)
    if not known:
        return folder / BODY_FILE, attach
    (folder / UPLOAD_BODY).write_bytes(body.encode("utf-8"))
    return folder / UPLOAD_BODY, attach


def find(run: Run) -> PullRequest | None:
    """The branch's pull request, read from GitHub."""
    assert run.record is not None
    return pr_publish.find_pr(run.env.gh, run.repo, run.record.branch)


def check_existing(run: Run, existing: PullRequest | None) -> None:
    """Refuse a merged or closed pull request, and a fix task with no open pull request to fix."""
    fix = run.task != run.owner
    if fix:
        if existing is None or existing.state != "OPEN":
            why = f"#{existing.number} is {existing.state.lower()}" if existing else "the branch has none"
            raise Stop(EXIT_REFUSED, f"{run.task} has no open pull request to fix ({why})")
        return
    if existing is None:
        return
    if existing.state == "MERGED":
        raise Stop(EXIT_REFUSED, f"#{existing.number} already merged this branch; new work is a new task")
    if existing.state == "CLOSED" and not run.request.reopen:
        raise Stop(EXIT_REFUSED, f"#{existing.number} was closed; --reopen to continue it")


#: A full commit id, as `gh pr view --json headRefOid` prints it.
_COMMIT_ID = re.compile(r"[0-9a-f]{40}")


def check_adopt(run: Run, existing: PullRequest | None) -> None:
    """With `--adopt <head>`: Stop(2), before anything is touched, unless there is a pull request no `pr open` has
    pushed (no tip in `pr-published.json`) and its branch is at that head on the remote. The head then becomes the
    lease of the push, once; the push itself still refuses a branch that moves in between."""
    head = run.request.adopt
    if head is None:
        if run.request.adopt_rewrite:
            raise Stop(EXIT_UNABLE, "--adopt-rewrite goes with --adopt <head>; it changes nothing alone")
        return
    if not _COMMIT_ID.fullmatch(head):
        raise Stop(
            EXIT_UNABLE,
            f"--adopt takes the full 40-digit head of the pull request (gh pr view N --json headRefOid), not {head!r}",
        )
    if existing is None:
        raise Stop(EXIT_UNABLE, "--adopt: the branch has no pull request to take over; leave --adopt out")
    recorded = pr_publish.read_published(task_folder(run)).tip
    if recorded:
        raise Stop(
            EXIT_UNABLE,
            f"--adopt: pr open already pushed #{existing.number} (pr-published.json records {recorded[:12]}); "
            "leave --adopt out",
        )
    assert run.record is not None
    remote, branch = run.request.remote, run.record.branch
    there = pr_publish.remote_tip(run.env.git, run.root, remote, branch)
    if there != head:
        raise Stop(
            EXIT_UNABLE,
            f"--adopt {head[:12]}: {remote}/{branch} is at {there[:12] or 'nothing'}; read the head again "
            f"(gh pr view {existing.number} --json headRefOid)",
        )
    lacking = _commits_the_unit_lacks(run, head)
    if lacking and not run.request.adopt_rewrite:
        shown = "; ".join(f"{sha[:12]} {subject}" for sha, subject in lacking[:5])
        raise Stop(
            EXIT_UNABLE,
            f"--adopt: the pull request's branch has {len(lacking)} commit(s) whose change the unit's branch lacks "
            f"({shown}); the push would drop them. Have the engineer bring them into the unit (cherry-pick), "
            "or add --adopt-rewrite when dropping them is intended",
        )
    run.say(f"taking over #{existing.number} at {head[:12]}, which no pr open pushed")


def _commits_the_unit_lacks(run: Run, head: str) -> list[tuple[str, str]]:
    """The commits of the pull request's branch (at `head`) whose change the unit's branch does not hold, as (id,
    subject). Compared by patch (`git cherry`), not by ancestry: a branch rebased or amended past its remote head
    still holds the head's changes. Fetches the pull request's branch (that updates its remote-tracking ref, never a
    local branch)."""
    assert run.record is not None
    git, remote, branch = run.env.git, run.request.remote, run.record.branch
    fetched = git(run.root, ["fetch", "--quiet", "--no-tags", remote, f"refs/heads/{branch}"])
    if fetched.returncode != 0:
        raise Stop(EXIT_UNABLE, f"--adopt: cannot fetch {remote}/{branch}: {fetched.stderr.strip() or 'failed'}")
    cherry = git(run.root, ["cherry", f"refs/heads/{branch}", head])
    if cherry.returncode != 0:
        raise Stop(EXIT_UNABLE, f"--adopt: git cherry failed: {cherry.stderr.strip() or 'no message'}")
    lacking: list[tuple[str, str]] = []
    for line in cherry.stdout.splitlines():
        if line.startswith("+ "):
            sha = line[2:].strip()
            subject = git(run.root, ["log", "-1", "--format=%s", sha]).stdout.strip()
            lacking.append((sha, subject))
    return lacking


def manual_paths(run: Run, proof: Proof) -> list[str]:
    """The changed paths that are the maintainer's to merge (`merge.manual_paths` of the default branch's rules)."""
    assert run.rules is not None
    return run.rules.merge.manual_among(proof.subject.paths)


def wants_auto_merge(run: Run, proof: Proof) -> bool:
    """Whether auto-merge should be on: not declined, allowed by the default branch's rules, not a change that is the
    maintainer's to merge (only while `pull_requests.auto_merge_manual_paths` is off), and allowed by the signed-in
    account's permission."""
    assert run.rules is not None
    merge = run.rules.merge
    if run.request.no_auto_merge or not merge.auto_merge:
        return False
    held_back = [] if run.rules.pull_requests.auto_merge_manual_paths else manual_paths(run, proof)
    if held_back:
        shown = ", ".join(held_back[:3]) + (" ..." if len(held_back) > 3 else "")
        run.say(f"  auto-merge left off: the change touches {shown}, which the maintainer merges")
        return False
    user = run.env.remote.current_user()
    if run.env.remote.permission(user) not in MERGERS:
        run.say(f"  auto-merge left off: {user} may not turn it on here")
        return False
    return True


def guard_auto_merge(run: Run, existing: PullRequest | None, want: bool) -> None:
    """Before anything is pushed or edited: auto-merge that is on for the branch's pull request while this release must
    not be merged by it is turned off and read again; Stop(4) when it stays on. Otherwise a failed step between the
    push and the read-back would leave the new tip to merge by itself. A closed one (`--reopen`) is checked too, in case
    GitHub kept its auto-merge request across the close; a merged one is never released onto."""
    if want or existing is None or existing.state == "MERGED":
        return
    gh = run.env.gh
    if not pr_publish.view_pr(gh, run.repo, existing.number).auto_merge:
        return
    run.say(f"  auto-merge is on for #{existing.number} but should be off here; turning it off before the push")
    pr_publish.disable_auto_merge(gh, run.repo, existing.number)
    if pr_publish.view_pr(gh, run.repo, existing.number).auto_merge:
        raise Stop(
            EXIT_FAILED,
            f"auto-merge on #{existing.number} stays on and this release must not be merged by it; nothing was "
            "pushed: turn it off on GitHub, then run pr open again",
        )


def auto_merge(run: Run, number: int, want: bool) -> bool:
    """Turn auto-merge on when `want` (from `wants_auto_merge`); whether it is on."""
    if not want:
        return False
    assert run.rules is not None
    pr_publish.enable_auto_merge(run.env.gh, run.repo, number, run.rules.merge.methods[0])
    return True


def publish(run: Run, proof: Proof, existing: PullRequest | None, published: pr_publish.Published) -> PullRequest:
    """Create the pull request, or update (and reopen) the branch's one; its address and uploads are remembered."""
    generated = proof.generated
    assert generated is not None and run.record is not None
    body_file, attach = write_text(run, proof, published)
    gh, repo, cwd = run.env.gh, run.repo, run.text_path.parent
    if existing is None:
        base = run.default_branch
        pr = pr_publish.create_pr(gh, repo, base, run.record.branch, generated.title, body_file, attach, cwd)
    else:
        pr = existing
        if existing.state == "CLOSED":
            pr_publish.reopen_pr(gh, repo, existing.number)
        pr_publish.edit_pr(gh, repo, pr.number, generated.title, body_file, attach, cwd)
    published.number, published.url = pr.number, pr.url
    return pr


def _readback_problems(
    proof: Proof, rules: PullRequests, view: pr_publish.View, tip: str, want_auto: bool
) -> tuple[list[str], list[str]]:
    """What is wrong with the pull request on GitHub: (problems a new body fixes, problems nothing here fixes)."""
    assert proof.generated is not None
    fixable: list[str] = []
    if view.title != proof.generated.title:
        fixable.append("the title differs")
    paths = list(proof.subject.paths)
    # When it was opened, as CI's `body` job passes it: a rule that began later does not ask it.
    checked = check_body(view.body, rules, proof.models, view.title, paths, view.opened)
    fixable += [f"body: {text}" for text in checked]
    for marker in (rules.markers.summary, rules.markers.facts):
        if f"<!-- {marker} -->" not in view.body or f"<!-- /{marker} -->" not in view.body:
            fixable.append(f"the {marker} block is damaged")
    # A GIF clip is an image once placed; an MP4 is a bare address, which is no image.
    expected = len(proof.pictures) + sum(1 for clip, _ in proof.clips if not clip.video)
    if len(image_targets(view.body)) != expected:
        fixable.append(f"{len(image_targets(view.body))} pictures shown, {expected} expected")
    if rules.clips is not None:
        fixable += clip_placement_problems(view.body, [clip for clip, _ in proof.clips], rules.clips)
    hard: list[str] = []
    if view.head != tip:
        hard.append(f"the pull request head is {view.head[:12]}, not the pushed tip {tip[:12]}")
    if want_auto and not view.auto_merge:
        hard.append("auto-merge is off")
    if not want_auto and view.auto_merge:
        hard.append("auto-merge is on")
    return fixable, hard


#: The body with the clips placed, which `place` sends without `--attach`.
PLACED_BODY = "PR-BODY-placed.md"


def place(run: Run, proof: Proof, pr: PullRequest, view: pr_publish.View) -> pr_publish.View:
    """Put each uploaded clip's line of the read-back body in its placed form and send the body again (no `--attach`:
    nothing is uploaded twice); the view read after it, or `view` when every clip already stood placed."""
    assert run.rules is not None and proof.generated is not None
    rule = run.rules.pull_requests.clips
    if rule is None or not proof.clips:
        return view
    placed = place_clips(view.body, [clip for clip, _ in proof.clips], rule)
    if placed == view.body:
        return view
    run.say("  placing the clips: each MP4 as its bare address alone on its line, each GIF as an image")
    path = task_folder(run) / PLACED_BODY
    path.write_bytes(placed.encode("utf-8"))
    pr_publish.edit_pr(run.env.gh, run.repo, pr.number, proof.generated.title, path, [], run.text_path.parent)
    return pr_publish.view_pr(run.env.gh, run.repo, pr.number)


def read_back(
    run: Run,
    proof: Proof,
    pr: PullRequest,
    tip: str,
    want_auto: bool,
    published: pr_publish.Published | None = None,
) -> pr_publish.View:
    """Read the pull request back and place its clips (`place`); repair the body once if it is wrong, then Stop(4)
    with its address if still wrong. The repair sends the text again with the uploads `published` knows by their
    address and every other picture and clip attached."""
    assert run.rules is not None
    gh = run.env.gh
    view = place(run, proof, pr, pr_publish.view_pr(gh, run.repo, pr.number))
    fixable, hard = _readback_problems(proof, run.rules.pull_requests, view, tip, want_auto)
    if fixable:
        run.say(f"  the description on GitHub is wrong ({'; '.join(fixable[:3])}); repairing it once")
        assert proof.generated is not None
        body, attach = write_text(run, proof, published if published is not None else pr_publish.Published())
        pr_publish.edit_pr(gh, run.repo, pr.number, proof.generated.title, body, attach, run.text_path.parent)
        view = place(run, proof, pr, pr_publish.view_pr(gh, run.repo, pr.number))
        fixable, hard = _readback_problems(proof, run.rules.pull_requests, view, tip, want_auto)
    if "auto-merge is off" in hard:
        pr_publish.enable_auto_merge(gh, run.repo, pr.number, run.rules.merge.methods[0])
    elif "auto-merge is on" in hard:
        # A fix round or a person left it on, and GitHub keeps it across a push: this release must not be merged by it.
        run.say("  auto-merge is on but should be off here; turning it off")
        pr_publish.disable_auto_merge(gh, run.repo, pr.number)
    if "auto-merge is off" in hard or "auto-merge is on" in hard:
        view = pr_publish.view_pr(gh, run.repo, pr.number)
        fixable, hard = _readback_problems(proof, run.rules.pull_requests, view, tip, want_auto)
    if fixable or hard:
        raise Stop(
            EXIT_FAILED, f"read-back: {pr.url} is wrong: {'; '.join(fixable + hard)}; run pr open again to repair it"
        )
    return view


def images_from(view: pr_publish.View, proof: Proof, published: pr_publish.Published) -> None:
    """Remember the address of each picture's upload (the body shows them in the order of the file, before the GIF
    clips, which are images too)."""
    targets = image_targets(view.body)
    if len(targets) == len(proof.pictures) + sum(1 for clip, _ in proof.clips if not clip.video):
        for (_ref, _path, digest), target in zip(proof.pictures, targets[: len(proof.pictures)], strict=True):
            published.images[digest] = target


def clips_from(run: Run, view: pr_publish.View, proof: Proof, published: pr_publish.Published) -> None:
    """Remember the address of each clip's upload, with its file's hash (a clip uploaded by an earlier run and deleted
    since keeps the hash recorded then)."""
    assert run.rules is not None
    rule = run.rules.pull_requests.clips
    if rule is None:
        return
    addresses = clip_addresses(view.body, [clip for clip, _ in proof.clips], rule)
    for clip, digest in proof.clips:
        old = published.clips.get(clip.ref, {}).get("sha256", "")
        if clip.ref in addresses and (digest or old):
            published.clips[clip.ref] = {"sha256": digest or old, "url": addresses[clip.ref]}


def delete_clips(proof: Proof, published: pr_publish.Published) -> list[Path]:
    """Delete each clip file whose upload the read-back found in place (the record holds its address for this very
    file), as `clips publish` does once gh reports success; the files deleted. A clip not uploaded is kept."""
    deleted: list[Path] = []
    for clip, digest in proof.clips:
        record = published.clips.get(clip.ref)
        if digest and record is not None and record["sha256"] == digest and clip.path.is_file():
            clip.path.unlink()
            deleted.append(clip.path)
    return deleted


def progress(run: Run, published: pr_publish.Published) -> None:
    """Post `/progress pr-opened` on each issue once (the claim Action then shows the pull request)."""
    assert run.task_file is not None
    if published.progress_posted:
        return
    try:
        for number in run.task_file.issues:
            run.env.remote.post_command(number, PROGRESS)
        published.progress_posted = True
    except ConfigError as error:
        run.say(f"  warning: /progress pr-opened was not posted ({error}); a rerun posts it")
