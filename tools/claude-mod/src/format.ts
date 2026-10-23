// SPDX-License-Identifier: GPL-3.0-or-later
// What the mod says, as plain functions of the held snapshot and the clock: no `$`, so the tests can read them directly.
// The mod draws and never decides: every number it compares against (the warning hours, the disk limits, the
// intervals, the widths, the stale factor) arrives in the snapshot, from .github/conventions.yml.
import type { ClaimRow, Config, Held, Snapshot, Unit } from '../types'

/** The id of the Conclave pane: the band opens it, the poll looks for it and the hooks draw it. */
export const PANE = 'conclave'

export type Tone = 'normal' | 'ok' | 'warn' | 'bad' | 'dim'

/** The engine's colour name for each tone (`undefined`: the default colour). */
export const COLORS: Record<Tone, string | undefined> = {
  normal: undefined,
  ok: 'success',
  warn: 'warning',
  bad: 'error',
  dim: undefined,
}

export type MarkKind = 'pending' | 'active' | 'merged' | 'failed'

/** The surface names the engine draws on; the terminal gets single-width symbols, the others emoji. */
export type Surface = 'terminal' | 'desktop' | 'vscode' | 'mobile'

const SYMBOLS: Record<MarkKind, string> = { pending: '⚠', active: '●', merged: '✔', failed: '✖' }
const EMOJI: Record<MarkKind, string> = { pending: '⚠️', active: '🟢', merged: '✅', failed: '❌' }

/** The mark of a row: coloured single-width symbols on the terminal, emoji elsewhere (fonts decide the former). */
export function mark(kind: MarkKind, surface: Surface): string {
  return (surface === 'terminal' ? SYMBOLS : EMOJI)[kind]
}

const MARK_TONE: Record<MarkKind, Tone> = { pending: 'warn', active: 'ok', merged: 'dim', failed: 'bad' }

/** The tone a row's mark is drawn in. */
export function markTone(kind: MarkKind): Tone {
  return MARK_TONE[kind]
}

const two = (n: number): string => String(n).padStart(2, '0')

/** The time left until `deadline` as `2d 19h`, `21h 04m` or `42m 10s`; `expired` once it has passed. */
export function countdown(deadline: string, nowMs: number): string {
  const left = Math.floor((Date.parse(deadline) - nowMs) / 1000)
  if (Number.isNaN(left)) return '?'
  if (left <= 0) return 'expired'
  const days = Math.floor(left / 86400)
  const hours = Math.floor((left % 86400) / 3600)
  const minutes = Math.floor((left % 3600) / 60)
  if (days >= 1) return `${days}d ${two(hours)}h`
  if (hours >= 1) return `${hours}h ${two(minutes)}m`
  return `${minutes}m ${two(left % 60)}s`
}

/** How long ago `since` was, in one unit: `40s`, `4m`, `3h`, `2d`; `?` for a time that does not parse. */
export function elapsed(since: string, nowMs: number): string {
  const seconds = Math.floor((nowMs - Date.parse(since)) / 1000)
  if (Number.isNaN(seconds)) return '?'
  if (seconds < 60) return `${Math.max(0, seconds)}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h`
  return `${Math.floor(seconds / 86400)}d`
}

/** The UTC moment as people read it: `2026-10-25 20:14 UTC`. */
export function showUtc(iso: string): string {
  const moment = new Date(iso)
  if (Number.isNaN(moment.getTime())) return iso
  return `${moment.toISOString().slice(0, 16).replace('T', ' ')} UTC`
}

type ClaimRules = { warn_hours: number; deadline_hours: number; enforced: boolean }

/** A claim's countdown colour: amber from the warning, red in the last `redHours`; none while it waits or is unenforced. */
export function claimTone(claim: ClaimRow, rules: ClaimRules, redHours: number, nowMs: number): Tone {
  if (claim.deadline === null) return 'dim'
  if (!rules.enforced) return 'normal'
  const hoursLeft = (Date.parse(claim.deadline) - nowMs) / 3_600_000
  if (hoursLeft <= redHours) return 'bad'
  if (hoursLeft <= rules.deadline_hours - rules.warn_hours) return 'warn'
  return 'normal'
}

/** What a claim's right-hand cell says: the countdown, why it waits, or that expiry is not enforced yet. */
export function claimExpiry(claim: ClaimRow, rules: ClaimRules, nowMs: number): string {
  if (claim.deadline === null) return claim.waiting === null ? 'no deadline' : `waits while ${claim.waiting}`
  const left = countdown(claim.deadline, nowMs)
  return rules.enforced ? `expires in ${left}` : `would expire in ${left} (not enforced yet)`
}

export type UnitKind = MarkKind | 'gone'

/** A unit's mark: failed (a step, its pull request's checks or its task file), merged lately, not started, or active. */
export function unitKind(unit: Unit, nowMs: number, config: Config): UnitKind {
  if (unit.error !== null || unit.failed !== null) return 'failed'
  if (unit.pr !== null && (unit.pr.causes.length > 0 || unit.pr.ci === 'fail')) return 'failed'
  if (unit.pr !== null && unit.pr.state === 'merged') {
    const minutes = unit.pr.merged_at === null ? Infinity : (nowMs - Date.parse(unit.pr.merged_at)) / 60_000
    return minutes <= config.merged_visible_minutes ? 'merged' : 'gone'
  }
  return unit.agent === null ? 'pending' : 'active'
}

const ORDER: Record<UnitKind, number> = { failed: 0, active: 1, pending: 2, merged: 3, gone: 4 }

/** The units the pane lists, failed ones first, then active, not started and merged; long-merged ones left out. */
export function visibleUnits(snapshot: Snapshot, nowMs: number): { unit: Unit; kind: MarkKind }[] {
  if (snapshot.units.error !== null) return []
  const rows: { unit: Unit; kind: MarkKind }[] = []
  for (const unit of snapshot.units.units) {
    const kind = unitKind(unit, nowMs, snapshot.config)
    if (kind !== 'gone') rows.push({ unit, kind })
  }
  return rows.sort(
    (a, b) => ORDER[a.kind] - ORDER[b.kind] || a.unit.task.localeCompare(b.unit.task),
  )
}

/** The unit's own title for a row: its first issue's title when the snapshot has it, else the task's goal line. */
export function unitTitle(snapshot: Snapshot, unit: Unit): string {
  const first = unit.issues[0]
  const issues = snapshot.issues !== null && snapshot.issues.error === null ? snapshot.issues.issues : []
  const found = issues.find(issue => issue.number === first)
  return found?.title ?? (unit.goal || unit.task)
}

export type BandItem = {
  key: string
  text: string
  tone: Tone
}

/** Whole minutes since the last good local read, once it is older than `stale_after_reads` local intervals; else null. */
export function staleMinutes(held: Held, nowMs: number): number | null {
  const config = held.snapshot?.config
  if (held.localAt === 0 || config === undefined) return null
  const age = (nowMs - held.localAt) / 1000
  return age > config.intervals.local * config.stale_after_reads ? Math.floor(age / 60) : null
}

const plural = (n: number, one: string, many: string): string => `${n} ${n === 1 ? one : many}`

/** The band's items, zero counts left out: units, pull requests, claims, builds, critical issues and free disk. */
export function bandItems(held: Held, nowMs: number): BandItem[] {
  const snapshot = held.snapshot
  if (snapshot === null) return []
  const items: BandItem[] = []
  if (snapshot.units.error === null) {
    const live = snapshot.units.units.filter(unit => {
      const kind = unitKind(unit, nowMs, snapshot.config)
      return kind !== 'gone' && kind !== 'merged'
    })
    if (live.length > 0) items.push({ key: 'units', text: plural(live.length, 'unit', 'units'), tone: 'normal' })
    const failed = live.filter(unit => unitKind(unit, nowMs, snapshot.config) === 'failed').length
    if (failed > 0) items.push({ key: 'failed', text: `✖${failed} failed`, tone: 'bad' })
  }
  if (snapshot.prs !== null && snapshot.prs.error === null && snapshot.prs.prs.length > 0) {
    const broken = snapshot.prs.prs.filter(pr => pr.causes.length > 0 || pr.ci === 'fail')
    const first = broken[0]
    const detail = first === undefined ? '' : ` (✖${broken.length} #${first.number})`
    items.push({
      key: 'prs',
      text: `PRs ${snapshot.prs.prs.length}${detail}`,
      tone: broken.length > 0 ? 'bad' : 'normal',
    })
  }
  if (snapshot.claims !== null && snapshot.claims.error === null && snapshot.claims.claims.length > 0) {
    const { claims, rules } = snapshot.claims
    const tones = claims.map(claim => claimTone(claim, rules, snapshot.config.claim_red_hours, nowMs))
    const tone: Tone = tones.includes('bad') ? 'bad' : tones.includes('warn') ? 'warn' : 'normal'
    items.push({ key: 'claims', text: `claims ${claims.length}`, tone })
  }
  if (snapshot.builds.error === null) {
    const lanes = Object.values(snapshot.builds.lanes)
    const running = lanes.reduce((n, lane) => n + lane.slots.filter(slot => slot.claim !== null).length, 0)
    const queued = lanes.reduce((n, lane) => n + lane.queue.length, 0)
    if (running + queued > 0) {
      items.push({
        key: 'builds',
        text: `builds ${running}${queued > 0 ? ` (+${queued} queued)` : ''}`,
        tone: 'normal',
      })
    }
  }
  if (snapshot.issues !== null && snapshot.issues.error === null) {
    const critical = snapshot.issues.issues.filter(issue => issue.priority === 'P-critical')
    const first = critical[0]
    if (first !== undefined) {
      const more = critical.length > 1 ? ` +${critical.length - 1}` : ''
      items.push({ key: 'critical', text: `⚠ P-critical #${first.number}${more}`, tone: 'warn' })
    }
  }
  if (snapshot.machine.error === null) {
    const { free_gb, min_free_gb, warn_free_gb } = snapshot.machine
    const tone: Tone = free_gb < min_free_gb ? 'bad' : free_gb < warn_free_gb ? 'warn' : 'normal'
    items.push({ key: 'disk', text: `disk ${Math.round(free_gb)} GB`, tone })
  }
  return items
}

const slashes = (path: string): string => path.replace(/\\/g, '/').replace(/\/+$/, '').toLowerCase()

/** Whether `cwd` is the worktree of `unit` or a folder inside it. */
export function inWorktree(unit: Unit, cwd: string): boolean {
  if (unit.worktree === null) return false
  const tree = slashes(unit.worktree)
  const here = slashes(cwd)
  return here === tree || here.startsWith(`${tree}/`)
}

/** The status line: `role · unit · stage` for a session in a unit's worktree, a short summary for the coordinator. */
export function statusText(role: string | null, cwd: string, held: Held, nowMs: number): string {
  if (role === null) return 'no role: start with coney-agent'
  const snapshot = held.snapshot
  if (snapshot === null || snapshot.units.error !== null) return role
  if (role === 'coordinator') {
    const kinds = snapshot.units.units.map(unit => unitKind(unit, nowMs, snapshot.config))
    const live = kinds.filter(kind => kind !== 'gone' && kind !== 'merged').length
    const failed = kinds.filter(kind => kind === 'failed').length
    return ['coordinator', plural(live, 'unit', 'units'), failed > 0 ? `✖${failed}` : ''].filter(Boolean).join(' · ')
  }
  const unit = snapshot.units.units.find(one => inWorktree(one, cwd))
  return unit === undefined ? role : `${role} · ${unit.task} · ${unit.stage ?? '?'}`
}
