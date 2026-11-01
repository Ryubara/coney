# The body hash and a moved function

How `coney-tools orig` decides that an `@orig` function **changed** (the body hash) and that it only **moved** (a second
hash that leaves its own qualified name out). Everything that reads units, `orig review-stamp`, `orig check-review`,
`task review` and `pr verify`, uses these two, and `orig check-tags` and the touch-it rule of `docs check` read the
same moves for every function ([Any function](#any-function)). The commands are on [the fidelity
page](coney-tools-fidelity.md#orig).

## The body hash {#body-hash}

A unit's `body_sha256` is the SHA-256 of its definition's **code**, which is what decides that a function changed
(`review-stamp`, `check-review`, the ledger, a `compared` stamp's `coney_body_sha256`, `task review` and `pr verify`
all read this one hash, `orig_review.code_text` and `_body_hash`); [a moved function](#moved) is told by a second hash
that leaves its own qualified name out.

* **Left out:** every comment (`//` to the end of the line, also over a trailing backslash, and `/* ... */` across any
  number of lines) and all white space: indentation, trailing blanks, line breaks, the spacing around an operator.
  Repointing a research link in a comment, rewording one, or re-wrapping a line is not a change of the function, and
  neither `task review` nor `pr verify` asks for a new `orig compare` for it.
* **Kept:** every token. A string, a raw string and a character literal are read whole before any comment is looked
  for, so a `//` or `/*` inside one is code and editing it changes the hash; so does a space inside a literal. A
  single space stays where its absence would join two tokens (`return x`, `a + +b` against `a ++b`), because there
  it is code.
* **Not a comment:** commenting code out removes tokens, so it changes the hash; the code is gone from the function.

**Stamps and records made under the old hash** (the lines of the definition, trailing blanks dropped) are read by
rehashing, not migrated, so none is invalidated when the hash changes and no stamp or ledger line is rewritten:

* A `compared` stamp whose `coney_body_sha256` is not the hash now is checked against the function's code at the
  stamp's own `commit`: it stands when that hash is the old-style hash of the function at that commit and the new-style
  hash of the function there equals the one now. So an unchanged function keeps its stamp, a comment edit after the
  stamp keeps it too, and any edit of the code refuses it ("no current `compared` stamp") until `orig compare` is
  run again. A stamp whose commit is gone from the repository (a garbage-collected branch) cannot be rehashed and is
  refused the same way; run `orig compare` again.
* A ledger entry made under the old hash counts while the function's lines are exactly as they were then. After any
  edit it is stale, as before, and the function is reviewed again.
* An `ORIG-REVIEW.json` verdict is not rehashed: it lives as long as its branch, and one written before this change is
  refused as "reviewed at another body"; the reviewer writes it again from `review-stamp`.

## A moved function {#moved}

Moving an `@orig` function into another class, renaming the class or the function, or putting it in another file
changes its key and, for an out-of-class definition, the line that names its class, so its body hash changes while its
logic does not. That is not a new function: a function that **keeps its code and its tags** keeps the study and the
compare it already had, and is not asked for new ones. `orig_review.moves` reads the units at the base and at the tip
and calls a unit with a new key a **move** when one unit that is gone at the tip (taken over once, so a copy is not a
move) has all of:

* the same `@orig` addresses, in both builds, and the same names in its tags;
* the same code, so either the same body hash, or the same **moved hash** (`Unit.moved_sha256`, `_moved_hash`): the
  [body hash](#body-hash) of the definition, and of its separate declaration (the header's text up to the `;`: a
  default argument, `virtual`, `static`, a return type), with every `Scope::` that leads to its own class dropped
  wherever it stands (`ns::Mind::`, `Mind::`, also inside the body: `Mind::kStep`) and every use of its own name, the
  declarator and a recursive call, replaced by one placeholder. A body or a declaration that now calls, reads or
  names anything else (`Other::kStep`, a changed literal or operator, a changed default argument) has another moved
  hash and stays changed. A function renamed to the name of a helper it called (`alpha` calling `beta` becomes `beta`
  calling `beta`) stays changed, because that name is now its own; a rename that also renames its own recursive call
  is a move; a recursive call left under the old name stays changed. A string, raw string or character literal is
  data and is left as it is: a message `"Brain::think failed"` that becomes `"Mind::think failed"` is a changed
  literal, not a rewritten name.

The hashes read **names as text**, not as what they resolve to, so a program still cannot tell some changes from a
move: a body that calls another function whose meaning changed with the move (an unqualified call or member that now
resolves in another class, so that the same name reaches other code). It also cannot tell whether the gone function
ever had a compare: a legacy function that was never compared is cleared by a move as well, and the review ledger,
which is by key, starts it as unreviewed. So a moved function is not left unread: `task review` opens the
[orig checklist](review/orig.md#procedure) for each one, and its one step is to diff the two copies and their
declarations and write a finding if anything but names differs; the compare is run again when it does. When two new
functions have the code of one gone function, the first takes its study and compare and the second stays changed.

A moved function is in neither `functions` nor `removed` of `review-stamp --branch`, so `orig check-review` and
`task review` ask for no verdict and no `compared` stamp of it, and `pr verify` lists it as a note
(`Mind::think (0x00100010) moved from Brain::think (src/ai/brain.cpp:3) with its code and tags as they were: it keeps
its study and compare, so none is asked`) instead of refusing it as never studied. A function whose body, address or
tag changed in the same branch is changed, moved or not: it owes a fresh compare as any changed function does, and so
does one whose old copy is still in the code. A function that is a move in one branch and edited in the next is checked
against the branch's base, so the edit is a change. The review ledger is by key, so a moved function starts there as
unreviewed.

### Any function, tagged or not {#any-function}

The same rule decides that any function only moved, for the two checks that judge a change by what it adds: [`orig
check-tags`](coney-tools-fidelity.md#orig) and the touch-it rule of [the standards](coney-tools-standards.md#standards)
(which `docs check` runs). `orig_review.function_moves` reads every definition and every separate declaration at the
base and now, tagged or not, and calls one whose file, qualified name and parameter types are new a move of one that is
gone under its own when `moves` would: the same body hash or moved hash, the same tags, each gone function taken over
once. Two more things must be as they were, since a function without a tag has no address to be told by: its
**standing** (its marker state, `@orig`, `@coney-only`, the older wording or none, from its own comment, its
declaration's or its file's, and whether a comment stands above it) and, for an untagged function, its own name. So a
function moved with `git mv`, into another class or to another file or folder keeps the standing it had:

* `orig check-tags` counts it as known, as it does a function of a renamed file: it was in the fidelity audit's backlog,
  or needed no marker in a Coney-only folder (`src/platform/` to `src/gamemodes/`, the class renamed). It joins the
  backlog of `orig index` there, not the gate.
* The touch-it rule leaves its lines out of what the change touched, so it owes neither the comment and tag rules nor a
  comparison (a moved `@orig` function the function database lists as `legacy` is not asked for one), and a new file
  that only took it in is held for the rest. Deleting the moved code from its old place is part of the move: a pure
  deletion whose lines are each blank, nothing but comment (as a scan of the old file's comments reads it: a line in
  a block comment is one, while `*p = 5;` or `/* x */ a = 9;` is code) or a line of a function that moved away does
  not touch the line it sits at. git may align the deletion on equal lines of the function above (its `return a;`
  and `}` taken for the moved function's), so a deleted code line also counts as the moved code's when an equal line
  of it stayed in place; each such line counts once, so a code line deleted beside the move still touches its
  function.

A function whose body, declaration, tags or marker changed on the way, one renamed without a tag, and a copy whose old
function is still there are new work as before. A class that is new, or renamed, is a touched type: its own comment is
owed.
