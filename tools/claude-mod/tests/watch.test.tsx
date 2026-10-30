// SPDX-License-Identifier: GPL-3.0-or-later
// `claude plugin test tools/claude-mod`: the watchers (docs/guides/conclave.md#watchers): a toast and one wake for each
// event a read brings, nothing for an event told before, no idle wake, and the stop guard on pull request problems.
import { expect, test } from 'claude-code/testing'
import type { Engine } from 'claude-code/testing'

import type { EventRow, Snapshot } from '../types'
import { engine, fails, prints, SESSION, snapshot } from './support'
import type { World } from './support'

// --- the watchers (docs/guides/conclave.md#watchers) ----------------------------------------------------------------

/** A snapshot whose events are what `edit` makes of the fixture's four (a failed check, a merge, a unit, a question). */
function withEvents(edit: (events: EventRow[]) => EventRow[]): Snapshot {
  return snapshot(copy => {
    if (copy.events === null || copy.events.error !== null) throw new Error('the fixture holds events')
    copy.events.events = edit(copy.events.events)
  })
}

/** An event of the fixture, by kind. */
function kindOf(events: EventRow[], kind: EventRow['kind']): EventRow {
  const found = events.find(one => one.kind === kind)
  if (found === undefined) throw new Error(`the fixture holds no ${kind}`)
  return found
}

/** The same events with the failed check held (handled) by an agent. */
const held = (events: EventRow[]): EventRow[] =>
  events.map(one => (one.kind === 'check-failed' ? { ...one, state: 'handled', handled_by: 'engineer-2 until later' } : one))

/** The session of `role`, started, with its first GitHub read made. */
async function startAs($: Engine, world: World, role: string): Promise<void> {
  await $.session.start({ ...SESSION })
  await $.classic.SessionStart({ source: 'startup', agent_type: role })
  await world.clock.advance(1000)
}

const MINUTE = 60_000

test('the coordinator is toasted and woken once for the events a read brings, and for no event told before', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')

  // four new events: a count in one toast, one wake that names each and what to do, under the conventions' preamble
  expect(world.toasts).toHaveLength(1)
  expect(world.toasts[0]).toMatch(/^Conclave: 4 new events/)
  expect(world.wakes).toHaveLength(1)
  const lines = (world.wakes[0] ?? '').split('\n')
  expect(lines[0]).toBe(snapshot().config.watch.preamble)
  expect(lines).toHaveLength(5)
  expect(lines.find(line => line.includes('#812'))).toContain('pr hold 812')
  expect(lines.find(line => line.includes('ready for pr open'))).toContain('coney-tools pr open fix-new')

  // a long time of reads that bring the same events: nothing is told again, and no idle wake comes
  await world.clock.advance(30 * MINUTE)
  expect(world.toasts).toHaveLength(1)
  expect(world.wakes).toHaveLength(1)
})

test('a new event is told alone, and only when it is new', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')

  const another = (events: EventRow[]): EventRow[] => [
    ...events,
    { ...kindOf(events, 'question'), id: 'question:fix-new:QUESTION-2.md:later', text: 'fix-new asks: Another?' },
  ]
  world.answer = prints(withEvents(another))
  await world.clock.advance(3 * MINUTE)
  expect(world.toasts.at(-1)).toBe('Conclave: fix-new asks: Another?')
  expect(world.wakes).toHaveLength(2)
  expect(world.wakes[1]?.split('\n').slice(1)).toHaveLength(1)
  expect(world.wakes[1]).toContain('Another?')
})

test('a problem that is held is not told, and is told again when its hold runs out', async ($, on) => {
  const world = engine(on, prints(withEvents(held)))
  await startAs($, world, 'coordinator')
  expect(world.wakes).toHaveLength(1)
  expect(world.wakes[0]).not.toContain('#812')

  // the hold runs out: the problem is open again, with the same id, and is told
  world.answer = prints(snapshot())
  await world.clock.advance(3 * MINUTE)
  expect(world.wakes).toHaveLength(2)
  expect(world.wakes[1]?.split('\n')).toHaveLength(2)
  expect(world.wakes[1]).toContain('#812')

  // held, open, held: a second hold that runs out is a state change too, and is told once more
  world.answer = prints(withEvents(held))
  await world.clock.advance(3 * MINUTE)
  world.answer = prints(snapshot())
  await world.clock.advance(3 * MINUTE)
  expect(world.wakes).toHaveLength(3)
})

test('an event that is gone and comes back is told as a new one', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  world.answer = prints(withEvents(events => events.filter(one => one.kind !== 'unit-ready')))
  await world.clock.advance(3 * MINUTE)
  expect(world.wakes).toHaveLength(1)
  world.answer = prints(snapshot())
  await world.clock.advance(3 * MINUTE)
  expect(world.wakes).toHaveLength(2)
  expect(world.wakes[1]).toContain('ready for pr open')
})

test('what could not be read is not taken for gone', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  world.answer = prints(
    snapshot(copy => {
      if (copy.events === null || copy.events.error !== null) throw new Error('the fixture holds events')
      copy.events.events = copy.events.events.filter(one => one.kind !== 'check-failed')
      copy.events.unread = ['check-failed']
    }),
  )
  await world.clock.advance(3 * MINUTE)
  world.answer = prints(snapshot())
  await world.clock.advance(3 * MINUTE)
  expect(world.wakes).toHaveLength(1)
})

test('a failed events section tells nothing and forgets nothing', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  world.answer = prints(
    snapshot(copy => {
      copy.events = { error: 'gh: rate limited' }
    }),
  )
  await world.clock.advance(3 * MINUTE)
  world.answer = prints(snapshot())
  await world.clock.advance(3 * MINUTE)
  expect(world.wakes).toHaveLength(1)
})

test('the events already told in an earlier session are not told again', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  expect(world.wakes).toHaveLength(1)

  // a new session of the same mod and store
  await $.session.start({ ...SESSION })
  await $.classic.SessionStart({ source: 'startup', agent_type: 'coordinator' })
  await world.clock.advance(5 * MINUTE)
  expect(world.wakes).toHaveLength(1)
  expect(world.toasts).toHaveLength(1)
})

test('the other roles are neither toasted nor woken', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'engineer')
  await world.clock.advance(5 * MINUTE)
  expect(world.toasts).toEqual([])
  expect(world.wakes).toEqual([])
})

test('a session started with no role watches nothing', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })
  await world.clock.advance(5 * MINUTE)
  expect(world.wakes).toEqual([])
})

test('the roles that watch are the ones the snapshot names, not a name in the mod', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        copy.config.watch.roles = ['engineer']
      }),
    ),
  )
  await startAs($, world, 'engineer')
  expect(world.wakes).toHaveLength(1)
})

test('a wake waits while the person types, and goes after the deferral', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  world.draft = 'half a thought'
  await startAs($, world, 'coordinator')
  const defer = snapshot().config.watch.defer_typing_minutes
  // the toast does not wait; the wake does
  expect(world.toasts).toHaveLength(1)
  await world.clock.advance((defer - 2) * MINUTE)
  expect(world.wakes).toEqual([])
  await world.clock.advance(4 * MINUTE)
  expect(world.wakes).toHaveLength(1)
})

test('a wake does not come sooner than the gap after the last one', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  expect(world.wakes).toHaveLength(1)
  const second = (events: EventRow[]): EventRow[] => [...events, { ...kindOf(events, 'question'), id: 'q2', text: 'fix-new asks: Two?' }]
  world.answer = prints(withEvents(second))
  // the next GitHub read is 120 s away, past the 60 s gap: it goes with that read, not sooner
  await world.clock.advance(100_000)
  expect(world.wakes).toHaveLength(1)
  await world.clock.advance(40_000)
  expect(world.wakes).toHaveLength(2)
})

test('a wake the engine refuses is tried again after the resend time, and once taken is not sent twice', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  world.isWakeRefused = true
  await startAs($, world, 'coordinator')
  expect(world.wakes).toEqual([])
  const resend = snapshot().config.watch.resend_minutes
  world.isWakeRefused = false
  await world.clock.advance((resend - 1) * MINUTE)
  expect(world.wakes).toEqual([])
  await world.clock.advance(2 * MINUTE)
  expect(world.wakes).toHaveLength(1)
  await world.clock.advance(10 * MINUTE)
  expect(world.wakes).toHaveLength(1)
})

test('a session whose GitHub read never worked wakes for nothing', async ($, on) => {
  const world = engine(on, fails)
  await startAs($, world, 'coordinator')
  await world.clock.advance(10 * MINUTE)
  expect(world.wakes).toEqual([])
})

test('the stop guard holds the end of a coordinator turn for a problem nobody holds, a few times, then lets it end', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  const watch = snapshot().config.watch
  const before = world.runs.length

  const first = await $.classic.Stop({ stop_hook_active: false })
  expect(first.block).toContain(watch.stop_preamble)
  expect(first.block).toContain('#812')
  // it looked again at GitHub before it blocked
  expect(world.runs.length).toBe(before + 1)
  expect(world.runs.at(-1)).toContain('--github')

  for (let n = 1; n < watch.stop_max_blocks; n += 1) {
    expect((await $.classic.Stop({ stop_hook_active: true })).block).toBeDefined()
  }
  // past the most in a row for one reason the turn ends
  expect((await $.classic.Stop({ stop_hook_active: true })).block).toBeUndefined()
})

test('the stop guard lets the turn end once the problem is held, as the fresh read says', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  world.answer = prints(withEvents(held))
  expect((await $.classic.Stop({ stop_hook_active: false })).block).toBeUndefined()
})

test('the stop guard does not look again when nothing is open', async ($, on) => {
  const world = engine(on, prints(withEvents(events => events.filter(one => !one.guard))))
  await startAs($, world, 'coordinator')
  const reads = world.runs.length
  expect((await $.classic.Stop({ stop_hook_active: false })).block).toBeUndefined()
  expect(world.runs.length).toBe(reads)
})

test('the stop guard holds no engineer', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'engineer')
  expect((await $.classic.Stop({ stop_hook_active: false })).block).toBeUndefined()
})

test('the stop guard never blocks on a list it could not make current', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  world.answer = fails
  // the last GitHub read is older than three watch intervals and the fresh one fails
  await world.clock.advance(20 * MINUTE)
  expect((await $.classic.Stop({ stop_hook_active: false })).block).toBeUndefined()
})

test('the stop guard names a refresh that failed, and keeps the problems of the list it holds', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  world.answer = fails
  // the last GitHub read is still recent, so the list stands, with the reason it could not be made newer
  const done = await $.classic.Stop({ stop_hook_active: false })
  expect(done.block).toContain('#812')
  expect(done.block).toContain('could not refresh: gh: not signed in')
  expect(done.block).not.toContain('more')
})

test('a refresh that keeps failing does not keep the turn from ending: the count is by problem, not by error text', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  const watch = snapshot().config.watch
  world.answer = fails
  let n = 0
  world.answerRun = () => ({ ...fails, stderr: `error ${(n += 1)}
` })

  for (let blocks = 0; blocks < watch.stop_max_blocks; blocks += 1) {
    expect((await $.classic.Stop({ stop_hook_active: blocks > 0 })).block).toContain('could not refresh: error')
  }
  expect((await $.classic.Stop({ stop_hook_active: true })).block).toBeUndefined()
})

test('a refresh that works says nothing of a failure', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await startAs($, world, 'coordinator')
  const done = await $.classic.Stop({ stop_hook_active: false })
  expect(done.block).not.toContain('could not refresh')
})

test('the answer beneath the stop guard is kept whole', async ($, on) => {
  const world = engine(on, prints(withEvents(events => events.filter(one => !one.guard))), {
    beneathStop: { additionalContext: ['from Tether'] },
  })
  await startAs($, world, 'coordinator')
  const done = await $.classic.Stop({ stop_hook_active: false })
  expect(done.additionalContext).toEqual(['from Tether'])
})

test('a block already set beneath the stop guard is kept whole, and the guard does not count a block it did not make', async ($, on) => {
  const beneathStop: Record<string, unknown> = { block: 'Tether: two agents are still running', additionalContext: ['x'] }
  const world = engine(on, prints(snapshot()), { beneathStop })
  await startAs($, world, 'coordinator')
  const watch = snapshot().config.watch
  const before = world.runs.length

  // Tether's block ends up as it was: Conclave's reason is not added to it, nor does it replace it
  for (let n = 0; n <= watch.stop_max_blocks; n += 1) {
    const done = await $.classic.Stop({ stop_hook_active: n > 0 })
    expect(done.block).toBe('Tether: two agents are still running')
    expect(done.additionalContext).toEqual(['x'])
  }
  // it did not even read GitHub for a block it would not use
  expect(world.runs.length).toBe(before)

  // once nothing is blocked beneath, the guard still has all its blocks: Tether's turn ends were not counted
  delete beneathStop.block
  for (let n = 0; n < watch.stop_max_blocks; n += 1) {
    expect((await $.classic.Stop({ stop_hook_active: n > 0 })).block).toContain(watch.stop_preamble)
  }
  expect((await $.classic.Stop({ stop_hook_active: true })).block).toBeUndefined()
})
