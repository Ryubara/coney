// SPDX-License-Identifier: GPL-3.0-or-later
// What the mod says, as plain functions of the held snapshot and the clock: no `$`, so the tests can read them directly.
// The mod draws and never decides: every number it compares against (the warning hours, the disk limits, the
// intervals, the widths, the stale factor) arrives in the snapshot, from .github/conventions.yml.
import type { ClaimRow, Config, Held, Snapshot, Unit } from '../types'

/** The id of the Conclave pane: the band opens it, the poll looks for it and the hooks draw it. */
export const PANE = 'conclave'

export type Tone = 'normal' | 'ok' | 'warn' | 'bad' | 'dim'

/** The engine's colour name for a tone (`undefined`: the default colour). */
export function color(tone: Tone): string | undefined {
  switch (tone) {
    case 'ok':
      return 'success'
    case 'warn':
      return 'warning'
    case 'bad':
      return 'error'
    case 'normal':
    case 'dim':
      return undefined
  }
}

export type MarkKind = 'pending' | 'active' | 'merged' | 'failed'

/** The surface names the engine draws on; the terminal gets single-width symbols, the others emoji. */
export type Surface = 'terminal' | 'desktop' | 'vscode' | 'mobile'

/** The mark of a row: coloured single-width symbols on the terminal, emoji elsewhere (fonts decide the former). */
export function mark(kind: MarkKind, surface: Surface): string {
  const isTerminal = surface === 'terminal'
  switch (kind) {
    case 'pending':
      return isTerminal ? '⚠' : '⚠️'
    case 'active':
      return isTerminal ? '●' : '🟢'
    case 'merged':
      return isTerminal ? '✔' : '✅'
    case 'failed':
      return isTerminal ? '✖' : '❌'
  }
}

/** The tone a row's mark is drawn in. */
export function markTone(kind: MarkKind): Tone {
  switch (kind) {
    case 'pending':
      return 'warn'
    case 'active':
      return 'ok'
    case 'merged':
      return 'dim'
    case 'failed':
      return 'bad'
  }
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

/**
 * The pane's layouts, widest first: `wide` puts a row on one line, `compact` gives a row its key fields and then its
 * title on a second line, `narrow` keeps only the issue and the stage on the first line and abbreviates the claims, and
 * `line` is one clipped summary line.
 */
export type Layout = 'wide' | 'compact' | 'narrow'

/** The layout for a pane `columns` wide, by the three limits the snapshot carries (never a width of the mod's own). */
export function paneLayout(
  columns: number,
  config: Pick<Config, 'pane_min_columns' | 'pane_compact_columns' | 'pane_wide_columns'>,
): Layout | 'line' {
  if (columns >= config.pane_wide_columns) return 'wide'
  if (columns >= config.pane_compact_columns) return 'compact'
  if (columns >= config.pane_min_columns) return 'narrow'
  return 'line'
}

/** A claim's countdown colour: amber from the warning, red in the last `redHours`; none while it waits or is unenforced. */
export function claimTone(claim: ClaimRow, rules: ClaimRules, redHours: number, nowMs: number): Tone {
  if (claim.deadline === null) return 'dim'
  if (!rules.enforced) return 'normal'
  const hoursLeft = (Date.parse(claim.deadline) - nowMs) / 3_600_000
  if (hoursLeft <= redHours) return 'bad'
  if (hoursLeft <= rules.deadline_hours - rules.warn_hours) return 'warn'
  return 'normal'
}

/**
 * What a claim's right-hand cell says: the countdown, why it waits, or that expiry is not enforced yet. `isShort` drops
 * the explanation of an unenforced expiry (`would expire in 2d 14h`), for the compact pane.
 */
export function claimExpiry(claim: ClaimRow, rules: ClaimRules, nowMs: number, isShort = false): string {
  if (claim.deadline === null) return claim.waiting === null ? 'no deadline' : `waits while ${claim.waiting}`
  const left = countdown(claim.deadline, nowMs)
  if (rules.enforced) return `expires in ${left}`
  return isShort ? `would expire in ${left}` : `would expire in ${left} (not enforced yet)`
}

/**
 * A claim's countdown in the fewest characters, for the narrow pane: `2d 14h`, `~2d 14h` while expiry is not enforced
 * (it would expire then), `waits: S-blocked`, `no deadline` or `expired`. The colour carries how near it is.
 */
export function claimExpiryTiny(claim: ClaimRow, rules: ClaimRules, nowMs: number): string {
  if (claim.deadline === null) return claim.waiting === null ? 'no deadline' : `waits: ${claim.waiting}`
  const left = countdown(claim.deadline, nowMs)
  return rules.enforced || left === 'expired' ? left : `~${left}`
}

/**
 * One held claim's line in the claims list. The wide one gives the claimant, the stage and then the countdown; the
 * compact one leads with the countdown; the narrow one has the bare countdown and the warning ahead of the claimant, so
 * a line clipped to the pane keeps them. All say `(warned)` only for a claim that has a deadline: one that waits has no
 * clock to warn about.
 */
export function claimLineText(claim: ClaimRow, rules: ClaimRules, nowMs: number, layout: Layout): string {
  const warned = claim.deadline !== null && claim.warned
  if (layout === 'narrow') {
    return `  #${claim.issue} ${claimExpiryTiny(claim, rules, nowMs)}${warned ? ' (warned)' : ''} ${claim.claimant}`
  }
  if (layout === 'compact') {
    return `  #${claim.issue} ${claimExpiry(claim, rules, nowMs, true)}  ${claim.claimant}${warned ? ' (warned)' : ''}`
  }
  const expiry = claimExpiry(claim, rules, nowMs)
  return `  #${claim.issue}  ${claim.claimant}  ${claim.stage ?? claim.status}  ${expiry}${warned ? '  (warned)' : ''}`
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

/** Where a kind of unit sorts in the pane: failed first, then active, not started, merged. */
function order(kind: UnitKind): number {
  switch (kind) {
    case 'failed':
      return 0
    case 'active':
      return 1
    case 'pending':
      return 2
    case 'merged':
      return 3
    case 'gone':
      return 4
  }
}

/** The units the pane lists, failed ones first, then active, not started and merged; long-merged ones left out. */
export function visibleUnits(snapshot: Snapshot, nowMs: number): { unit: Unit; kind: MarkKind }[] {
  if (snapshot.units.error !== null) return []
  const rows: { unit: Unit; kind: MarkKind }[] = []
  for (const unit of snapshot.units.units) {
    const kind = unitKind(unit, nowMs, snapshot.config)
    if (kind !== 'gone') rows.push({ unit, kind })
  }
  return rows.sort(
    (a, b) => order(a.kind) - order(b.kind) || a.unit.task.localeCompare(b.unit.task),
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

/** A unit's issue numbers as `#741 #803`, empty when it names none. */
export function issueIds(unit: Unit): string {
  return unit.issues.map(number => `#${number}`).join(' ')
}

/** The stage a unit is at in words the pane, the band and the status line share: `compared failed`, `assigned`. */
export function stageText(unit: Unit): string {
  if (unit.error !== null) return 'task file unreadable'
  return `${unit.stage ?? '?'}${unit.failed !== null ? ' failed' : ''}`
}

/** What a unit's build says: `build: slot 1 3m`, `build: queue 2`, or nothing. */
export function buildText(unit: Unit, nowMs: number): string {
  if (unit.build === null) return ''
  if (unit.build.state === 'running') {
    return `build: slot ${unit.build.slot ?? '?'} ${unit.build.since ? elapsed(unit.build.since, nowMs) : ''}`.trimEnd()
  }
  return `build: queue ${unit.build.position ?? '?'}`
}

/** A unit's pull request in a few words: `PR #812`, `merged 4m ago`, or nothing. */
export function prText(unit: Unit, nowMs: number): string {
  if (unit.pr === null) return ''
  if (unit.pr.state === 'open') return `PR #${unit.pr.number}`
  return `merged ${unit.pr.merged_at ? elapsed(unit.pr.merged_at, nowMs) : ''} ago`
}

/** `text` cut to `width` characters, ending in an ellipsis when it was cut. */
export function clip(text: string, width: number): string {
  const chars = [...text]
  return chars.length <= width ? text : `${chars.slice(0, Math.max(0, width - 1)).join('')}…`
}

/**
 * `tokens` (each cut to `width`) grouped into lines that fit `width` when joined by `sep`: a token never splits, so a
 * compact pane can draw each one in its own colour and still know where a line ends. Always at least one (maybe empty) line.
 */
export function packLines(tokens: string[], sep: string, width: number): string[][] {
  const lines: string[][] = []
  const sepWidth = [...sep].length
  let line: string[] = []
  let used = 0
  for (const token of tokens.map(one => clip(one, width))) {
    const size = [...token].length
    if (line.length > 0 && used + sepWidth + size > width) {
      lines.push(line)
      line = [token]
      used = size
    } else {
      used = line.length === 0 ? size : used + sepWidth + size
      line.push(token)
    }
  }
  if (line.length > 0 || lines.length === 0) lines.push(line)
  return lines
}

/** `tokens` joined by `sep` and broken into lines no wider than `width`; a token wider than a line alone is clipped. */
export function wrapTokens(tokens: string[], sep: string, width: number): string[] {
  return packLines(tokens, sep, width).map(line => line.join(sep))
}

const slashes = (path: string): string => path.replace(/\\/g, '/').replace(/\/+$/, '').toLowerCase()

/** Whether `cwd` is the worktree of `unit` or a folder inside it. */
export function inWorktree(unit: Unit, cwd: string): boolean {
  if (unit.worktree === null) return false
  const tree = slashes(unit.worktree)
  const here = slashes(cwd)
  return here === tree || here.startsWith(`${tree}/`)
}

/** The status line: `role · issue · stage` for a session in a unit's worktree, a short summary for the coordinator. */
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
  // The unit and its stage as the pane's row names them (the issue, else the task, and the same stage words).
  return unit === undefined ? role : `${role} · ${issueIds(unit) || unit.task} · ${stageText(unit)}`
}
