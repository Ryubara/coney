// SPDX-License-Identifier: GPL-3.0-or-later
// Reading the snapshot: run `coney-tools conclave snapshot`, keep the sections of the last good reads, poll on the
// intervals the snapshot itself carries. Read-only: the mod runs one command that only prints, and writes nothing
// but its own `$.state` and `$.store`.
import type { EngineInterface } from 'claude-code'

import type { Held, Snapshot, Unit } from '../types'
import { PANE } from './format'
import { changeHeld, clearOffer, getHeld, getRole, setNow } from './state'

/** The snapshot shape this mod draws; `coney-tools conclave snapshot` prints it as `schema`. */
export const SCHEMA = 2
// The three numbers the mod holds itself, because it needs them before any snapshot has said otherwise (every other
// number arrives in the snapshot, from .github/conventions.yml). A read that overruns is killed, so a hung `uv` never
// outlives its bound: a session that ends meanwhile (`claude -p`) is not held by it either.
/** Where a read is retried before any snapshot has said how often to read. */
const BOOTSTRAP_RETRY_MS = 15_000
/** The longest a local snapshot may take (a cold `uv` start included). */
const LOCAL_TIMEOUT_MS = 30_000
/** The longest a snapshot with `--github` may take (the GitHub read is the slow one: 8 to 15 s here). */
const GITHUB_TIMEOUT_MS = 60_000

export type Run = { ok: true; snapshot: Snapshot } | { ok: false; error: string }

/** The checkout a mod folder belongs to: `<checkout>/tools/claude-mod` without its last two parts. */
export function checkoutOf(pluginRoot: string): string {
  const parts = pluginRoot.replace(/\\/g, '/').replace(/\/+$/, '').split('/')
  return parts.slice(0, -2).join('/')
}

/**
 * The private Python environment for a checkout, in that checkout's own scratch folder (`<workspace>/scratch/<branch>/
 * conclave-venv`, the main checkout's being `scratch/main/`), or null when the checkout is not in the workspace layout
 * (`<workspace>/repos/coney` or `<workspace>/worktrees/coney/<branch>`). Without it `uv run` would create
 * `python/.venv` inside a worktree, where every tool and linter would then scan it (docs/guides/how-we-work.md, "Keep
 * Python out of the tree"). One per checkout, and it goes with the branch's scratch folder, which is pruned like the rest.
 */
export function venvOf(checkout: string): string | null {
  const parts = checkout.replace(/\\/g, '/').replace(/\/+$/, '').split('/')
  const name = parts.at(-1) ?? ''
  const isMain = parts.at(-2) === 'repos'
  const home = isMain ? 2 : parts.at(-3) === 'worktrees' ? 3 : 0
  if (home === 0 || name === '') return null
  // No worktree can be named `main` (the main checkout holds that branch), so the main checkout's folder cannot clash.
  return `${parts.slice(0, -home).join('/')}/scratch/${isMain ? 'main' : name}/conclave-venv`
}

/** The store key of the agent list saved for the checkout a mod folder belongs to: each checkout keeps its own. */
export function offerKey(pluginRoot: string): string {
  return `offer:${checkoutOf(pluginRoot)}`
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
  const venv = venvOf(root)
  try {
    const done = await $.process.run(argv, {
      cwd: root,
      timeoutMs: github ? GITHUB_TIMEOUT_MS : LOCAL_TIMEOUT_MS,
      ...(venv === null ? {} : { env: { UV_PROJECT_ENVIRONMENT: venv } }),
    })
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
    events: old?.events ?? null,
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
 * What a good local read does to the agent list. The list a session hides by is read from the store when the session
 * starts (or a conversation does: a `/clear` starts a new prompt cache) and never changes under the prompt cache, so a
 * read only keeps the list for the NEXT start: it stores the allowed types under this checkout's key. A read that says
 * the hook settings cannot be read (or name no role) drops this checkout's stored list and the frozen one, because a
 * stale list must not hide anything the settings no longer ask to hide; another checkout's list is never touched.
 */
async function keepOffer($: EngineInterface, snapshot: Snapshot): Promise<void> {
  const allowed = allowedTypes(snapshot)
  if (allowed !== null) {
    await $.store.set(offerKey($.plugin.root), { allowed })
    return
  }
  await $.store.delete(offerKey($.plugin.root))
  await clearOffer($)
}

/**
 * Starts the poll: one timer a second that moves the mod's clock while a pane is shown (once a minute otherwise) and
 * starts a read when one is due, the first at once. It returns without waiting for that read: the session must not
 * wait for a command that runs `uv`, and the band draws from the first good read whenever it lands.
 */
export function startPolling($: EngineInterface, onChange: (state: Held, nowMs: number) => void): void {
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
      if (result.ok && !github) await keepOffer($, result.snapshot)
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
    // A session that watches reads GitHub on the watchers' own interval while the pane is closed: the events come from it.
    const watcher = state.snapshot !== null && state.snapshot.config.watch.roles.includes((await getRole($)) ?? '')
    const idleMs = watcher ? state.snapshot?.config.watch.github_seconds : intervals?.github_idle
    const githubMs = (isShown ? intervals?.github : idleMs) ?? 0
    const isGithubDue = intervals !== undefined && at - Math.max(state.githubAt, lastGithubTry) >= githubMs * 1000
    if (isGithubDue) await take(true)
    else if (at - Math.max(state.localAt, lastTry) >= localMs) await take(false)
  }

  // A failed tick is skipped, never thrown: the next second tries again.
  $.clock.every(1000, () => {
    tick().catch(() => undefined)
  })
  // The first read is local, so the band never waits for GitHub; its failure is the next tick's to retry.
  take(false).catch(() => undefined)
}
