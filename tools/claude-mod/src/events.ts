// SPDX-License-Identifier: GPL-3.0-or-later
// The watchers' plain functions (docs/guides/conclave.md#watchers): which events are new, when a wake goes, what it says
// and when the stop guard lets a turn end. No engine call is made here: watch.ts does the calling. Every number comes from
// the snapshot's `config.watch` (the `conclave.watch` keys of .github/conventions.yml); the mod holds none of them.
//
// The rule that keeps the coordinator's wakes honest: an event is told once, until its state changes. It is told when the
// watchers first see it open; it is forgotten when it is handled or gone, so the same event opening again is told again,
// and a handled or vanished event never is.
import type { EventRow, Held, SeenEvent, Snapshot, StopState, WatchConfig, Watched } from '../types'

export const EMPTY_WATCHED: Watched = { seen: [], lastWakeAt: 0, retryAt: 0 }

/** How many new events get a toast each before the rest are folded into one. */
const TOASTS = 3
/** A snapshot older than this many GitHub intervals of the watchers is not acted on. */
const FRESH_INTERVALS = 3

/** Whether a stored value has the shape of what the watchers remember. */
export function isWatched(value: unknown): value is Watched {
  const found = value as Partial<Watched> | null
  if (found === null || typeof found !== 'object') return false
  if (typeof found.lastWakeAt !== 'number' || typeof found.retryAt !== 'number' || !Array.isArray(found.seen)) return false
  return found.seen.every(
    (one: Partial<SeenEvent> | null) =>
      one !== null &&
      typeof one === 'object' &&
      typeof one.id === 'string' &&
      typeof one.kind === 'string' &&
      typeof one.seenAt === 'number' &&
      typeof one.wokeAt === 'number',
  )
}

/** The events of a snapshot and the kinds that could not be read, or null when it carries none to trust. */
export function eventsOf(snapshot: Snapshot | null): { events: EventRow[]; unread: string[] } | null {
  const section = snapshot?.events
  if (section === undefined || section === null || section.error !== null) return null
  return { events: section.events, unread: section.unread }
}

/** Whether the session with this role is told of Coney's own events (the `conclave.watch.roles`). */
export function watcherSession(snapshot: Snapshot, role: string | null): boolean {
  return role !== null && snapshot.config.watch.roles.includes(role)
}

/** Whether the last GitHub read is recent enough to act on: a stale list could wake for what has been handled since. */
export function isFresh(held: Held, nowMs: number, watch: WatchConfig): boolean {
  return held.githubAt > 0 && nowMs - held.githubAt <= FRESH_INTERVALS * watch.github_seconds * 1000
}

/**
 * Brings what is remembered in step with the events: an open one not seen before is new (`fresh`) and remembered; a
 * handled one is forgotten (so it is new again if it opens again); one that left the list is forgotten, unless its
 * kind could not be read this time, which says nothing about it.
 */
export function reconcile(
  watched: Watched,
  events: EventRow[],
  unread: string[],
  nowMs: number,
): { watched: Watched; fresh: EventRow[] } {
  const known = new Map(watched.seen.map(one => [one.id, one]))
  const open = new Map(events.filter(one => one.state === 'open').map(one => [one.id, one]))
  const kept: SeenEvent[] = []
  for (const one of watched.seen) {
    if (open.has(one.id) || unread.includes(one.kind)) kept.push(one)
  }
  const fresh = [...open.values()].filter(one => !known.has(one.id))
  for (const one of fresh) kept.push({ id: one.id, kind: one.kind, seenAt: nowMs, wokeAt: 0 })
  return { watched: { ...watched, seen: kept }, fresh }
}

/** The open events that have not been woken for yet, in the snapshot's order. */
export function pending(watched: Watched, events: EventRow[]): EventRow[] {
  const woken = new Set(watched.seen.filter(one => one.wokeAt > 0).map(one => one.id))
  const seen = new Set(watched.seen.map(one => one.id))
  return events.filter(one => one.state === 'open' && seen.has(one.id) && !woken.has(one.id))
}

/**
 * Whether a wake for `due` waits now: for the refused wake's resend time, for the gap since the last wake, and for a
 * draft in the prompt box (a person typing), which holds it only for `defer_typing_minutes` from when the oldest of the
 * events was first seen. Nothing due, nothing held.
 */
export function wakeHeld(
  watched: Watched,
  due: EventRow[],
  gate: { nowMs: number; hasDraft: boolean },
  watch: WatchConfig,
): boolean {
  if (due.length === 0) return false
  if (gate.nowMs < watched.retryAt) return true
  if (gate.nowMs - watched.lastWakeAt < watch.wake_gap_seconds * 1000) return true
  if (!gate.hasDraft) return false
  const ids = new Set(due.map(one => one.id))
  const first = Math.min(...watched.seen.filter(one => ids.has(one.id)).map(one => one.seenAt))
  return gate.nowMs - first < watch.defer_typing_minutes * 60_000
}

/** The memory after a wake for `ids` is taken: they are woken, and the gap counts from now. */
export function markWoken(watched: Watched, ids: string[], nowMs: number): Watched {
  const named = new Set(ids)
  return {
    ...watched,
    seen: watched.seen.map(one => (named.has(one.id) ? { ...one, wokeAt: nowMs } : one)),
    lastWakeAt: nowMs,
  }
}

/** The memory after the engine refused a wake: the events are due again, but not before the resend time. */
export function refused(watched: Watched, ids: string[], nowMs: number, watch: WatchConfig): Watched {
  const named = new Set(ids)
  return {
    seen: watched.seen.map(one => (named.has(one.id) ? { ...one, wokeAt: 0 } : one)),
    lastWakeAt: 0,
    retryAt: nowMs + watch.resend_minutes * 60_000,
  }
}

/** The text of a wake: the preamble from the conventions, then a line for each event and what to do about it. */
export function wakeText(preamble: string, events: EventRow[]): string {
  return [preamble, ...events.map(one => `- ${one.text} -> ${one.do}`)].join('\n')
}

/** The toasts for new events: one each, or a single count when there are more than a few. */
export function toastTexts(events: EventRow[]): string[] {
  if (events.length === 0) return []
  if (events.length <= TOASTS) return events.map(one => `Conclave: ${one.text}`)
  const first = events.slice(0, 2).map(one => one.text)
  return [`Conclave: ${events.length} new events: ${first.join(', ')}, ...`]
}

/** The open events the stop guard holds a turn end for: pull request problems nobody holds. */
export function guardEvents(snapshot: Snapshot | null): EventRow[] {
  return (eventsOf(snapshot)?.events ?? []).filter(one => one.guard && one.state === 'open')
}

/** The stop guard's reason: its preamble from the conventions, then a line for each problem and what to do about it. */
export function stopReason(preamble: string, events: EventRow[]): string {
  return wakeText(preamble, events)
}

/**
 * Whether the stop guard blocks this turn end, and its count after. It blocks at most `stop_max_blocks` times in a row
 * for one reason; a new reason, or `stop_reset_minutes` without a block, counts again from one; no reason clears it.
 */
export function stopVerdict(
  state: StopState | null,
  reason: string | null,
  nowMs: number,
  watch: WatchConfig,
): { block: boolean; next: StopState | null } {
  if (reason === null) return { block: false, next: null }
  const isSame = state !== null && state.reason === reason && nowMs - state.lastBlockAt < watch.stop_reset_minutes * 60_000
  if (!isSame) return { block: true, next: { reason, blocks: 1, lastBlockAt: nowMs } }
  if (state.blocks >= watch.stop_max_blocks) return { block: false, next: state }
  return { block: true, next: { reason, blocks: state.blocks + 1, lastBlockAt: nowMs } }
}
