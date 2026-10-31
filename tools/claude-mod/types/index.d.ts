// SPDX-License-Identifier: GPL-3.0-or-later
// The contract of the Conclave mod (docs/guides/conclave.md): the shape of `coney-tools conclave snapshot --json`
// (python/src/coney_tools/conclave_snapshot.py writes it) and the values the mod keeps in `$.state`.
// A snapshot of a higher `schema` than the mod's SCHEMA (src/snapshot.ts) is refused, never drawn.

/** A section that could not be read holds only its `error`. */
export type Read<T> = ({ error: null } & T) | { error: string }

/** What the watchers act by (conclave.watch in .github/conventions.yml): the mod holds none of these itself. */
export type WatchConfig = {
  /** The agent types of the sessions that are woken, toasted and held to the stop guard. */
  roles: string[]
  /** The first line of a wake. */
  preamble: string
  /** Seconds between GitHub reads in a session that watches, pane open or not. */
  github_seconds: number
  /** At least this long between two wakes. */
  wake_gap_seconds: number
  /** A wake waits this long for a draft in the prompt box, then goes anyway. */
  defer_typing_minutes: number
  /** A wake the engine refused is tried again after this long. */
  resend_minutes: number
  /** The stop guard blocks at most this many turn ends in a row for one reason ... */
  stop_max_blocks: number
  /** ... and counts again from one after this long without a block. */
  stop_reset_minutes: number
  /** The first line of the stop guard's reason. */
  stop_preamble: string
}

export type Config = {
  /** Why the hook settings could not be read; the lists are then empty and the mod hides no agent type. */
  error: string | null
  source: string
  roles: string[]
  helpers: string[]
  require_role: boolean
  claude_code: string
  intervals: { local: number; github: number; github_idle: number }
  /** Under it the pane is one clipped summary line; from it the narrow layout. */
  pane_min_columns: number
  /** From it the compact layout. */
  pane_compact_columns: number
  /** From it the wide layout. */
  pane_wide_columns: number
  band_narrow_columns: number
  stale_after_reads: number
  merged_visible_minutes: number
  claim_red_hours: number
  watch: WatchConfig
}

export type StepState = 'pass' | 'fail' | 'none' | 'missing'

export type StepRow = { step: string; state: StepState; time: string | null }

export type UnitBuild = {
  state: 'running' | 'waiting'
  preset: string | null
  lane: string
  step?: string | null
  slot?: number | null
  since?: string | null
  position?: number | null
}

/**
 * A unit's `task check` as `coney-tools coord status` reads its progress file (python/src/coney_tools/check_progress.py):
 * the words are Python's, so the mod draws `short` on a row and `text` in its detail and compares nothing.
 */
export type UnitCheck = {
  /** `abandoned` is also what a reading says of a run whose process is gone. */
  state: 'waiting' | 'running' | 'passed' | 'failed' | 'abandoned'
  /** The check (or checks) that run and the kind of work: `pytest`, `build`, `ctest`. */
  check: string
  kind: string
  /** The check's place in the run's list (`index` of `total`). */
  index: number
  total: number
  elapsed: number
  /** A running check whose fraction has not moved for `conclave.check_stall_minutes`. */
  stalled: boolean
  /** The place in a slot queue while it waits. */
  queue: { lane: string; place: number; total: number; minutes: number; held_by: string[] } | null
  /** `pytest 64% 50m`, `queued 5/6 ~36m`: the unit row's form. */
  short: string
  /** `check 6 of 8, pytest: running 50 min, 64% done`: the full form, for the row's detail. */
  text: string
}

export type UnitPr = {
  number: number
  state: 'open' | 'merged'
  ci: string | null
  causes: string[]
  merged_at: string | null
}

export type Overlap = { with: string; kind: 'unit' | 'claim'; scope: string[] }

export type Unit = {
  task: string
  /** Why the task file could not be read; the row then holds no stage. */
  error: string | null
  issues: number[]
  kind: string | null
  claimant: string | null
  claimed_at: string | null
  scope: string[]
  goal: string
  branch: string | null
  worktree: string | null
  busy: boolean
  stage: string | null
  stage_since: string | null
  failed: string | null
  steps: StepRow[]
  agent: { id: string; type: string } | null
  stamp_problems: number
  build: UnitBuild | null
  last_build: { state: string | null; exit_code: number | null; ended: string | null } | null
  /** The unit's `task check` while it goes on, and for an hour after it ended; null when there is none. */
  check: UnitCheck | null
  overlaps: Overlap[]
  pr: UnitPr | null
}

export type SlotClaim = { slot: number; agent: string; worktree: string; preset: string; step: string }

export type Lane = {
  slots: { slot: number; stale: boolean; claim: SlotClaim | null }[]
  queue: { position: number; stale: boolean; agent: string; worktree: string; preset: string }[]
}

export type ClaimRow = {
  issue: number
  title: string | null
  claimant: string
  status: string
  task: string | null
  scope: string[]
  stage: string | null
  pr: number | null
  claimed_at: string
  last_activity: string
  /** When the claim is released if nothing happens; null while it waits (a paused label). */
  deadline: string | null
  waiting: string | null
  warned: boolean
  adopted: boolean
}

export type PrRow = {
  number: number
  branch: string
  draft: boolean
  mergeable: string | null
  auto_merge: boolean
  ci: string
  causes: string[]
}

export type IssueRow = {
  number: number
  title: string
  status: string | null
  priority: string | null
  assignees: string[]
}

/**
 * `check-failed`, `pr-stuck` and `conflict-fix` are pull request problems (`guard`: the stop guard holds the turn end
 * for them); `conflict-fix` carries the command that starts the fix; `disk-low` is a watched drive under its floor.
 */
export type EventKind =
  | 'check-failed'
  | 'pr-stuck'
  | 'conflict-fix'
  | 'merged'
  | 'unit-ready'
  | 'question'
  | 'disk-low'

/** One event Coney raises for the coordinator. The id names the occurrence: the same thing read again has the same id. */
export type EventRow = {
  id: string
  kind: EventKind
  /** `handled` while a hold covers a pull request problem; an event that cannot come back leaves the list instead. */
  state: 'open' | 'handled'
  ref: { pr: number | null; task: string | null; branch: string | null }
  since: string | null
  /** What happened, in one line. */
  text: string
  /** What to do about it. */
  do: string
  guard: boolean
  /** Who holds a handled problem, and until when. */
  handled_by: string | null
}

/** One watched drive: what Coney keeps on it, its free space and the limits it is judged by. */
export type DriveRow = { name: string; holds: string[]; free_gb: number; min_free_gb: number; warn_free_gb: number }

export type Snapshot = {
  schema: number
  time: string
  checkout: string
  github: boolean
  config: Config
  units: Read<{ units: Unit[] }>
  builds: Read<{ lanes: Record<string, Lane> }>
  /**
   * The watched drives: `drive`, `free_gb` and the two limits are those of the one with the least room above its own
   * floor, which the band shows; `drives` lists every one, `problems` the drives that could not be read.
   */
  machine: Read<{
    drive: string
    free_gb: number
    min_free_gb: number
    warn_free_gb: number
    drives: DriveRow[]
    problems: string[]
  }>
  claims: Read<{
    account: string | null
    rules: { warn_hours: number; deadline_hours: number; enforced: boolean }
    claims: ClaimRow[]
  }> | null
  prs: Read<{ prs: PrRow[]; merged: { number: number; branch: string; merged_at: string }[] }> | null
  issues: Read<{ issues: IssueRow[] }> | null
  /** From a GitHub read only. `unread` names the kinds whose source could not be read: their absence says nothing. */
  events: Read<{ events: EventRow[]; unread: string[]; holds_problems: string[] }> | null
}

/** What the mod holds of the snapshot: the sections of the last good local and GitHub reads, merged. */
export type Held = {
  snapshot: Snapshot | null
  /** `$.clock.now()` of the last good local read, 0 before one. */
  localAt: number
  githubAt: number
  /** The first line of the last failed local or GitHub read, null after a good one. */
  error: string | null
  githubError: string | null
  /** The schema of a snapshot this mod refused for being newer, else null. */
  newer: number | null
}

/** An event the watchers have seen: toasted when first seen, woken for once (`wokeAt` 0 until then). */
export type SeenEvent = { id: string; kind: string; seenAt: number; wokeAt: number }

/** What the watchers remember across sessions (`$.store`, key `watch`): the events told, and the wake's pacing. */
export type Watched = {
  seen: SeenEvent[]
  /** `$.clock.now()` of the last wake that was taken. */
  lastWakeAt: number
  /** A wake the engine refused waits until then. */
  retryAt: number
}

/** The stop guard's count of turn ends it blocked in a row for one reason. */
export type StopState = { reason: string; blocks: number; lastBlockAt: number }

/** The agent types the model is offered, frozen for the session so the listing never changes under the cache. */
export type Offer = { allowed: string[] }

declare module 'claude-code' {
  interface PluginState {
    conclave: {
      held: Shaped<Held>
      /** The unit or claim row that is open on the pane (`task:<name>` or `#<issue>`), null for none. */
      open: string | null
      /** `$.clock.now()` as the mod's own clock last read it; redraws the countdowns. */
      now: number
      /** The main session's agent type (`coordinator`, `engineer`), null when it started with none. */
      role: string | null
      offer: Shaped<Offer | null>
      stops: Shaped<StopState | null>
    }
  }
}
