// What the coney mod reads (`coney-tools coord status --json [--github]`) and keeps in $.state.

/** Every section says why it was not read (error alone), or null; a section read in full has its fields too. */
export type Section<T> = { error: string | null } & Partial<T>

export type PcsxCopy = {
  copy: string
  path: string
  port: number | null
  /** free, held, stale, or "in use?" (PCSX2 runs without a claim). */
  state: string
  claim: { agent: string; time: string; copy?: string } | null
  running: boolean
  sdl: boolean
  pids: number[]
}

export type BuildClaim = {
  slot: number
  agent: string
  worktree: string
  preset: string
  step: string
  pid: number
  started: string
  lane: string
}
export type BuildTicket = {
  position: number
  stale: boolean
  agent: string
  worktree: string
  preset: string
  priority?: boolean
  time: number
  pid: number
}
export type BuildLane = { slots: { slot: number; stale: boolean; claim: BuildClaim | null }[]; queue: BuildTicket[] }

export type BuildResult = {
  worktree: string
  branch: string | null
  /** running, done or lost. */
  state: string
  agent: string | null
  preset: string | null
  exit_code: number | null
  started: string | null
  ended: string | null
  /** It ended within the last hour: news for the coordinator. */
  recent: boolean
}

export type Track = { branch: string; status: string; changed: string; worktree: string | null; busy: boolean }

export type Unit = {
  branch: string
  file: string
  path: string
  changed: string
  worktree: string | null
  /** origin has the branch (as the local clone knows; no fetch). */
  pushed: boolean
  /** The pull request that released it (open, or merged or closed after the file last changed); --github only. */
  pr: number | null
  pr_state: string | null
  /** Waits for the coordinator to release it; null when only a --github read can tell. */
  queued: boolean | null
}

export type Issue = {
  number: number
  status: string | null
  priority: string | null
  area: string | null
  labels: string[]
  created: string | null
  updated: string | null
  age_days: number | null
}

export type Pr = {
  number: number
  branch: string
  base: string
  draft: boolean
  mergeable: string | null
  auto_merge: boolean
  ci: 'pass' | 'pending' | 'fail' | 'none'
  updated: string | null
  /** The pr-health causes: checks-failed, conflict, ci-stuck, auto-merge-off. */
  causes: string[]
}

/** What `coney-tools coord status --json` answers: ids, labels, states, counts and times; never a title or a body. */
export type CoordStatus = {
  time: string
  checkout: string
  github: boolean
  worktrees: Section<{ count: number }>
  pcsx2: Section<{ copies: PcsxCopy[] }>
  builds: Section<{ lanes: Record<string, BuildLane> }>
  results: Section<{ results: BuildResult[] }>
  tracks: Section<{ tracks: Track[] }>
  release: Section<{ units: Unit[] }>
  issues: Section<{ issues: Issue[]; counts: Record<string, number>; notices: string[] }> | null
  prs: Section<{ prs: Pr[]; health: { stuck: { number: number; cause: string; detail?: string }[] } }> | null
  ci: Section<{ queued: number; in_progress: number }> | null
  /** What waits for the coordinator, as stable ids (`issue:<n>:<label>`, `pr:<n>:<cause>`, `unit:<branch>:<file>`,
   * `build:done:<worktree>`). */
  action: string[]
}

/** The last reads: the last of either kind (its local sections are the newest), and the last with --github. */
export type Reading = { last: CoordStatus | null; github: CoordStatus | null }

export type Watch = {
  /** When the last read finished (ISO time), or null before the first. */
  readAt: string | null
  /** When the last --github read finished, or null. */
  githubAt: string | null
  /** Why the last read failed, or null. */
  failure: string | null
  /** Why the last --github read failed, or null. */
  githubFailure: string | null
  /** The waiting ids the session was last woken for, sorted. */
  wokenFor: string[]
  /** When the session was last woken (ISO time), or null. */
  wokenAt: string | null
  /** Repeat wakes since the last wake for new work: each waits twice as long as the one before. */
  repeats?: number
  /** Why the last wake was refused, while it stands, or null. */
  refused?: string | null
  /** Subagents at work, at the last read. */
  agents?: number
}

export type Tab = 'work' | 'machine' | 'files'

/** Where the person is in the pane: the tab, each tab's sub-tab, and what is picked or opened in it. */
export type View = {
  tab: Tab
  /** The Work tab's sub-tab: waiting, issues, prs, release. */
  workSub: string
  /** The Machine tab's sub-tab: pcsx2, builds, tracks. */
  machineSub: string
  /** Every open issue, not only those that need triage or carry no status. */
  allIssues: boolean
  filesSub: string
  /** Folders shown open in the tree, relative to the session's folder ("" is the folder itself). */
  open: string[]
  /** The file under Preview, relative to the session's folder. */
  file: string | null
}

/** A folder's listing as the Files tab draws it, or why it could not be read. */
export type Listing = { entries: { name: string; dir: boolean; size: number; mtimeMs: number }[]; error: string | null }
export type Changes = { at: string | null; lines: { mark: string; path: string }[]; error: string | null }
export type Preview = { path: string; text: string | null; size: number; mtimeMs: number; error: string | null }

declare module 'claude-code' {
  interface PluginState {
    coney: {
      reading: Reading
      watch: Watch
      view: View
      tree: Record<string, Listing>
      changes: Changes
      preview: Preview | null
      /** The kind of the element the keyboard's focus is on in the pane (its tip shows), or null. */
      focusTip: string | null
    }
  }
}
