# Progress history

How far Coney has come, one point per day of `main`: the share of the original's code that is
[reimplemented, understood and researched](index.md), measured the way the Progress page measures it. Each point is
the last commit of its day (UTC).

Nothing here is committed. The docs build reads `main`'s git history and measures each day with today's tracker, so
every day is on the same definition, and publishes the points as `progress/history.json` beside the page. A deploy
reuses the days of the previously published file and measures only the days it lacks
([how it is built](../guides/coney-tools.md#progress)). A day is left out when the tracker's inputs cannot be read at
its last commit (before the tracker existed, or in a format since replaced).

<!-- progress:history -->
