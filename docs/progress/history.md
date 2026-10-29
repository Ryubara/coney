# Progress history

How far Coney has come, one point per day of `main`: the share of the original's functions that is
[reimplemented, understood and researched](index.md), counted by function the way the Progress page counts it. Each
point is the last commit of its day (UTC).

Nothing here is committed. The docs build reads `main`'s git history and measures each day with today's tracker, so
every day is on the same definition ([how it is built](index.md#history), and how a deploy reuses the published days:
[the progress commands](../guides/coney-tools-references.md#progress)). A day is left out when the tracker's inputs
cannot be read at its last commit (before the tracker existed, or in a format since replaced).

**Counting by function:** the maintainer decided on 2026-10-29 that progress is counted by function, not bytes
([the decision](index.md#by-function)). Because every day is measured again with today's definition, the whole
history is by function and no day keeps an older byte figure. A day before the Ghidra listing was committed
(2026-10-07) has no total number of functions to count against: its row shows the number of ports recorded and `n/a`
for the shares, and the chart starts at the first day with a listing.

<!-- progress:history -->
