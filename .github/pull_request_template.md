<!-- markdownlint-disable-file MD041 -- a pull request body starts at the second heading level -->
## Summary

<!-- One short paragraph: what this changes and why.
     Call whoever tested or filed it "the user" and whoever decides "the maintainer", never "the owner".
     If the change needs no documentation, add a line `Docs: none`
     (CI refuses a change under src/ or python/src/ without a docs change, unless this line is here).
     If this finishes an issue, add a line `Fixes #<number>` (one per issue): merging then closes it. -->

## Issues affected

<!-- Every open issue this change partly fixes, changes the behaviour of or makes out of date, one line each on how:
     `- #123: its step 2 is done`. An issue this change finishes is `Fixes #N` in Summary and is not listed here.
     When no open issue is affected, replace the list with one line, `None (<reason>)`, with any short reason:
     `None (one-line fix)`. `coney-tools issue impact` lists candidate issues from the diff if you have the tools;
     it is a helper, not a requirement. After the merge the coordinator edits each issue listed. -->

- ...

## Checklist

<!-- A pull request merges only when every task in it is finished: list what it finishes, each item ticked, and
     CI refuses an unticked `- [ ]` item. Work this pull request does not finish is not part of it and is not
     listed here; it lives in its own issue. An issue this pull request resolves gets a `Fixes #N` line in
     Summary. A track's working status lives in its scratch STATUS.md, not here. -->

Done:

- [x] ...

## Testing

<!-- What was run and what came out: the commands (build preset, `ctest`, `pytest`, `mkdocs build --strict`), the
     tests added or changed, any disc, playthrough or trace-diff run, and the results as counts, pass or fail and
     numbers ("412 passed, 0 failed", "playthrough reached checkpoint 4 in 1,830 updates"). For a docs-only or
     research change, say which checks ran and what runtime evidence (PCSX2 traces, memory reads) backs the claims.
     Work that cannot be tested yet is listed as such, with the reason.

     A title area a player sees (`gameplay`, `graphics`, `audio`) needs one line starting `Runtime:` in this section,
     and CI refuses the pull request without it: the evidence from the original running, with numbers (a trace diff,
     an event diff, or named PCSX2 captures or reads compared with Coney's; docs/guides/testing.md#done), or
     `Runtime: none (<reason>)` when the original cannot say anything about this change. The reviewer opens the log,
     trace or capture behind it in the scratch folder and refuses a reason that is only convenience.
     Any other title area does not need the line: delete it, or keep it when the original running says something
     about the change. (`coney-tools pr text` writes the other Testing lines itself, from the stamps.) -->

- ...
- Runtime: ...

## Screenshots

<!-- For anything a player can see, show it: Coney before, Coney after and the original
     (PCSX2) from the same place. Write each picture as a Markdown image, `![alt](./shots/file.png)`; it is
     uploaded with `gh pr create --body-file ... --attach ./shots/file.png` (gh 2.99+), or drag a PNG into this
     box on GitHub. A short video clip goes up with `coney-tools clips publish`, which deletes it afterwards
     (docs/guides/testing.md#clips). Never commit it (LEGAL.md). An agent keeps its captures in
     scratch/<branch>/shots/ and writes the image references with paths relative to that folder; the coordinator
     attaches them when opening the pull request. The text never names a path on your machine (no scratch/...,
     ../../..., C:\...): say "the scratch folder"; only these image references may be local.
     When nothing visible changes, replace the three lines with one: `Screenshots: none (<reason>)`, for example
     `Screenshots: none (no visible change)`. -->

- **Coney before:** ...
- **Coney after:** ...
- **Original (PCSX2):** ...

## AI use

<!-- Who wrote the work in this pull request's commits (code, docs, tests), not who opened the pull request. -->

- **Commits written by:** <!-- a person / an AI / an AI, edited by a person / a person, with AI help -->
- **Model:** <!-- the model that wrote them, for example Claude Opus 5.5; n/a when a person wrote them -->
