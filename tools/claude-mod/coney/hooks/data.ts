// Plain helpers for what the mod reads: the command line of `coney-tools coord status`, how a local read and a
// --github read combine, which ids wake the session, and how times are told. The reads themselves are made in
// register.tsx, beside the hooks.
import type { CoordStatus, Reading } from '../types'

/** The coney-tools command line for `coord status`, run from the checkout at `repo` (the session's folder), the way
 * .mcp.json runs coney-tools: uv's own cached environment, the lock file as it is, no output but the report. */
export function coordArgv(repo: string, github: boolean): string[] {
  const project = `${repo.replace(/[\\/]+$/, '')}\\python`
  const argv = ['uv', 'run', '--isolated', '--frozen', '--quiet', '--project', project, 'coney-tools']
  return [...argv, 'coord', 'status', '--json', ...(github ? ['--github'] : [])]
}

/** Action kinds only a --github read can tell (coord_cli.GITHUB_KINDS): a local read never has them, so they come from
 * the last --github read. */
export const GITHUB_KINDS = ['issue', 'pr', 'unit']

/** Ids whose wakes another mod owns: Tether's prs poller wakes the session for pull-request problems (its config:
 * docs/guides/coordinator-mod.md), so they are shown here and never wake it twice. */
export const OWNED_ELSEWHERE = ['pr:']

const kind = (id: string) => id.slice(0, Math.max(0, id.indexOf(':')))

/** The report the panes draw: the local sections of the newest read, the GitHub sections and ids of the last --github
 * read, and the local ids of the newest read. */
export function merged(r: Reading): CoordStatus | null {
  const { last, github } = r
  if (!last) return github
  const local = last.action.filter(id => !GITHUB_KINDS.includes(kind(id)))
  const remote = (github?.action ?? []).filter(id => GITHUB_KINDS.includes(kind(id)))
  // A --github read's release section knows which units are queued; a newer local read's only lists the files
  const units = last.github ? last.release : github?.release && !github.release.error ? withQueued(last, github) : last.release
  return {
    ...last,
    github: !!github,
    issues: github?.issues ?? null,
    prs: github?.prs ?? null,
    ci: github?.ci ?? null,
    release: units,
    action: [...new Set([...local, ...remote])].sort(),
  }
}

// The local read's units, each with what the last --github read knew of it (a file that is new since then: unknown).
function withQueued(last: CoordStatus, github: CoordStatus): CoordStatus['release'] {
  const known = new Map((github.release.units ?? []).map(u => [`${u.branch}/${u.file}`, u]))
  return {
    ...last.release,
    units: (last.release.units ?? []).map(u => {
      const k = known.get(`${u.branch}/${u.file}`)
      return k && k.changed === u.changed ? { ...u, pr: k.pr, pr_state: k.pr_state, queued: k.queued } : u
    }),
  }
}

/** Every id that waits for the coordinator, sorted. */
export function waiting(s: CoordStatus | null): string[] {
  return [...(s?.action ?? [])].sort()
}

/** The ids this mod wakes the session for: every waiting one but those another mod owns. */
export function wakeable(s: CoordStatus | null): string[] {
  return waiting(s).filter(id => !OWNED_ELSEWHERE.some(p => id.startsWith(p)))
}

/** Why something failed, in one line. */
export function why(err: unknown): string {
  return err instanceof Error ? err.message : String(err)
}

/** The last line a command wrote to standard error, or its exit code. */
export function failed(r: { exitCode: number | null; stderr: string }): string {
  return r.stderr.trim().split('\n').pop() || `exit ${r.exitCode}`
}

/** HH:MM:SS of an ISO time (UTC), or a dash. */
export function clock(iso: string | null | undefined): string {
  return iso && iso.length >= 19 ? iso.slice(11, 19) : '--:--:--'
}

/** How long ago (or how long until, for a time ahead) an ISO time is: 45s, 12m, 3h, 2d. */
export function ago(iso: string | null | undefined, nowMs: number): string {
  if (!iso) return '-'
  const t = Date.parse(iso)
  if (Number.isNaN(t)) return '?'
  const s = Math.round(Math.abs(nowMs - t) / 1000)
  if (s < 90) return `${s}s`
  if (s < 90 * 60) return `${Math.round(s / 60)}m`
  if (s < 36 * 3600) return `${Math.round(s / 3600)}h`
  return `${Math.round(s / 86400)}d`
}

// Folders the Files tab's tree never lists: tool output and caches.
export const HIDDEN = new Set(['.git', 'node_modules', '__pycache__', '.venv', '.ruff_cache', '.mypy_cache', 'build'])

/** A preview reads a file up to this size. */
export const PREVIEW_BYTES = 256 * 1024
