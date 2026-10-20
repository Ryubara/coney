import { expect, mock, test } from 'claude-code/testing'

import type { CoordStatus } from '../types'

// A made-up coord status with the given action ids: the local kinds in every read, the GitHub kinds only in a
// --github read, as coney-tools gives them.
function coord(ids: string[], github: boolean): CoordStatus {
  const local = ids.filter(i => !/^(issue|pr|unit):/.test(i))
  return {
    time: '2026-01-01T00:00:00Z',
    checkout: 'C:\\Work',
    github,
    worktrees: { error: null, count: 2 },
    pcsx2: { error: null, copies: [] },
    builds: { error: null, lanes: { main: { slots: [{ slot: 1, stale: false, claim: null }], queue: [] } } },
    results: { error: null, results: [] },
    tracks: { error: null, tracks: [] },
    release: { error: null, units: [] },
    issues: github ? { error: null, issues: [], counts: {}, notices: [] } : null,
    prs: github ? { error: null, prs: [], health: { stuck: [] } } : null,
    ci: github ? { error: null, queued: 0, in_progress: 0 } : null,
    action: github ? [...ids].sort() : local.sort(),
  }
}

const ok = (stdout: string) => ({
  value: { exitCode: 0, stdout, stderr: '', isStdoutTruncated: false, isStderrTruncated: false },
})

// Shared set-up: coney-tools answers with the waiting ids `set` gives; returns the prompts, toasts, status lines,
// the commands run and a switch to refuse wakes.
function coordinator(on: any, opts: { agents?: () => any[]; idle?: string } = {}) {
  let ids: string[] = []
  const prompts: string[] = []
  const toasts: string[] = []
  const lines: string[] = []
  const runs: string[][] = []
  const state = { refuse: false, fail: false }
  on('process.run', async (_$: unknown, e: any) => {
    runs.push(e.argv)
    if (state.fail) return { value: { exitCode: 2, stdout: '', stderr: 'coney-tools: boom', isStdoutTruncated: false, isStderrTruncated: false } }
    return ok(JSON.stringify(coord(ids, e.argv.includes('--github'))))
  })
  on('prompt.submit', async (_$: unknown, e: any) => {
    prompts.push(e.text)
    return state.refuse ? { drop: 'a Stop stands' } : { text: e.text, context: [] }
  })
  on('agent.list', async () => ({ value: opts.agents ? opts.agents() : [] }))
  on('env.get', async (_$: unknown, e: any) => ({ value: e.name === 'CONEY_IDLE_WAKE_MINUTES' ? opts.idle : undefined }))
  on('command.register', async (_$: unknown, e: any) => ({ value: { command: e.name } }))
  on('ui.open', async () => ({ value: { isPlaced: true } }))
  on('ui.status', async (_$: unknown, e: any) => {
    lines.push(String(e.text))
    return { value: undefined }
  })
  on('ui.toast', async (_$: unknown, e: any) => {
    toasts.push(String(e.text))
    return { value: undefined }
  })
  on('session.start', async () => ({ cwd: 'C:/Work' }))
  on('session.cwd', () => ({ value: 'C:\\Work' }))
  on('turn.start', (_$: unknown, e: any) => ({ turnId: e.turnId }))
  on('turn.complete', (_$: unknown, e: any) => ({ text: e.answer ?? '' }))
  return { prompts, toasts, lines, runs, state, set: (next: string[]) => (ids = next) }
}

async function turn($: any, text: string, turnId: string) {
  await $.turn.start({ text, turnId } as any)
  await $.turn.complete({ turnId, answer: '' } as any)
}

test('reads coord status from the checkout every 15 s, with --github every two minutes', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  const reads = () => c.runs.filter(r => r[0] === 'uv')
  expect(reads()[0]).toEqual([
    'uv', 'run', '--isolated', '--frozen', '--quiet', '--project', 'C:\\Work\\python', 'coney-tools',
    'coord', 'status', '--json', '--github',
  ]) // prettier-ignore
  await clock.advance(15_000)
  expect(reads()[1]).not.toContain('--github')
  await clock.advance(7 * 15_000)
  expect(reads().filter(r => r.includes('--github'))).toHaveLength(2)
  expect(reads()).toHaveLength(9)
})

test('wakes the session once per change in waiting work, naming the new ids', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on, { agents: () => [{ id: 'a', status: 'running' }] })
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  expect(c.prompts).toEqual([])

  c.set(['build:done:wt-a'])
  await clock.advance(15_000)
  expect(c.prompts).toEqual(['Coordinator: new work.\nbuild:done:wt-a.'])
  await clock.advance(15_000) // the same work still waits: no second wake
  expect(c.prompts.length).toBe(1)
  expect(c.lines.at(-1)).toBe('Coney: 1 waiting · 1 agent')

  await turn($, c.prompts[0], 't1')
  c.set(['build:done:wt-a', 'build:done:wt-b'])
  await clock.advance(15_000)
  expect(c.prompts[1]).toBe('Coordinator: new work.\nbuild:done:wt-b.')
})

test('GitHub ids come from the last --github read and stay between local reads', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  c.set(['issue:12:S-needs-triage'])
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  expect(c.prompts).toEqual(['Coordinator: new work.\nissue:12:S-needs-triage.'])
  await turn($, c.prompts[0], 't1')
  // local reads lack the issue: it still waits, so neither handled nor new
  for (let n = 0; n < 6; n++) await clock.advance(15_000)
  expect(c.prompts.length).toBe(1)
  expect(c.lines.at(-1)).toBe('Coney: 1 waiting · 0 agents')
  // the issue is triaged: the next --github read drops it; a new unit then wakes for itself alone
  c.set(['unit:core-x:PR.md'])
  await clock.advance(2 * 15_000)
  expect(c.prompts[1]).toBe('Coordinator: new work.\nunit:core-x:PR.md.')
})

test("pull-request problems are Tether's: shown and counted elsewhere, never a wake", async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  c.set(['pr:7:conflict', 'pr:8:checks-failed'])
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  await clock.advance(40 * 60_000)
  expect(c.prompts).toEqual([])
  expect(c.lines.at(-1)).toBe('Coney: 0 waiting · 0 agents')
  c.set(['pr:7:conflict', 'issue:3:S-needs-triage'])
  await clock.advance(2 * 60_000)
  expect(c.prompts).toEqual(['Coordinator: new work.\nissue:3:S-needs-triage.'])
})

test('one wake while a turn runs: work handled meanwhile never wakes, new work waits for the turn to end', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  const seven = ['build:done:a', 'build:done:b', 'build:done:c', 'build:done:d', 'build:done:e', 'build:done:f', 'build:done:g']
  c.set(seven)
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(1)
  await $.turn.start({ text: c.prompts[0], turnId: 't1' } as any)
  for (let n = 6; n >= 1; n--) {
    c.set(seven.slice(0, n))
    await clock.advance(15_000)
  }
  c.set(['build:done:a', 'build:done:h'])
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(1)
  await $.turn.complete({ turnId: 't1', answer: '' } as any)
  await clock.advance(15_000) // h is new: one wake now
  expect(c.prompts.length).toBe(2)
  expect(c.prompts[1]).toBe('Coordinator: new work.\nbuild:done:h.')
  await clock.advance(15_000) // that wake has not started a turn yet: no second one
  expect(c.prompts.length).toBe(2)
  await $.turn.start({ text: c.prompts[1], turnId: 't2' } as any)
  c.set([])
  await $.turn.complete({ turnId: 't2', answer: '' } as any)
  await clock.advance(15_000)
  c.set(['build:done:h']) // a handled id that comes back is new work again
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(3)
})

test('a wake no turn picks up while the session sits idle is sent again', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  c.set(['build:done:a'])
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(1)
  await clock.advance(60_000) // queued a minute: still waiting for it
  expect(c.prompts.length).toBe(1)
  await clock.advance(2 * 60_000) // idle past the limit with no turn: dropped, so the same work wakes again
  expect(c.prompts.length).toBe(2)
  expect(c.prompts[1]).toContain('build:done:a')
})

test('a refused wake is told once, tried again after the wait, and after a turn the person starts', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  c.state.refuse = true
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  c.set(['build:done:a'])
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(1)
  await clock.advance(60_000)
  expect(c.prompts.length).toBe(1)
  expect(c.toasts).toEqual(['Coney: the wake was refused (a Stop stands). Type any line in this window to resume.'])
  expect(c.lines.at(-1)).toBe('Coney: 1 waiting · 0 agents · wake refused')
  await clock.advance(75_000) // past the wait: tried again, refused again, told no second time
  expect(c.prompts.length).toBe(2)
  await clock.advance(15_000)
  expect(c.toasts.length).toBe(1)
  c.state.refuse = false
  await $.turn.start({ text: 'go on', turnId: 't1' } as any)
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(2)
  await $.turn.complete({ turnId: 't1', answer: '' } as any)
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(3)
  expect(c.lines.at(-1)).toBe('Coney: 1 waiting · 0 agents')
})

// about twelve hours of 15-second reads: longer than the default timeout on a slow runner
test('the wake names at most eight ids, and repeats after half an hour, doubling to four hours', { timeoutMs: 30_000 }, async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  const ten = Array.from({ length: 10 }, (_, n) => `build:done:w${n}`)
  c.set(ten)
  await clock.advance(15_000)
  const [first, reason] = c.prompts[0].split('\n')
  expect(first).toBe('Coordinator: new work.')
  expect(reason).toContain('build:done:w7')
  expect(reason).not.toContain('build:done:w8')
  expect(reason.endsWith('(+2 more).')).toBe(true)
  await turn($, c.prompts[0], 't1')
  await clock.advance(31 * 60_000)
  expect(c.prompts[1].split('\n')[1]).toMatch(/^Still waiting 30 minutes after the last wake: build:done:w0/)
  await turn($, c.prompts[1], 't2')
  const nextWake = async (turnId: string, expected: number) => {
    const from = c.prompts.length
    let minutes = expected - 2
    await clock.advance(minutes * 60_000)
    while (c.prompts.length === from && minutes < expected + 5) {
      await clock.advance(60_000)
      minutes++
    }
    await turn($, c.prompts[from], turnId)
    return { reason: c.prompts[from].split('\n')[1], minutes }
  }
  for (const [n, wait] of [60, 120, 240, 240].entries()) {
    const { reason, minutes } = await nextWake(`r${n}`, wait)
    expect(reason).toContain(`${wait} minutes after the last wake`)
    expect(minutes).toBeGreaterThanOrEqual(wait - 1)
    expect(minutes).toBeLessThanOrEqual(wait + 1)
  }
})

test('an idle wake comes only when the launcher asks for one, and never while a subagent works', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  let agents = [{ id: 'a-1', description: 'an analyst', type: 'general-purpose', status: 'running' }]
  const c = coordinator(on, { agents: () => agents, idle: '30' })
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  await clock.advance(20 * 60_000)
  expect(c.prompts).toEqual([])
  await clock.advance(15 * 60_000) // past it, but a subagent still works
  expect(c.prompts).toEqual([])
  agents = [{ ...agents[0], status: 'completed' }]
  await clock.advance(15_000)
  expect(c.prompts.length).toBe(1)
  expect(c.prompts[0]).toMatch(/^Coordinator: new work\.\nNothing new; woken after 30 idle minutes/)
})

test('no idle wake without CONEY_IDLE_WAKE_MINUTES', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  await clock.advance(5 * 60 * 60_000)
  expect(c.prompts).toEqual([])
})

test('a failed read is told once and keeps what waits as it was', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  c.set(['build:done:a'])
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  expect(c.prompts.length).toBe(1)
  await turn($, c.prompts[0], 't1')
  c.state.fail = true
  await clock.advance(15_000)
  await clock.advance(15_000)
  expect(c.toasts).toEqual(['Coney: coord status could not be read (coney-tools: boom)'])
  c.state.fail = false
  await clock.advance(15_000) // the same id: not new after the failed reads
  expect(c.prompts.length).toBe(1)
})

test('with no session start, drawing the pane starts nothing; /coney starts the reads, once', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const c = coordinator(on)
  const props = { title: 'Coney', isFocused: false, bodyColumns: 80, placement: 'dock', scroll: { bodyRows: 20 } }
  const reads = () => c.runs.filter(r => r[0] === 'uv').length
  const drawn = await $.ui.mount({ plugin: 'coney', surface: 'terminal', component: 'Pane', requestId: 'coney', props } as any)
  await clock.settle()
  expect(await drawn.find({ type: 'Text', text: /Reading coord status/ })).toBeDefined()
  await drawn.unmount()
  expect(reads()).toBe(0)
  const opened: any = await $.command.run({ command: 'coney' } as any)
  expect(opened.text).toBe('Coney pane opened.')
  await $.command.run({ command: 'coney' } as any)
  await clock.settle()
  expect(reads()).toBe(1)
  await clock.advance(15_000)
  expect(reads()).toBe(2)
})

test('/coney raises the pane in front of another pane, and says why when it is not drawn', async ($, on) => {
  const opens: any[] = []
  let placed = true
  on('ui.open', async (_$: unknown, e: any) => {
    opens.push(e)
    return { value: placed ? { isPlaced: true } : { isPlaced: false, reason: 'no surface is attached' } }
  })
  const ok: any = await $.command.run({ command: 'coney' } as any)
  expect(ok.text).toBe('Coney pane opened.')
  expect(opens.at(-1)).toMatchObject({ id: 'coney', focus: true })
  placed = false
  const waiting: any = await $.command.run({ command: 'coney' } as any)
  expect(waiting.text).toBe('The Coney pane is open but not drawn: no surface is attached')
})
