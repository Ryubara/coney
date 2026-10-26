// SPDX-License-Identifier: GPL-3.0-or-later
// The watchers' plain functions (docs/guides/conclave.md#watchers): which events are new, when a wake goes, what it
// says, and when the stop guard lets a turn end. An event is told once, until its state changes.
import { expect, test } from 'claude-code/testing'

import type { EventRow, Snapshot, Watched } from '../types'
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
} from '../src/events'
import { EMPTY } from '../src/state'
import fixture from './fixtures/snapshot'

const snapshot = (): Snapshot => JSON.parse(JSON.stringify(fixture)) as Snapshot
const NOW = Date.parse('2026-10-23T14:00:00Z')
const MIN = 60_000

/** An open event, changed by `over`. */
function event(id: string, over: Partial<EventRow> = {}): EventRow {
  return {
    id,
    kind: 'check-failed',
    state: 'open',
    ref: { pr: 812, task: null, branch: 'fix-frame-812' },
    since: null,
    text: `text of ${id}`,
    do: `do ${id}`,
    guard: true,
    handled_by: null,
    ...over,
  }
}

const watch = snapshot().config.watch

test('the fixture carries the watch settings and events the mod acts by', () => {
  const snap = snapshot()
  expect(snap.config.watch.roles).toEqual(['coordinator'])
  const found = eventsOf(snap)
  expect(found?.events.map(one => one.kind).sort()).toEqual(['check-failed', 'merged', 'question', 'unit-ready'])
})

test('a snapshot with no events section, or one that could not be read, gives the watchers nothing to act on', () => {
  const snap = snapshot()
  snap.events = null
  expect(eventsOf(snap)).toBeNull()
  snap.events = { error: 'gh: not signed in' }
  expect(eventsOf(snap)).toBeNull()
  expect(eventsOf(null)).toBeNull()
})

test('only the sessions of the watch roles watch', () => {
  const snap = snapshot()
  expect(watcherSession(snap, 'coordinator')).toBe(true)
  expect(watcherSession(snap, 'engineer')).toBe(false)
  expect(watcherSession(snap, null)).toBe(false)
})

test('an event seen for the first time is new, and the same event read again is not', () => {
  const list = [event('a'), event('b')]
  const first = reconcile(EMPTY_WATCHED, list, [], NOW)
  expect(first.fresh.map(one => one.id)).toEqual(['a', 'b'])
  expect(first.watched.seen.map(one => [one.id, one.seenAt, one.wokeAt])).toEqual([
    ['a', NOW, 0],
    ['b', NOW, 0],
  ])
  const again = reconcile(first.watched, list, [], NOW + MIN)
  expect(again.fresh).toEqual([])
  expect(again.watched).toEqual(first.watched)
})

test('an event that is handled is forgotten, and told again when the same one opens again', () => {
  const open = [event('a')]
  const told = markWoken(reconcile(EMPTY_WATCHED, open, [], NOW).watched, ['a'], NOW)
  const handled = reconcile(told, [event('a', { state: 'handled', handled_by: 'engineer-2 until later' })], [], NOW + MIN)
  expect(handled.fresh).toEqual([])
  expect(handled.watched.seen).toEqual([])
  const reopened = reconcile(handled.watched, open, [], NOW + 5 * MIN)
  expect(reopened.fresh.map(one => one.id)).toEqual(['a'])
  expect(pending(reopened.watched, open).map(one => one.id)).toEqual(['a'])
})

test('an event that leaves the list is forgotten, unless its kind could not be read', () => {
  const told = markWoken(reconcile(EMPTY_WATCHED, [event('a'), event('m', { kind: 'merged' })], [], NOW).watched, ['a', 'm'], NOW)
  const gone = reconcile(told, [], [], NOW + MIN)
  expect(gone.watched.seen).toEqual([])
  const unread = reconcile(told, [], ['check-failed'], NOW + MIN)
  expect(unread.watched.seen.map(one => one.id)).toEqual(['a'])
})

test('a wake is for the open events not yet woken, once', () => {
  const list = [event('a'), event('b', { kind: 'question', guard: false })]
  let watched = reconcile(EMPTY_WATCHED, list, [], NOW).watched
  expect(pending(watched, list).map(one => one.id)).toEqual(['a', 'b'])
  watched = markWoken(watched, ['a'], NOW)
  expect(pending(watched, list).map(one => one.id)).toEqual(['b'])
  watched = reconcile(watched, list, [], NOW + MIN).watched
  expect(pending(watched, list).map(one => one.id)).toEqual(['b'])
  expect(watched.lastWakeAt).toBe(NOW)
})

test('a wake waits for the gap, for a refused wake and for a draft, but a draft is not waited for ever', () => {
  const list = [event('a')]
  const seen = reconcile({ ...EMPTY_WATCHED, lastWakeAt: NOW - 30_000 }, list, [], NOW - 15 * MIN).watched
  const gate = (nowMs: number, hasDraft: boolean) => ({ nowMs, hasDraft })
  // within the gap of the last wake
  expect(wakeHeld(seen, list, gate(NOW, false), watch)).toBe(true)
  expect(wakeHeld(seen, list, gate(NOW + 40_000, false), watch)).toBe(false)
  // a draft holds it for the deferral, counted from when the event was first seen, then it goes anyway
  expect(wakeHeld(seen, list, gate(NOW, true), { ...watch, wake_gap_seconds: 1 })).toBe(false)
  const fresh = reconcile(EMPTY_WATCHED, list, [], NOW).watched
  expect(wakeHeld(fresh, list, gate(NOW + MIN, true), watch)).toBe(true)
  expect(wakeHeld(fresh, list, gate(NOW + watch.defer_typing_minutes * MIN, true), watch)).toBe(false)
  // nothing to say, nothing to hold
  expect(wakeHeld(fresh, [], gate(NOW, false), watch)).toBe(false)
})

test('a wake the engine refused puts the events back and waits the resend time', () => {
  const list = [event('a')]
  const sent = markWoken(reconcile(EMPTY_WATCHED, list, [], NOW).watched, ['a'], NOW)
  expect(pending(sent, list)).toEqual([])
  const back = refused(sent, ['a'], NOW, watch)
  expect(pending(back, list).map(one => one.id)).toEqual(['a'])
  expect(back.lastWakeAt).toBe(EMPTY_WATCHED.lastWakeAt)
  expect(back.retryAt).toBe(NOW + watch.resend_minutes * MIN)
  const gate = (nowMs: number) => ({ nowMs, hasDraft: false })
  expect(wakeHeld(back, list, gate(NOW + MIN), watch)).toBe(true)
  expect(wakeHeld(back, list, gate(NOW + watch.resend_minutes * MIN), watch)).toBe(false)
})

test('a wake names each event and what to do about it, under the preamble from the conventions', () => {
  const text = wakeText('Coordinator: new work.', [event('a'), event('b', { kind: 'merged', guard: false })])
  expect(text.split('\n')).toEqual(['Coordinator: new work.', '- text of a -> do a', '- text of b -> do b'])
})

test('toasts: one for each of the first few new events, then one count', () => {
  expect(toastTexts([])).toEqual([])
  expect(toastTexts([event('a'), event('b')])).toEqual(['Conclave: text of a', 'Conclave: text of b'])
  const many = ['a', 'b', 'c', 'd', 'e'].map(id => event(id))
  expect(toastTexts(many)).toEqual(['Conclave: 5 new events: text of a, text of b, ...'])
})

test('the stop guard holds for the open events that guard, and not for handled ones', () => {
  const snap = snapshot()
  expect(guardEvents(snap).map(one => one.kind)).toEqual(['check-failed'])
  if (snap.events === null || snap.events.error !== null) throw new Error('the fixture holds events')
  snap.events.events = snap.events.events.map(one => ({ ...one, state: 'handled', handled_by: 'x' }))
  expect(guardEvents(snap)).toEqual([])
})

test('the stop guard reason is the preamble and a line for each problem', () => {
  const reason = stopReason('Pull requests are in trouble:', [event('a'), event('b')])
  expect(reason.split('\n')).toEqual(['Pull requests are in trouble:', '- text of a -> do a', '- text of b -> do b'])
})

test('the stop guard blocks a turn end up to the most in a row for one reason, then lets it end', () => {
  let state = null as ReturnType<typeof stopVerdict>['next']
  const results: boolean[] = []
  for (let n = 0; n < watch.stop_max_blocks + 2; n += 1) {
    const verdict = stopVerdict(state, 'reason', NOW + n * MIN, watch)
    results.push(verdict.block)
    state = verdict.next
  }
  expect(results).toEqual([...Array<boolean>(watch.stop_max_blocks).fill(true), false, false])
})

test('a new reason starts the count again, and no reason clears it', () => {
  let state = stopVerdict(null, 'one', NOW, watch).next
  for (let n = 1; n < watch.stop_max_blocks; n += 1) state = stopVerdict(state, 'one', NOW + n, watch).next
  expect(stopVerdict(state, 'one', NOW + 99, watch).block).toBe(false)
  expect(stopVerdict(state, 'two', NOW + 99, watch).block).toBe(true)
  expect(stopVerdict(state, null, NOW + 99, watch)).toEqual({ block: false, next: null })
})

test('the count starts again after the reset time without a block', () => {
  let state = stopVerdict(null, 'one', NOW, watch).next
  for (let n = 1; n < watch.stop_max_blocks; n += 1) state = stopVerdict(state, 'one', NOW + n, watch).next
  expect(stopVerdict(state, 'one', NOW + 60_000, watch).block).toBe(false)
  expect(stopVerdict(state, 'one', NOW + watch.stop_reset_minutes * MIN + 10, watch).block).toBe(true)
})

test('a snapshot older than three GitHub intervals of the watchers is not acted on', () => {
  const held = { ...EMPTY, snapshot: snapshot(), githubAt: NOW - 2 * watch.github_seconds * 1000 }
  expect(isFresh(held, NOW, watch)).toBe(true)
  expect(isFresh({ ...held, githubAt: NOW - 4 * watch.github_seconds * 1000 }, NOW, watch)).toBe(false)
  expect(isFresh({ ...held, githubAt: 0 }, NOW, watch)).toBe(false)
})

test('what the watchers remember is checked before it is trusted', () => {
  const good: Watched = { seen: [{ id: 'a', kind: 'merged', seenAt: 1, wokeAt: 0 }], lastWakeAt: 2, retryAt: 3 }
  expect(isWatched(good)).toBe(true)
  expect(isWatched(EMPTY_WATCHED)).toBe(true)
  expect(isWatched(null)).toBe(false)
  expect(isWatched({ ...good, seen: [{ id: 1 }] })).toBe(false)
  expect(isWatched({ ...good, retryAt: 'soon' })).toBe(false)
  expect(isWatched({ seen: 'a' })).toBe(false)
})
