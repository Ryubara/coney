import { expect, mock, test } from 'claude-code/testing'

import type { ZoneStatus } from '../types'

// A made-up zone status: the reviewer role with the given page ids waiting.
function status(waiting: string[], idleWake: number | null = null): ZoneStatus {
  return {
    time: '2026-01-01T00:00:00Z',
    account: 'reviewer-x',
    zone: 'reviewer-1',
    role: 'page-reviewer-1',
    brief: 'C:\\Machine\\roles\\page-reviewer-1.md',
    idle_wake_minutes: idleWake,
    inbox: {
      role: 'page-reviewer-1',
      active: true,
      stations: {
        pages: {
          role: 'page-reviewer-1',
          pages: waiting.map(id => ({ id, state: 'awaiting-reviewer-1', verdict: null })),
          counts: { 'awaiting-reviewer-1': waiting.length },
          action: waiting,
        },
      },
      action: waiting.map(id => `pages:${id}`),
    },
    error: null,
    walls: { checked: 'T', passed: 3, checks: 3, breach: false, breach_folders: [], legacy_folders: [] },
    pages_in_main: null,
  }
}

test('wakes the session once per change in waiting work', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  let current = status([])
  const prompts: string[] = []
  on('process.run', async (_$, e) => {
    const run = e.argv[0] === 'cmd.exe' ? 'C:\\PD\r\n' : JSON.stringify(current)
    const code = e.argv[0] === 'cmd.exe' || current.inbox?.action.length === 0 ? 0 : 1
    return { value: { exitCode: code, stdout: run, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }
  })
  on('fs.read', async () => ({ value: JSON.stringify({ python: 'py.exe' }) }))
  on('prompt.submit', async (_$, e) => {
    prompts.push(e.text)
    return { text: e.text, context: [] }
  })
  on('command.register', async () => ({ value: { command: 'coney' } }))
  on('ui.open', async () => ({ value: { isPlaced: true } }))
  const lines: string[] = []
  on('ui.status', async (_$, e) => {
    lines.push(String((e as { text?: string }).text))
    return { value: undefined }
  })
  on('ui.toast', async () => ({ value: undefined }))
  on('session.start', async () => ({ cwd: 'C:/Work' }))
  on('turn.start', (_$: unknown, e: any) => ({ turnId: e.turnId }))
  on('turn.complete', (_$: unknown, e: any) => ({ text: e.answer ?? '' }))

  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  expect(prompts).toEqual([])

  current = status(['p-1'])
  await clock.advance(15_000)
  expect(prompts).toEqual([
    'Follow C:\\Machine\\roles\\page-reviewer-1.md\nWoken for new work in your inbox: pages awaiting-reviewer-1 p-1.',
  ])

  await clock.advance(15_000) // the same work still waits: no second wake
  expect(prompts.length).toBe(1)

  await $.turn.start({ text: prompts[0], turnId: 't1' } as any)
  await $.turn.complete({ turnId: 't1', answer: '' } as any)
  current = status(['p-1', 'p-2'])
  await clock.advance(15_000)
  expect(prompts.length).toBe(2)

  expect(lines.at(-1)).toBe('Coney reviewer-1: 2 waiting')

  current = { ...status(['p-1', 'p-2']), walls: { ...status([]).walls, legacy_folders: ['C:\\Zone\\notes'] } }
  await clock.advance(15_000)
  expect(lines.at(-1)).toBe('Coney reviewer-1: LEGACY COPY')
})

// Shared set-up for the wake tests below: returns the prompts submitted and a setter for the waiting work.
function zone(on: any): { prompts: string[]; set: (ids: string[]) => void } {
  let current = status([])
  const prompts: string[] = []
  on('process.run', async (_$: unknown, e: any) => {
    const run = e.argv[0] === 'cmd.exe' ? 'C:\\PD\r\n' : JSON.stringify(current)
    return { value: { exitCode: 0, stdout: run, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }
  })
  on('fs.read', async () => ({ value: JSON.stringify({ python: 'py.exe' }) }))
  on('prompt.submit', async (_$: unknown, e: any) => {
    prompts.push(e.text)
    return { text: e.text, context: [] }
  })
  on('command.register', async () => ({ value: { command: 'coney' } }))
  on('ui.open', async () => ({ value: { isPlaced: true } }))
  on('ui.status', async () => ({ value: undefined }))
  on('ui.toast', async () => ({ value: undefined }))
  on('session.start', async () => ({ cwd: 'C:/Work' }))
  on('turn.start', (_$: unknown, e: any) => ({ turnId: e.turnId }))
  on('turn.complete', (_$: unknown, e: any) => ({ text: e.answer ?? '' }))
  return { prompts, set: ids => (current = status(ids)) }
}

test('one wake while a turn runs: work handled meanwhile never wakes, new work waits for the turn to end', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const z = zone(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()

  const seven = ['p-1', 'p-2', 'p-3', 'p-4', 'p-5', 'p-6', 'p-7']
  z.set(seven)
  await clock.advance(15_000)
  expect(z.prompts.length).toBe(1)

  // The woken turn handles the items one by one, and new work arrives, all while it runs
  await $.turn.start({ text: z.prompts[0], turnId: 't1' } as any)
  for (let n = 6; n >= 1; n--) {
    z.set(seven.slice(0, n))
    await clock.advance(15_000)
  }
  z.set(['p-1', 'p-8'])
  await clock.advance(15_000)
  expect(z.prompts.length).toBe(1)

  await $.turn.complete({ turnId: 't1', answer: '' } as any)
  await clock.advance(15_000) // p-8 is new: one wake now
  expect(z.prompts.length).toBe(2)

  await clock.advance(15_000) // that wake has not started a turn yet: no second one
  expect(z.prompts.length).toBe(2)
  await $.turn.start({ text: z.prompts[1], turnId: 't2' } as any)
  z.set([])
  await $.turn.complete({ turnId: 't2', answer: '' } as any)
  await clock.advance(15_000)
  expect(z.prompts.length).toBe(2)

  z.set(['p-8']) // a handled item that comes back is new work again
  await clock.advance(15_000)
  expect(z.prompts.length).toBe(3)
})

test('a wake no turn picks up while the session sits idle is sent again', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const z = zone(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  z.set(['p-1'])
  await clock.advance(15_000)
  expect(z.prompts.length).toBe(1)
  z.set(['p-1', 'p-2'])
  await clock.advance(60_000) // queued a minute: still waiting for it
  expect(z.prompts.length).toBe(1)
  await clock.advance(2 * 60_000) // idle past the limit with no turn: it was dropped
  expect(z.prompts.length).toBe(2)
})

test('wakes a role with an idle wake after that long idle, never while a subagent works', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const current = status([], 30)
  const prompts: string[] = []
  let agents = [{ id: 'a-1', description: 'an analyst', type: 'general-purpose', status: 'running' }]
  on('process.run', async (_$, e) => {
    const run = e.argv[0] === 'cmd.exe' ? 'C:\\PD\r\n' : JSON.stringify(current)
    return { value: { exitCode: 0, stdout: run, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }
  })
  on('fs.read', async () => ({ value: JSON.stringify({ python: 'py.exe' }) }))
  on('prompt.submit', async (_$, e) => {
    prompts.push(e.text)
    return { text: e.text, context: [] }
  })
  on('agent.list', async () => ({ value: agents }))
  on('command.register', async () => ({ value: { command: 'coney' } }))
  on('ui.open', async () => ({ value: { isPlaced: true } }))
  on('ui.status', async () => ({ value: undefined }))
  on('ui.toast', async () => ({ value: undefined }))
  on('session.start', async () => ({ cwd: 'C:/Work' }))

  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  await clock.advance(20 * 60_000) // idle, but not yet half an hour
  expect(prompts).toEqual([])

  await clock.advance(15 * 60_000) // past it, but a subagent still works: no wake
  expect(prompts).toEqual([])

  agents = [{ ...agents[0], status: 'completed' }]
  await clock.advance(15_000)
  expect(prompts).toEqual([
    'Follow C:\\Machine\\roles\\page-reviewer-1.md\n' +
      'Woken after 30 idle minutes with no subagent at work, for your own work between inbox items.',
  ])

  await clock.advance(15 * 60_000) // woken a quarter hour ago: not again yet
  expect(prompts.length).toBe(1)

  await clock.advance(16 * 60_000)
  expect(prompts.length).toBe(2)
})

test('the wake says why: only the new items, at most eight named, or the repeat after half an hour', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const z = zone(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()

  z.set(['p-1'])
  await clock.advance(15_000)
  await $.turn.start({ text: z.prompts[0], turnId: 't1' } as any)
  await $.turn.complete({ turnId: 't1', answer: '' } as any)

  const ten = Array.from({ length: 10 }, (_, n) => `q-${n}`)
  z.set(['p-1', ...ten])
  await clock.advance(15_000)
  const [first, why] = z.prompts[1].split('\n')
  expect(first).toBe('Follow C:\\Machine\\roles\\page-reviewer-1.md')
  expect(why).not.toContain('p-1')
  expect(why).toContain('pages awaiting-reviewer-1 q-0')
  expect(why).toContain('q-7')
  expect(why).not.toContain('q-8')
  expect(why.endsWith('(+2 more).')).toBe(true)
  await $.turn.start({ text: z.prompts[1], turnId: 't2' } as any)
  await $.turn.complete({ turnId: 't2', answer: '' } as any)

  await clock.advance(31 * 60_000) // the same eleven still wait half an hour on
  expect(z.prompts[2].split('\n')[1]).toBe(
    'Woken because 11 waiting items are still in your inbox 30 minutes after the last wake.',
  )
})
