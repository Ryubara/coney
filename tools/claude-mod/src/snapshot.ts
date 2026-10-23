// Reading the snapshot: run `coney-tools conclave snapshot`, keep the sections of the last good reads, poll on the
// intervals the snapshot itself carries. Read-only: the mod runs one command that only prints, and writes nothing
// but its own `$.state` and `$.store`.
import type { EngineInterface } from 'claude-code'

import type { Held, Snapshot, Unit } from '../types'
import { changeHeld, getHeld, getOffer, setNow, setOffer } from './state'

/** The snapshot shape this mod draws; `coney-tools conclave snapshot` prints it as `schema`. */
export const SCHEMA = 1
/** Where a read is retried before any snapshot has said how often to read: only the first snapshot can set it. */
const BOOTSTRAP_RETRY_MS = 15_000
/** The longest a snapshot may take (the GitHub read is the slow one). */
const READ_TIMEOUT_MS = 120_000
const PANE = 'conclave'

export type Run = { ok: true; snapshot: Snapshot } | { ok: false; error: string }

/** The checkout a mod folder belongs to: `<checkout>/tools/claude-mod` without its last two parts. */
export function checkoutOf(pluginRoot: string): string {
  const parts = pluginRoot.replace(/\\/g, '/').replace(/\/+$/, '').split('/')
  return parts.slice(0, -2).join('/')
}

/** The first line of an error's text, cut to fit a band. */
export function firstLine(text: string): string {
  const line = text.split(/\r?\n/).find(one => one.trim() !== '') ?? 'unknown error'
  return line.length > 120 ? `${line.slice(0, 117)}...` : line
}

/** Runs the snapshot command and parses what it printed. */
export async function runSnapshot($: EngineInterface, github: boolean): Promise<Run> {
  const root = checkoutOf($.plugin.root)
  const argv = ['uv', 'run', '-q', '--project', `${root}/python`, 'coney-tools', 'conclave', 'snapshot', '--json']
  if (github) argv.push('--github')
  try {
    const done = await $.process.run(argv, { cwd: root, timeoutMs: READ_TIMEOUT_MS })
    if (done.exitCode !== 0) return { ok: false, error: firstLine(done.stderr || done.stdout) }
    const snapshot = JSON.parse(done.stdout) as Snapshot
    if (typeof snapshot.schema !== 'number') return { ok: false, error: 'not a snapshot' }
    return { ok: true, snapshot }
  } catch (error) {
    return { ok: false, error: firstLine(String(error)) }
  }
}

/** A local read leaves the units' pull requests and claim overlaps to the last GitHub read: carry them over. */
function carryUnits(previous: Snapshot | null, fresh: Snapshot): Snapshot['units'] {
  if (previous === null || previous.units.error !== null || fresh.units.error !== null) return fresh.units
  const before = new Map<string, Unit>(previous.units.units.map(unit => [unit.task, unit]))
  return {
    ...fresh.units,
    units: fresh.units.units.map(unit => {
      const old = before.get(unit.task)
      if (old === undefined) return unit
      return {
        ...unit,
        pr: old.pr,
        overlaps: [...unit.overlaps, ...old.overlaps.filter(overlap => overlap.kind === 'claim')],
      }
    }),
  }
}

/** The held sections after one more read (`github`: it was a read with `--github`) at `nowMs`. */
export function merged(previous: Held, result: Run, github: boolean, nowMs: number): Held {
  if (!result.ok) {
    return github ? { ...previous, githubError: result.error } : { ...previous, error: result.error }
  }
  const fresh = result.snapshot
  if (fresh.schema > SCHEMA) return { ...previous, newer: fresh.schema, error: null }
  if (github) {
    return { ...previous, snapshot: fresh, localAt: nowMs, githubAt: nowMs, error: null, githubError: null, newer: null }
  }
  const old = previous.snapshot
  const snapshot: Snapshot = {
    ...fresh,
    github: old?.github ?? false,
    claims: old?.claims ?? null,
    prs: old?.prs ?? null,
    issues: old?.issues ?? null,
    units: carryUnits(old, fresh),
  }
  return { ...previous, snapshot, localAt: nowMs, error: null, newer: null }
}

/** Whether a stored value has the shape of the frozen agent list. */
export function isOffer(value: unknown): value is { allowed: string[] } {
  const list = (value as { allowed?: unknown } | null)?.allowed
  return Array.isArray(list) && list.every(item => typeof item === 'string')
}

/** The agent types the model may be offered, from the snapshot's config; null while it names no role. */
export function allowedTypes(snapshot: Snapshot): string[] | null {
  const { config } = snapshot
  if (config.error !== null || config.roles.length === 0) return null
  return [...new Set([...config.roles, ...config.helpers])]
}

/**
 * Starts the poll: one timer a second that moves the mod's clock while a pane is shown (once a minute otherwise) and
 * starts a read when one is due. Resolves with the first read, so `session.start` can wait for it when it must.
 */
export function startPolling(
  $: EngineInterface,
  onChange: (state: Held, nowMs: number) => void,
): Promise<void> {
  let isBusy = false
  let lastTry = 0
  let lastGithubTry = 0
  let lastClock = 0

  const take = async (github: boolean): Promise<void> => {
    isBusy = true
    try {
      const result = await runSnapshot($, github)
      const at = await $.clock.now()
      if (github) lastGithubTry = at
      lastTry = at
      await changeHeld($, state => merged(state, result, github, at))
      if (result.ok && !github) {
        const allowed = allowedTypes(result.snapshot)
        // Kept for the next session, which hides agent types from its first turn without waiting for a read.
        if (allowed !== null) await $.store.set('offer', { allowed })
        if (allowed !== null && (await getOffer($)) === null) await setOffer($, allowed)
      }
      const state = await getHeld($)
      onChange(state, at)
    } finally {
      isBusy = false
    }
  }

  const tick = async (): Promise<void> => {
    const at = await $.clock.now()
    const panes = await $.ui.panes()
    const isShown = panes.some(pane => pane.id === PANE && pane.isPlaced)
    // Countdowns need a second hand only while the pane shows them.
    if (isShown || at - lastClock >= 60_000) {
      lastClock = at
      await setNow($, at)
    }
    if (isBusy) return
    const state = await getHeld($)
    const intervals = state.snapshot?.config.intervals
    const localMs = (intervals?.local ?? BOOTSTRAP_RETRY_MS / 1000) * 1000
    const githubMs = (isShown ? intervals?.github : intervals?.github_idle) ?? 0
    const isGithubDue = intervals !== undefined && at - Math.max(state.githubAt, lastGithubTry) >= githubMs * 1000
    if (isGithubDue) await take(true)
    else if (at - Math.max(state.localAt, lastTry) >= localMs) await take(false)
  }

  // A failed tick is skipped, never thrown: the next second tries again.
  $.clock.every(1000, () => {
    tick().catch(() => undefined)
  })
  // The first read is local, so the band and the agent list never wait for GitHub.
  return take(false).catch(() => undefined)
}
