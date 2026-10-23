// SPDX-License-Identifier: GPL-3.0-or-later
// The contract of the Conclave mod (docs/guides/conclave.md): the shape of `coney-tools conclave snapshot --json`
// (python/src/coney_tools/conclave_snapshot.py writes it) and the values the mod keeps in `$.state`.
// A snapshot of a higher `schema` than the mod's SCHEMA (src/snapshot.ts) is refused, never drawn.

/** A section that could not be read holds only its `error`. */
export type Read<T> = ({ error: null } & T) | { error: string }

export type Config = {
  /** Why the hook settings could not be read; the lists are then empty and the mod hides no agent type. */
  error: string | null
  source: string
  roles: string[]
  helpers: string[]
  require_role: boolean
  claude_code: string
  intervals: { local: number; github: number; github_idle: number }
  pane_min_columns: number
  pane_wide_columns: number
  band_narrow_columns: number
  stale_after_reads: number
  merged_visible_minutes: number
  claim_red_hours: number
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

export type Snapshot = {
  schema: number
  time: string
  checkout: string
  github: boolean
  config: Config
  units: Read<{ units: Unit[] }>
  builds: Read<{ lanes: Record<string, Lane> }>
  machine: Read<{ free_gb: number; min_free_gb: number; warn_free_gb: number }>
  claims: Read<{
    account: string | null
    rules: { warn_hours: number; deadline_hours: number; enforced: boolean }
    claims: ClaimRow[]
  }> | null
  prs: Read<{ prs: PrRow[]; merged: { number: number; branch: string; merged_at: string }[] }> | null
  issues: Read<{ issues: IssueRow[] }> | null
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
    }
  }
}
