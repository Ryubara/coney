// SPDX-License-Identifier: GPL-3.0-or-later
// What the mod's tests share: the fixture snapshot, the world beneath the mod (the snapshot command's answer, the clock,
// the store, the panes, the prompt box) and the helpers that start a session in it.
import type { On } from 'claude-code'
import { mock } from 'claude-code/testing'
import type { Engine } from 'claude-code/testing'

import type { Snapshot } from '../types'
import fixture from './fixtures/snapshot'

// The moment the fixture snapshot was taken.
export const T0 = Date.parse('2026-10-23T14:00:00Z')

export const BAND = {
  plugin: 'conclave',
  component: 'AbovePrompt',
  props: {
    hasSurvey: false,
    isWorking: false,
    maxRows: 10,
    bodyColumns: 160,
    scroll: { offset: 0, bodyRows: 10 },
    view: {},
  },
} as const

export const PANE = {
  plugin: 'conclave',
  component: 'Pane',
  requestId: 'conclave',
  props: {
    title: 'Conclave',
    isFocused: false,
    bodyColumns: 130,
    placement: 'dock',
    scroll: { offset: 0, bodyRows: 40 },
    view: {},
  },
} as const

/** Starts the session and lets the first tick make the GitHub read, so the clock stands at T0 and every section is held. */
export async function start($: Engine, world: World): Promise<void> {
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)
}

export const SESSION = { cwd: '/work/worktrees/fix-frame-812', surface: 'terminal', isInteractive: true } as const

export const OFFERED = { description: 'x', source: 'built-in', provider: { plugin: 'engine', tier: 'core' } } as const

/** A copy of the fixture snapshot, changed by `edit`. */
export function snapshot(edit?: (copy: Snapshot) => void): Snapshot {
  const copy = JSON.parse(JSON.stringify(fixture)) as Snapshot
  edit?.(copy)
  return copy
}

export type Answer = { exitCode: number; stdout: string; stderr: string }
export type World = {
  statuses: (string | undefined)[]
  runs: string[][]
  /** The `env` each run was given. */
  envs: (Record<string, string> | undefined)[]
  /** How long each run was given, in milliseconds. */
  timeouts: (number | undefined)[]
  clock: ReturnType<typeof mock.clock>
  /** What the command prints next; change it to move the world on. */
  answer: Answer
  opened: string[]
  /** The store in memory: the list saved for the checkout under test is `own`, other checkouts' lists keep their keys. */
  stored: Record<string, unknown>
  /** The keys the mod asked for as its own checkout's saved list. */
  ownKeys: Set<string>
  /** The toasts shown, and the wakes the mod submitted. */
  toasts: string[]
  wakes: string[]
  /** What the person has typed in the prompt box. */
  draft: string
  /** Set to make the engine refuse the next wakes (a person interrupted a turn). */
  isWakeRefused: boolean
}

/** The snapshot as it reads once claims expire: the fixture was written with `claims.release` still off. */
export function enforced(copy: Snapshot): void {
  if (copy.claims !== null && copy.claims.error === null) copy.claims.rules.enforced = true
}

export const prints = (snap: Snapshot): Answer => ({ exitCode: 0, stdout: JSON.stringify(snap), stderr: '' })
export const fails: Answer = { exitCode: 1, stdout: '', stderr: 'gh: not signed in\nmore\n' }

/** Everything beneath the mod, standing for the engine: the command's answer, the clock, the store and the panes. */
export function engine(
  on: On,
  answer: Answer,
  options: {
    /** The list saved for the checkout under test. */
    saved?: unknown
    /** The lists other checkouts saved, by their keys. */
    elsewhere?: Record<string, unknown>
    isPaneShown?: boolean
    isHung?: boolean
    beneath?: Record<string, unknown>
    /** What the plugins beneath answer to a Stop (Tether's own guard, in a real session). */
    beneathStop?: Record<string, unknown>
  } = {},
): World {
  const world: World = {
    statuses: [],
    runs: [],
    envs: [],
    timeouts: [],
    clock: mock.clock(on, { now: T0 - 1000 }),
    answer,
    opened: [],
    stored: {},
    ownKeys: new Set(),
    toasts: [],
    wakes: [],
    draft: '',
    isWakeRefused: false,
  }
  // The store in memory, kept here so a test can read what the mod stored (mock.store would hide it). The mod's own
  // list sits under a key that names its checkout, which the test cannot know: any `offer:` key not given as another
  // checkout's is the mod's, and is kept as `own`.
  const elsewhere = options.elsewhere ?? {}
  world.stored = { ...elsewhere }
  if (options.saved !== undefined) world.stored.own = options.saved
  const slot = (key: string): string => {
    if (!key.startsWith('offer:') || key in elsewhere) return key
    world.ownKeys.add(key)
    return 'own'
  }
  on('store.get', (_, e) => ({ value: world.stored[slot(e.key)] }))
  on('store.set', (_, e) => {
    world.stored[slot(e.key)] = e.value
    return { value: undefined }
  })
  on('store.delete', (_, e) => {
    delete world.stored[slot(e.key)]
    return { value: undefined }
  })
  on('agent.offer', () => ({ isOffered: true }))
  // What the plugins beneath answer to the session start (Tether's context for the first turn, in a real session).
  on('classic.SessionStart', () => options.beneath ?? {})
  on('classic.Stop', () => options.beneathStop ?? {})
  on('session.start', (_, e) => ({ cwd: e.cwd }))
  on('command.register', () => ({ value: { command: 'conclave' } }))
  on('ui.open', (_, e) => {
    world.opened.push(e.id)
    return { value: { isPlaced: true } }
  })
  on('ui.panes', () => ({
    value: options.isPaneShown
      ? [{ id: 'conclave', title: 'Conclave', isShown: true, isFocused: false, isPlaced: true }]
      : [],
  }))
  on('ui.toast', (_, e) => {
    world.toasts.push(e.text)
    return { value: undefined }
  })
  on('prompt.read', () => ({ value: { text: world.draft, cursor: world.draft.length } }))
  on('prompt.submit', (_, e) => {
    if (world.isWakeRefused) return { drop: 'the turn was interrupted' }
    world.wakes.push(e.text)
    return { text: e.text }
  })
  on('ui.status', (_, e) => {
    world.statuses.push(e.text)
    return { value: undefined }
  })
  on('process.run', (_, e) => {
    world.runs.push([...e.argv])
    world.envs.push(e.init?.env)
    world.timeouts.push(e.init?.timeoutMs)
    // A snapshot command that never answers (a hung `uv`): the session must start all the same.
    if (options.isHung) return new Promise<never>(() => undefined)
    return { value: { ...world.answer, isStdoutTruncated: false, isStderrTruncated: false } }
  })
  return world
}
