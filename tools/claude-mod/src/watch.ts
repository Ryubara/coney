// SPDX-License-Identifier: GPL-3.0-or-later
// The watchers (docs/guides/conclave.md#watchers): Coney's own events told to the coordinator once. After each read the
// events of the snapshot are toasted when first seen and gathered into one wake; the stop guard keeps the coordinator from
// ending a turn on a pull request problem nobody holds. The plain decisions are in events.ts; this file makes the calls.
//
// Never an idle wake, and nothing about the session's health: those stay with Tether (docs/guides/conclave.md#boundary).
import type { EngineInterface } from 'claude-code'

import type { Held, Watched } from '../types'
import {
  EMPTY_WATCHED,
  eventsOf,
  guardEvents,
  isFresh,
  isWatched,
  markWoken,
  pending,
  reconcile,
  refused,
  stopReason,
  stopVerdict,
  toastTexts,
  wakeHeld,
  wakeText,
  watcherSession,
} from './events'
import { merged, runSnapshot, SCHEMA } from './snapshot'
import { changeHeld, getHeld, getRole, getStops, setStops } from './state'

/** The key of what the watchers remember across sessions in `$.store`: the events told and the wake's pacing. */
const WATCH_KEY = 'watch'

/** Whether a pass of the watchers is running: the next read's pass waits for the next read rather than overlap. */
let isRunning = false

/** What the watchers remember, or nothing when no valid record is stored. */
async function readWatched($: EngineInterface): Promise<Watched> {
  const stored = await $.store.get(WATCH_KEY)
  return isWatched(stored) ? stored : EMPTY_WATCHED
}

/**
 * One pass of the watchers over the held snapshot, after a read: toasts for the events seen for the first time, and one
 * wake for the ones not yet woken once the gap, the resend time and a draft allow. Only a session of a watch role acts,
 * and only on a recent GitHub read (a stale list could wake for what was handled since).
 */
export async function watchEvents($: EngineInterface, state: Held, nowMs: number): Promise<void> {
  const snapshot = state.snapshot
  if (isRunning || snapshot === null || !watcherSession(snapshot, await getRole($))) return
  const found = eventsOf(snapshot)
  const { watch } = snapshot.config
  if (found === null || !isFresh(state, nowMs, watch)) return
  isRunning = true
  try {
    const before = await readWatched($)
    const { watched, fresh } = reconcile(before, found.events, found.unread, nowMs)
    for (const text of toastTexts(fresh)) $.ui.toast(text)
    let after = watched
    const due = pending(watched, found.events)
    if (due.length > 0) {
      const hasDraft = (await $.prompt.read()).text !== ''
      if (!wakeHeld(watched, due, { nowMs, hasDraft }, watch)) {
        const ids = due.map(one => one.id)
        // Recorded before the wake goes, so a session that ends mid-way never tells the same events twice.
        after = markWoken(watched, ids, nowMs)
        await $.store.set(WATCH_KEY, after)
        const answer = await takeWake($, wakeText(watch.preamble, due))
        if (answer !== null) after = refused(after, ids, nowMs, watch)
      }
    }
    if (JSON.stringify(after) !== JSON.stringify(before)) await $.store.set(WATCH_KEY, after)
  } finally {
    isRunning = false
  }
}

/** Submits a wake; null when the engine took it, else why it did not (a refusal, or a call that failed). */
async function takeWake($: EngineInterface, wake: string): Promise<string | null> {
  try {
    const result = await $.prompt.submit({ text: wake })
    return result.drop === undefined ? null : result.drop
  } catch (error) {
    return String(error)
  }
}

/**
 * The stop guard's reason for a turn that is about to end, or null to let it end. It looks again at GitHub first when
 * the last read had a pull request problem nobody holds (a hold recorded since, or a problem that fixed itself, counts),
 * blocks at most `stop_max_blocks` times in a row for one reason, and never blocks on a list it could not make current.
 * A guard that fails lets the turn end: it must never keep a session from stopping by its own fault.
 */
export async function guardReason($: EngineInterface): Promise<string | null> {
  try {
    const state = await getHeld($)
    const snapshot = state.snapshot
    if (snapshot === null || !watcherSession(snapshot, await getRole($))) return null
    const { watch } = snapshot.config
    let events = guardEvents(snapshot)
    if (events.length > 0) {
      const result = await runSnapshot($, true)
      const at = await $.clock.now()
      if (result.ok) {
        await changeHeld($, held => merged(held, result, true, at))
        events = result.snapshot.schema > SCHEMA ? [] : guardEvents(result.snapshot)
      } else if (!isFresh(state, at, watch)) {
        events = []
      }
    }
    const reason = events.length === 0 ? null : stopReason(watch.stop_preamble, events)
    const verdict = stopVerdict(await getStops($), reason, await $.clock.now(), watch)
    await setStops($, verdict.next)
    return verdict.block ? reason : null
  } catch {
    return null
  }
}
