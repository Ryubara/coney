export type InboxPage = { id: string; state: string; verdict: string | null }

export type InboxStation = {
  role: string
  pages: InboxPage[]
  counts: Record<string, number>
  action: string[]
}

export type Walls = {
  checked: string | null
  passed: number | null
  checks: number | null
  breach: boolean
  breach_folders: string[]
  /** Folders where the last full check found a copy of an older Coney file outside the sweep's quarantine. */
  legacy_folders: string[]
}

/** What `coney-tools zone status --json` answers: ids, states and counts only, never page text. */
export type ZoneStatus = {
  time: string
  account: string
  zone: string | null
  role: string | null
  brief: string | null
  /** Wake the role again after this many idle minutes with no subagent at work; null: only for inbox changes. */
  idle_wake_minutes: number | null
  inbox: { role: string; active: boolean; stations: Record<string, InboxStation>; action: string[] } | null
  error: string | null
  walls: Walls
  pages_in_main: number | null
}

export type Watch = {
  /** When the last read finished (ISO time), or null before the first. */
  readAt: string | null
  /** Why the last read failed, or null. */
  failure: string | null
  /** The waiting items the session was last woken for, sorted. */
  wokenFor: string[]
  /** When the session was last woken (ISO time), or null. */
  wokenAt: string | null
}

declare module 'claude-code' {
  interface PluginState {
    'coney-zone': { status: ZoneStatus | null; watch: Watch; impls: ImplLog | null; picked: string | null }
  }
}

/** One implementer's live state, as `coney-tools impl log --json` lists it. */
export type ImplState = {
  name: string
  task?: string
  branch?: string
  started?: string
  ended?: string
  exit?: number
}

/** What `coney-tools impl log --json` answers: the implementers, the one shown and the last lines of its log. */
export type ImplLog = { implementers: ImplState[]; shown: string | null; lines: string[]; readable: boolean }
