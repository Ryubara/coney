# The body hash and a moved function

How `coney-tools orig` decides that an `@orig` function **changed** (the body hash) and that it only **moved** (a second
hash that leaves its own qualified name out). Everything that reads units, `orig review-stamp`, `orig check-review`,
`task review` and `pr verify`, uses these two. The commands are on [the fidelity page](coney-tools-fidelity.md#orig).

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
  [body hash](#body-hash) of the definition with every `Scope::` that leads to its own class dropped wherever it stands
  (`ns::Mind::`, `Mind::`, also inside the body: `Mind::kStep`) and its own name dropped at its declarator. A body that
  now calls, reads or names anything else (`Other::kStep`, another function, a changed literal or operator) has another
  moved hash and stays changed. A string, raw string or character literal is data and is left as it is: a message
  `"Brain::think failed"` that becomes `"Mind::think failed"` is a changed literal, not a rewritten name.

The two hashes read **names as text**, not as what they resolve to, so a program cannot tell some changes from a move:
a recursive call left under the function's old name (`think` after a rename to `ponder`), and an unqualified call or
member that now resolves in another class, leave the hash as it was and read as a move. The reviewer reads the diff of
a moved function's two copies for that, as for any change of a name, and the compare is run again when it shows one.
When two new functions have the code of one gone function, the first takes its study and compare and the second stays
changed.

A moved function is in neither `functions` nor `removed` of `review-stamp --branch`, so `orig check-review` and
`task review` ask for no verdict and no `compared` stamp of it, and `pr verify` lists it as a note
(`Mind::think (0x00100010) moved from Brain::think (src/ai/brain.cpp:3) with its code and tags as they were: it keeps
its study and compare, so none is asked`) instead of refusing it as never studied. A function whose body, address or
tag changed in the same branch is changed, moved or not: it owes a fresh compare as any changed function does, and so
does one whose old copy is still in the code. A function that is a move in one branch and edited in the next is checked
against the branch's base, so the edit is a change. The review ledger is by key, so a moved function starts there as
unreviewed.
