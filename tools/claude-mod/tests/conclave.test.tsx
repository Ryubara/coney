// SPDX-License-Identifier: GPL-3.0-or-later
// `claude plugin test tools/claude-mod`: the band, the status line, the pane and the agent list, drawn through the
// plugin from a fixture snapshot that the Python tests write (python/tests/test_conclave_fixture.py), so the mod's
// tests and the command share one shape.
import type { On } from 'claude-code'
import { expect, mock, test } from 'claude-code/testing'
import type { Engine } from 'claude-code/testing'

import type { Snapshot } from '../types'
import fixture from './fixtures/snapshot'

// The moment the fixture snapshot was taken.
const T0 = Date.parse('2026-10-23T14:00:00Z')

const BAND = {
  plugin: 'conclave',
  component: 'AbovePrompt',
  props: {
    hasSurvey: false,
    isWorking: false,
    maxRows: 10,
    bodyColumns: 160,
    scroll: { offset: 0, bodyRows: 10 },
    view: {},
  },
} as const

const PANE = {
  plugin: 'conclave',
  component: 'Pane',
  requestId: 'conclave',
  props: {
    title: 'Conclave',
    isFocused: false,
    bodyColumns: 130,
    placement: 'dock',
    scroll: { offset: 0, bodyRows: 40 },
    view: {},
  },
} as const

/** Starts the session and lets the first tick make the GitHub read, so the clock stands at T0 and every section is held. */
async function start($: Engine, world: World): Promise<void> {
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)
}

const SESSION = { cwd: '/work/worktrees/fix-frame-812', surface: 'terminal', isInteractive: true } as const

const OFFERED = { description: 'x', source: 'built-in', provider: { plugin: 'engine', tier: 'core' } } as const

/** A copy of the fixture snapshot, changed by `edit`. */
function snapshot(edit?: (copy: Snapshot) => void): Snapshot {
  const copy = JSON.parse(JSON.stringify(fixture)) as Snapshot
  edit?.(copy)
  return copy
}

type Answer = { exitCode: number; stdout: string; stderr: string }
type World = {
  statuses: (string | undefined)[]
  runs: string[][]
  /** The `env` each run was given. */
  envs: (Record<string, string> | undefined)[]
  /** How long each run was given, in milliseconds. */
  timeouts: (number | undefined)[]
  clock: ReturnType<typeof mock.clock>
  /** What the command prints next; change it to move the world on. */
  answer: Answer
  opened: string[]
  stored: Record<string, unknown>
}

/** The snapshot as it reads once claims expire: the fixture was written with `claims.release` still off. */
function enforced(copy: Snapshot): void {
  if (copy.claims !== null && copy.claims.error === null) copy.claims.rules.enforced = true
}

const prints = (snap: Snapshot): Answer => ({ exitCode: 0, stdout: JSON.stringify(snap), stderr: '' })
const fails: Answer = { exitCode: 1, stdout: '', stderr: 'gh: not signed in\nmore\n' }

/** Everything beneath the mod, standing for the engine: the command's answer, the clock, the store and the panes. */
function engine(
  on: On,
  answer: Answer,
  options: { store?: Record<string, unknown>; isPaneShown?: boolean; isHung?: boolean; beneath?: Record<string, unknown> } = {},
): World {
  const world: World = {
    statuses: [],
    runs: [],
    envs: [],
    timeouts: [],
    clock: mock.clock(on, { now: T0 - 1000 }),
    answer,
    opened: [],
    stored: {},
  }
  // The store in memory, kept here so a test can read what the mod stored (mock.store would hide it).
  world.stored = { ...(options.store ?? {}) }
  on('store.get', (_, e) => ({ value: world.stored[e.key] }))
  on('store.set', (_, e) => {
    world.stored[e.key] = e.value
    return { value: undefined }
  })
  on('store.delete', (_, e) => {
    delete world.stored[e.key]
    return { value: undefined }
  })
  on('agent.offer', () => ({ isOffered: true }))
  // What the plugins beneath answer to the session start (Tether's context for the first turn, in a real session).
  on('classic.SessionStart', () => options.beneath ?? {})
  on('session.start', (_, e) => ({ cwd: e.cwd }))
  on('command.register', () => ({ value: { command: 'conclave' } }))
  on('ui.open', (_, e) => {
    world.opened.push(e.id)
    return { value: { isPlaced: true } }
  })
  on('ui.panes', () => ({
    value: options.isPaneShown
      ? [{ id: 'conclave', title: 'Conclave', isShown: true, isFocused: false, isPlaced: true }]
      : [],
  }))
  on('ui.status', (_, e) => {
    world.statuses.push(e.text)
    return { value: undefined }
  })
  on('process.run', (_, e) => {
    world.runs.push([...e.argv])
    world.envs.push(e.init?.env)
    world.timeouts.push(e.init?.timeoutMs)
    // A snapshot command that never answers (a hung `uv`): the session must start all the same.
    if (options.isHung) return new Promise<never>(() => undefined)
    return { value: { ...world.answer, isStdoutTruncated: false, isStderrTruncated: false } }
  })
  return world
}

test('the first read runs the snapshot command from the checkout the mod folder belongs to', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })

  const argv = world.runs[0] ?? []
  expect(argv.slice(0, 3)).toEqual(['uv', 'run', '-q'])
  expect(argv.slice(-3)).toEqual(['conclave', 'snapshot', '--json'])
  expect(argv).not.toContain('--github')
})

test('the band counts units, failures, pull requests, claims, builds and disk on every surface', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  for (const surface of ['terminal', 'desktop'] as const) {
    const ui = await $.ui.mount({ ...BAND, surface })
    expect((await ui.find({ key: 'units' }))?.text).toBe('2 units')
    expect((await ui.find({ key: 'failed' }))?.text).toBe('✖1 failed')
    expect((await ui.find({ key: 'prs' }))?.text).toBe('PRs 1 (✖1 #812)')
    expect((await ui.find({ key: 'claims' }))?.text).toBe('claims 2')
    expect((await ui.find({ key: 'builds' }))?.text).toBe('builds 1 (+1 queued)')
    expect((await ui.find({ key: 'critical' }))?.text).toBe('⚠ P-critical #790')
    expect((await ui.find({ key: 'disk' }))?.text).toBe('disk 61 GB')
    await ui.unmount()
  }
})

test('a claim near its deadline turns the claims item amber', async ($, on) => {
  const world = engine(on, prints(snapshot(enforced)))
  await start($, world)

  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await ui.find({ type: 'Text', text: /claims 2/ }))?.props.color).toBe('warning')
  expect((await ui.find({ type: 'Text', text: /PRs/ }))?.props.color).toBe('error')
  await ui.unmount()
})

for (const [free, color] of [
  [61, undefined],
  [35, 'warning'],
  [12, 'error'],
] as const) {
  test(`the disk figure with ${free} GB free is coloured ${color ?? 'plain'}`, async ($, on) => {
    const world = engine(
      on,
      prints(
        snapshot(copy => {
          if (copy.machine.error === null) copy.machine.free_gb = free
        }),
      ),
    )
    await start($, world)
    const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
    expect((await ui.find({ type: 'Text', text: /disk/ }))?.props.color).toBe(color)
    await ui.unmount()
  })
}

test('a narrow band takes two lines', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  const ui = await $.ui.mount({ ...BAND, surface: 'terminal', props: { ...BAND.props, bodyColumns: 80 } })
  const lines = await ui.findAll({ type: 'Box', key: 'line-0' })
  const second = await ui.findAll({ type: 'Box', key: 'line-1' })
  expect(lines).toHaveLength(1)
  expect(second).toHaveLength(1)
  expect(second[0]?.text).toContain('disk 61 GB')
  await ui.unmount()
})

test("the band takes two lines under the width the snapshot names, not a width of the mod's own", async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        copy.config.band_narrow_columns = 200
      }),
    ),
  )
  await start($, world)

  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect(await ui.findAll({ type: 'Box', key: 'line-1' })).toHaveLength(1)
  await ui.unmount()
})

test('the band goes stale after the number of reads the snapshot names', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        copy.config.stale_after_reads = 20
      }),
    ),
  )
  await $.session.start({ ...SESSION })

  world.answer = fails
  await world.clock.advance(150_000)
  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect(await ui.find({ type: 'Text', text: /stale/ })).toBeUndefined()
  await ui.unmount()
})

test('the band yields to a survey and says nothing before the first read', async ($, on) => {
  engine(on, prints(snapshot()))
  on('ui.render', ($, e) => {
    const { Text } = $.ui.resolve(e)
    return <Text>engine</Text>
  })

  const early = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await early.find({ type: 'Text', text: /engine/ }))?.text).toBe('engine')
  await early.unmount()

  await $.session.start({ ...SESSION })
  const survey = await $.ui.mount({ ...BAND, surface: 'terminal', props: { ...BAND.props, hasSurvey: true } })
  expect((await survey.find({ type: 'Text', text: /engine/ }))?.text).toBe('engine')
  await survey.unmount()
})

test('a read that fails before any snapshot says why in the band', async ($, on) => {
  const world = engine(on, fails)
  await start($, world)

  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await ui.find({ type: 'Text', text: /no snapshot yet/ }))?.text).toBe('Conclave: no snapshot yet (gh: not signed in)')
  await ui.unmount()
})

test('a snapshot that goes stale dims the band and says for how long', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })

  world.answer = fails
  await world.clock.advance(150_000)
  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await ui.find({ type: 'Text', text: /stale/ }))?.text).toBe('stale 2m')
  expect((await ui.find({ type: 'Text', text: /2 units/ }))?.props.dimColor).toBe(true)
  await ui.unmount()
})

test('a snapshot of a newer schema is refused, not drawn', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        copy.schema = 2
      }),
    ),
  )
  await start($, world)

  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await ui.find({ type: 'Text', text: /update the checkout/ }))?.text).toContain('schema 2')
  expect(await ui.find({ key: 'units' })).toBeUndefined()
  await ui.unmount()
})

test('the status line says what the session is', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)
  expect(world.statuses.at(-1)).toBe('no role: start with coney-agent')

  await $.classic.SessionStart({ source: 'startup', agent_type: 'engineer' })
  expect(world.statuses.at(-1)).toBe('engineer · fix-frame-812 · compared')

  await $.classic.SessionStart({ source: 'startup', agent_type: 'coordinator' })
  expect(world.statuses.at(-1)).toBe('coordinator · 2 units · ✖1')
})

test("the session start's answer from beneath passes through untouched", async ($, on) => {
  const beneath = { hookSpecificOutput: { hookEventName: 'SessionStart', additionalContext: 'context from beneath' } }
  engine(on, prints(snapshot()), { beneath })
  const answer = await $.classic.SessionStart({ source: 'startup', agent_type: 'engineer' })

  expect(answer).toEqual(beneath)
})

test('the pane lists failed units first and opens a row on a press', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  for (const surface of ['terminal', 'desktop', 'vscode'] as const) {
    const ui = await $.ui.mount({ ...PANE, surface })
    const rows = await ui.findAll({ type: 'Button' })
    expect(rows.map(row => row.key)).toEqual(['task:fix-frame-812', 'task:fix-new', 'task:add-task-start'])
    expect(rows[0]?.text).toContain('Fix the uncuff steer')
    expect(rows[0]?.text).toContain(surface === 'terminal' ? '✖' : '❌')
    expect(await ui.find({ type: 'Text', text: /^reason/ })).toBeUndefined()

    await ui.press({ key: 'task:fix-frame-812' })
    expect((await ui.find({ type: 'Text', text: /^reason/ }))?.text).toBe('reason    step compared failed')
    expect((await ui.find({ type: 'Text', text: /^expiry/ }))?.text).toBe('expiry    would expire in 2d 14h (not enforced yet) (2026-10-26 04:00 UTC)')
    expect((await ui.find({ type: 'Text', text: /^stamps/ }))?.text).toContain('✔ studied  ✔ fails-first  ✖ compared  · checked')
    expect((await ui.find({ type: 'Text', text: /^scope/ }))?.text).toContain('⚠ overlaps fix-new')

    await ui.press({ key: 'task:fix-frame-812' })
    expect(await ui.find({ type: 'Text', text: /^reason/ })).toBeUndefined()
    await ui.unmount()
  }
})

test('one row is open at a time', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal' })
  await ui.press({ key: 'task:fix-frame-812' })
  await ui.press({ key: 'task:fix-new' })
  expect(await ui.find({ type: 'Text', text: /^reason/ })).toBeUndefined()
  expect((await ui.find({ type: 'Text', text: /^status/ }))?.text).toContain('assigned')
  await ui.unmount()
})

test('the claims section counts down on the mod clock while the pane is shown', async ($, on) => {
  const world = engine(on, prints(snapshot(enforced)), { isPaneShown: true })
  await start($, world)

  const before = await $.ui.mount({ ...PANE, surface: 'terminal' })
  const claim = await before.find({ type: 'Text', text: /#770/ })
  expect(claim?.text).toBe('  #770  contrib-a  claimed  expires in 22h 00m')
  expect(claim?.props.color).toBe('warning')
  await before.unmount()

  await world.clock.advance(1000)
  const after = await $.ui.mount({ ...PANE, surface: 'terminal' })
  expect((await after.find({ type: 'Text', text: /#770/ }))?.text).toBe('  #770  contrib-a  claimed  expires in 21h 59m')
  await after.unmount()
})

test('a claim in its last hours is red, and one that waits has no countdown', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        enforced(copy)
        if (copy.claims !== null && copy.claims.error === null) {
          const [first, second] = copy.claims.claims
          if (first !== undefined) first.deadline = '2026-10-23T18:00:00Z'
          if (second !== undefined) {
            second.deadline = null
            second.waiting = 'S-blocked'
          }
        }
      }),
    ),
  )
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal' })
  const red = await ui.find({ type: 'Text', text: /^  #741/ })
  expect(red?.text).toContain('expires in 4h 00m')
  expect(red?.props.color).toBe('error')
  expect((await ui.find({ type: 'Text', text: /#770/ }))?.text).toContain('waits while S-blocked')
  await ui.unmount()
})

test('a pane under the minimum width points to the band', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 90 } })
  expect((await ui.find({ type: 'Text' }))?.text).toContain('needs about 110 columns')
  await ui.unmount()
})

/** The agent types the fixture's hook settings allow. */
const ALLOWED = ['coordinator', 'reviewer', 'research-reviewer', 'pr-reviewer', 'Explore', 'Plan', 'fork']

/** Whether the model is offered `agent` now. */
async function isOffered($: Engine, agent: string): Promise<boolean> {
  return (await $.agent.offer({ agent, ...OFFERED })).isOffered
}

test("a session that starts with the last session's list hides the generic types and keeps roles and helpers", async ($, on) => {
  engine(on, prints(snapshot()), { store: { offer: { allowed: ALLOWED } } })
  await $.session.start({ ...SESSION })

  for (const agent of ALLOWED) expect(await isOffered($, agent)).toBe(true)
  for (const agent of ['general-purpose', 'statusline-setup', 'claude']) expect(await isOffered($, agent)).toBe(false)
})

test('the first session, with no list stored, hides nothing and keeps the list for the next', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)

  // The listing never changes under the prompt cache: a list that lands mid-session waits for the next session.
  expect(await isOffered($, 'general-purpose')).toBe(true)
  expect((world.stored.offer as { allowed: string[] }).allowed).toEqual(ALLOWED)
})

test('the list is frozen for the session, so a later read never changes the agent listing', async ($, on) => {
  const world = engine(on, prints(snapshot()), { store: { offer: { allowed: ALLOWED } } })
  await $.session.start({ ...SESSION })

  world.answer = prints(
    snapshot(copy => {
      copy.config.roles = ['coordinator', 'general-purpose']
    }),
  )
  await world.clock.advance(60_000)

  expect(await isOffered($, 'general-purpose')).toBe(false)
  expect((world.stored.offer as { allowed: string[] }).allowed).toContain('general-purpose')
})

test('a saved list hides nothing once the hook settings cannot be read, and is dropped', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        copy.config.error = '.claude/hooks/hooks.toml: cannot be read'
        copy.config.roles = []
        copy.config.helpers = []
      }),
    ),
    { store: { offer: { allowed: ['coordinator', 'Explore'] } } },
  )
  await $.session.start({ ...SESSION })
  // Until the read lands the saved list stands (nothing is known yet); then nothing is hidden and nothing is kept.
  expect(await isOffered($, 'general-purpose')).toBe(false)
  await world.clock.advance(1000)

  expect(await isOffered($, 'general-purpose')).toBe(true)
  expect(world.stored.offer).toBeUndefined()
})

test('a failing read keeps the saved list: only a read that says the settings are unreadable drops it', async ($, on) => {
  const world = engine(on, fails, { store: { offer: { allowed: ['coordinator', 'Explore'] } } })
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)

  expect(await isOffered($, 'coordinator')).toBe(true)
  expect(await isOffered($, 'engineer')).toBe(false)
  expect(world.stored.offer).toEqual({ allowed: ['coordinator', 'Explore'] })
})

test('a stored value of the wrong shape is ignored', async ($, on) => {
  engine(on, fails, { store: { offer: { allowed: 'general-purpose' } } })
  await $.session.start({ ...SESSION })

  expect(await isOffered($, 'general-purpose')).toBe(true)
})

test('the session starts at once even when the snapshot command never answers', async ($, on) => {
  const world = engine(on, prints(snapshot()), { isHung: true })
  // Were the start to wait for the read, this would never return and the test would time out.
  await $.session.start({ ...SESSION })

  expect(world.runs).toHaveLength(1)
  // ... and with no list known the listing is the engine's own, whole and unchanging.
  expect(await isOffered($, 'general-purpose')).toBe(true)
})

test('a session that is not interactive reads the snapshot without waiting and opens no pane', async ($, on) => {
  const world = engine(on, prints(snapshot()), { isHung: true })
  await $.session.start({ ...SESSION, isInteractive: false })

  expect(world.runs).toHaveLength(1)
  expect(world.opened).toEqual([])
})

test('the snapshot command keeps Python out of the checkout and is bounded in time', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })

  const variable = world.envs[0]?.UV_PROJECT_ENVIRONMENT
  expect(variable).toMatch(/\/scratch\/conclave\/[^/]+\/venv$/)
  expect(variable).not.toContain('/tools/claude-mod')
  expect(world.timeouts[0]).toBeGreaterThan(0)
  expect(world.timeouts[0]).toBeLessThanOrEqual(60_000)
})

test('/conclave opens the pane', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })
  world.opened.length = 0

  const done = await $.command.run({
    command: 'conclave',
    args: '',
    origin: { kind: 'composer' },
    presentation: { isFullscreen: false, columns: 120 },
  })
  expect(done.text).toBe('Conclave pane opened.')
  expect(world.opened).toEqual(['conclave'])
})
