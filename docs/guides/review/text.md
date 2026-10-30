# Description review checklist

For a reviewer of one pull request description, the way a careful maintainer would read it on GitHub. A description is
two files ([the generated sections](task.md#generated)): the engineer's `PR.md` (the title on line 1, then the summary
paragraph, the `Runtime:` line, Issues affected and the pictures), and `PR-BODY.md` beside it, which `coney-tools pr
text` generates from `PR.md`, `TASK.md` and the stamps: the `Fixes` lines, the Checklist, the facts of Testing and AI
use. The packet's `text.generated_sections` lists the generated ones and `text.generated_body` the file. `coney-tools pr
check-body` and the [tool part of `task review`](task.md#tool-part) have already checked the structure; the reviewer
checks that the description is **true**: every picture shows what the text says, and every claim is backed by the diff
or a log. Report findings in the [format on the index page](index.md#finding-format) and never edit. The rules are in
[What a pull request shows](../testing.md#pr-evidence), the [Runtime line](../testing.md#runtime-line) and the
`pull_requests` section of [.github/conventions.yml](repo:.github/conventions.yml).

The reviewer reads only: `git diff`, `git log`, `git show` and the logs in the scratch folder (their ends, or with
grep); it changes no worktree, commits nothing and does not touch GitHub.

## Checklist

- [ ] **Read the description and the change.** `PR.md` whole, then `PR-BODY.md` (the packet says where it is and
  whether `pr text` has written it), then `git diff --stat <base>...HEAD`, `git log <base>..HEAD` and the diff of
  every file a claim is about.
- [ ] **Judge each part in the file that has it.** `pr open` refuses the generated sections in `PR.md`, so never fail
  `PR.md` for lacking the Checklist, the Testing facts, AI use or the `Fixes` lines (that was wrong on #1057, #1058,
  #1062 and #1063). Judge them in `PR-BODY.md` instead: each Checklist item is finished by the diff, the `Fixes` lines
  name issues the change finishes, and AI use names the models of the commits' trailers. Its stage lines are the
  stamps' as of the last `pr text` (`Review: not reviewed` before this review is no finding). When `PR-BODY.md` is not
  there yet (`exists` is false), judge `PR.md` alone, say in the report that the generated sections were not read, and
  ask for `pr text`.
- [ ] **Open every picture.** Use the Read tool on each image the description shows (paths are relative to `PR.md`'s
  folder), every one, before judging it; for a video, open the stills, or say it could not be checked (a blocker when
  the video is the only evidence). For each, record what it shows and check that:
    - it is the subject the text names: gameplay when the text says gameplay, not a letterboxed scene, a loading or
      black screen, a menu, a legal or title screen, the desktop or an error box;
    - it is the right level, place, character or menu the caption names;
    - a before and after pair differ where the change says they should (byte-identical files are refused by a
      program; also flag two shots that look the same where the text claims a visible change);
    - the frame, settings or options the text states (render size, camera, checkpoint, update number) fit what is
      seen.
- [ ] **Each claim in Summary and Testing is backed** by the diff or a log the description cites: test names exist in
  the diff or the tree, counts ("412 passed") appear in a log, a stated behaviour is in the code, a command named
  would run. A claim with nothing behind it is Important.
- [ ] **Each test stamped `keeps` is accounted for.** The packet's `tests` list says which added tests pass before the
  change as well as after it ([Tests that keep behaviour](task.md#keeps)). The Summary must say that the change keeps
  that behaviour (a refactor, a performance change, or a pin of what already matches the original) and name the tests
  that guard it; the Testing facts that `pr text` writes list each as kept behaviour. A `keeps` test the Summary does
  not account for, or one whose Summary describes a fix or new behaviour, is Important.
- [ ] **The `Runtime:` line is true** when the title's area is one a player sees (`gameplay`, `graphics`, `audio`:
  `pull_requests.runtime`); a program has only checked that the line exists and has numbers or a reason
  ([the runtime comparison](../testing.md#runtime-comparison)):
    - with evidence: open the log, trace diff output or captures it names in the scratch folder, and check that the
      numbers it quotes are in them, that they compare the **original** with Coney (a PCSX2 trace or read next to
      Coney's, not Coney against itself or against the research page), and that they cover the behaviour the change is
      about. A line with no log, trace or capture behind it is Important;
    - with `none (<reason>)`: a reason that is only convenience ("not checked", "no time", "the copy was busy", "hard
      to capture") when PCSX2 copies and the save states were available is Important; a reason that says why the
      original cannot show it (a refactor whose trace diff is unchanged, a Coney-only debug feature) is fine;
    - the before and after pictures have a view of the original from the same spot, or a stated reason why not.
- [ ] **The rules a program cannot check:**
    - `Fixes #N` and `Refs #N` (in `PR-BODY.md`) fit the change (`gh issue view N` is read only and allowed); a `Fixes`
      line for work the change does not finish is Important;
    - **Issues affected** is believable: for a change that is more than a one-line fix, a `None (<reason>)` whose
      reason is only "none" or "n/a" is a Minor finding, and when the diff plainly touches what an open issue cites
      (run `coney-tools issue impact --base origin/main --head HEAD`, read the top matches, `gh issue view N` read
      only) and the section omits it, that is Important; a one-line fix by hand with `None (one-line fix)` passes;
    - nothing is left for later: no "Left for later", "Remaining", "TODO", "follow-up" or "next step" in the body (a
      pull request merges only when every task in it is finished, [Issues](../how-we-work.md#issues));
    - `Docs: none` only when the change touches no behaviour, command or finding a doc describes;
    - AI use (in `PR-BODY.md`) is filled and names the model that wrote the commits (their `Co-Authored-By` trailers);
    - no game data, decompiler output or local path in the text, and "user", never "owner", for the person.

## Severity examples

- **Critical:** game data or decompiler output in the description or a picture it shows.
- **Important:** a picture that shows a loading screen where the text says gameplay; a count that appears in no log; a
  `Runtime:` line with no evidence behind it, or a convenience reason when the original was available; a `Fixes` line
  for unfinished work; "left for later" in the body; a missing or wrong model in AI use.
- **Minor:** wording ("owner"); a caption that could say more; a local path in a log excerpt.

## The release check that predates `task review` {#legacy-verdict}

One of the [legacy release checks](../how-we-work.md#pr-review), for a branch started before the cutover outside a task;
it is retired at the cutover, and a task's description is judged in `task review` instead.
`coney-tools legacy pr-review PR.md --worktree <worktree>` runs the reviewer on one description and then `legacy
check-pr-review`. The reviewer starts from `repo review-stamp PR.md --worktree <worktree>`, which prints the `tip`, the
description's `body_sha256` and each local image's `sha256`, and refuses (title and body reasons, no stamp) a
description that CI would refuse: then the reviewer writes no verdict and the author fixes the description first. It
copies the stamp, never computes a hash, and writes `REVIEW.json` and `REVIEW.md` next to `PR.md`:

```json
{
  "tip": "<from the stamp>",
  "body_sha256": "<from the stamp>",
  "verdict": "pass",
  "images": [{"path": "./shots/after.png", "sha256": "<from the stamp>", "shows": "<what you saw>"}],
  "findings": [{"severity": "blocker", "where": "Screenshots", "text": "after.png shows a letterboxed scene, not gameplay"}]
}
```

- `verdict` is `pass` only when no finding is a `blocker`, otherwise `fail`; a `note` is advice that does not hold the
  release. A finding the checklist calls Critical or Important is a `blocker` here.
- `images` lists every image of the stamp, each with its `path` and `sha256` from the stamp and a short `shows`.
- `findings` is `[]` when there is nothing to say; `where` names the section (or `Title`), and `text` says what is wrong
  and what would fix it, in one or two sentences.

`REVIEW.md` is the same verdict for a person: the verdict on the first line, one line per picture (what it shows), the
findings, and which claims were checked against what. The gate that reads the file is [`legacy
check-pr-review`](../how-we-work.md#pr-review).
