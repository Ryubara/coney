// SPDX-License-Identifier: GPL-3.0-or-later
// `claude plugin test tools/claude-mod`: the band, the status line, the pane and the agent list, drawn through the
// plugin from a fixture snapshot that the Python tests write (python/tests/test_conclave_fixture.py), so the mod's
// tests and the command share one shape. The watchers' tests are in watch.test.tsx.
import { expect, test } from 'claude-code/testing'
import type { Engine } from 'claude-code/testing'

import type { Snapshot } from '../types'
import { BAND, engine, enforced, fails, OFFERED, PANE, prints, SESSION, snapshot, start } from './support'

test('the first read runs the snapshot command from the checkout the mod folder belongs to', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })

  const argv = world.runs[0] ?? []
  expect(argv.slice(0, 3)).toEqual(['uv', 'run', '-q'])
  expect(argv.slice(-3)).toEqual(['conclave', 'snapshot', '--json'])
  expect(argv).not.toContain('--github')
})

// `uv run` syncs the environment before it starts the command, and on Windows that fails while the launcher's own
// `coney-agent.exe` is running (the file is locked): the read would then fail on every poll. Issue 1183.
test('every read runs uv without syncing the environment, local and GitHub alike', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)
  await world.clock.advance(60_000)

  expect(world.runs.length).toBeGreaterThan(1)
  expect(world.runs.some(argv => argv.includes('--github'))).toBe(true)
  for (const argv of world.runs) {
    expect(argv.slice(0, 4)).toEqual(['uv', 'run', '-q', '--no-sync'])
    expect(argv.indexOf('--no-sync')).toBeLessThan(argv.indexOf('--project'))
  }
})

test('a first read with no environment yet fails without syncing, then syncs once so the band can draw', async ($, on) => {
  const world = engine(on, fails)
  world.answerRun = argv => (argv.includes('--no-sync') ? undefined : prints(snapshot()))
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)

  expect(world.runs[0]).toContain('--no-sync')
  expect(world.runs[1]).not.toContain('--no-sync')
  expect(world.runs[1]?.slice(0, 3)).toEqual(['uv', 'run', '-q'])
  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await ui.find({ key: 'units' }))?.text).toBe('2 units')
  await ui.unmount()
})

test('once a read has worked, a failed read is never retried with a sync', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)
  world.answer = fails
  const before = world.runs.length
  await world.clock.advance(60_000)

  expect(world.runs.length).toBeGreaterThan(before)
  for (const argv of world.runs.slice(before)) expect(argv).toContain('--no-sync')
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

test('the band counts the check slots beside the builds and marks a stalled check', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  for (const surface of ['terminal', 'desktop'] as const) {
    const ui = await $.ui.mount({ ...BAND, surface })
    // the fixture holds one check slot and one waiting for it, and `fix-frame-812`'s pytest is stuck at 64%
    expect((await ui.find({ key: 'checks' }))?.text).toBe('checks 1 (+1 queued)')
    expect((await ui.find({ key: 'stalled' }))?.text).toBe('⚠1 stalled')
    expect((await ui.find({ type: 'Text', text: /stalled/ }))?.props.color).toBe('warning')
    // the builds count the build lanes only: the check lane is not a build
    expect((await ui.find({ key: 'builds' }))?.text).toBe('builds 1 (+1 queued)')
    await ui.unmount()
  }
})

test('a check that moves is not marked, and no check slot in use leaves the item out', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        if (copy.units.error !== null) return
        for (const unit of copy.units.units) if (unit.check !== null) unit.check.stalled = false
      }),
    ),
  )
  await start($, world)
  let ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await ui.find({ key: 'checks' }))?.text).toBe('checks 1 (+1 queued)')
  expect(await ui.find({ key: 'stalled' })).toBeUndefined()
  await ui.unmount()

  world.answer = prints(
    snapshot(copy => {
      if (copy.builds.error === null) delete copy.builds.lanes.check
    }),
  )
  await world.clock.advance(60_000)
  ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect(await ui.find({ key: 'checks' })).toBeUndefined()
  await ui.unmount()
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
        copy.schema = 4
      }),
    ),
  )
  await start($, world)

  const ui = await $.ui.mount({ ...BAND, surface: 'terminal' })
  expect((await ui.find({ type: 'Text', text: /update the checkout/ }))?.text).toContain('schema 4')
  expect(await ui.find({ key: 'units' })).toBeUndefined()
  await ui.unmount()
})

test('the status line says what the session is', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)
  expect(world.statuses.at(-1)).toBe('no role: start with coney-agent')

  await $.classic.SessionStart({ source: 'startup', agent_type: 'engineer' })
  // the issue and the stage the pane's rows show, in the same words
  expect(world.statuses.at(-1)).toBe('engineer · #741 · compared failed')

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

test("a unit's check shows on its row in the short form and in full when the row opens", async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 120 } })
  const rows = await ui.findAll({ type: 'Button' })
  expect(rows[0]?.text).toContain('pytest 64% 50m stalled')
  expect(rows[1]?.text).toContain('queued 5/6 ~36m')
  expect(rows[2]?.text).not.toContain('pytest') // the merged unit has no check
  expect(await ui.find({ type: 'Text', text: /^check/ })).toBeUndefined()

  await ui.press({ key: 'task:fix-frame-812' })
  const stalled = await ui.find({ type: 'Text', text: /^check/ })
  expect(stalled?.text).toBe('check     check 6 of 8, pytest: running 50 min, 64% done; stalled: no progress for 40 min')
  expect(stalled?.props.color).toBe('warning')
  await ui.press({ key: 'task:fix-new' })
  const queued = await ui.find({ type: 'Text', text: /^check/ })
  expect(queued?.text).toContain('waiting for a check slot, place 5 of 6, about 36 min; held by bob')
  expect(queued?.props.color).toBeUndefined()
  await ui.unmount()
})

for (const [columns, where] of [
  [120, 'on the row'],
  [80, 'first on the second line'],
  [40, 'on the second line after the pull request'],
  [30, 'on the second line after the pull request'],
] as const) {
  test(`at ${columns} columns the short form of a check is ${where} and every line fits`, async ($, on) => {
    const world = engine(on, prints(snapshot()))
    await start($, world)

    const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: columns } })
    const lines = await drawn(ui)
    // the wide row is let wrap, as every wide row is; the shorter layouts clip each line to the pane
    if (columns < 110) expect(overflows(lines, columns)).toEqual([])
    const at = lines.findIndex(line => line.text.includes('pytest 6'))
    expect(at).toBeGreaterThan(-1)
    if (columns >= 110) {
      expect(lines[at]?.text).toContain('pytest 64% 50m stalled') // the wide row says it whole, after the build
    } else if (columns >= 60) {
      // the check leads the dim second line, where a clip cuts the title and the build before it
      expect(lines[at]?.text.trimStart().startsWith('pytest 6')).toBe(true)
      expect(lines[at - 1]?.text).toContain('compared failed')
    } else {
      // the narrow pane keeps the pull request first, then the check
      expect(lines[at]?.text.trimStart().startsWith('PR #812 · pytest 6')).toBe(true)
      expect(lines[at - 1]?.text).toContain('compared failed')
    }
    await ui.unmount()
  })
}

test('a check that passed, or no check, adds nothing to the row', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        if (copy.units.error !== null) return
        for (const unit of copy.units.units) if (unit.check !== null) unit.check.state = 'passed'
      }),
    ),
  )
  await start($, world)
  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 120 } })
  for (const row of await ui.findAll({ type: 'Button' })) expect(row.text).not.toContain('pytest')
  await ui.unmount()
})

test('an abandoned check is marked amber in the row detail', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        if (copy.units.error !== null) return
        const check = copy.units.units.find(unit => unit.task === 'fix-frame-812')?.check
        if (check == null) return
        check.state = 'abandoned'
        check.stalled = false
        check.short = 'pytest abandoned'
        check.text = 'check 6 of 8, pytest: abandoned, its process is gone (last seen running, 64% done)'
      }),
    ),
  )
  await start($, world)
  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 120 } })
  expect((await ui.findAll({ type: 'Button' }))[0]?.text).toContain('pytest abandoned')
  await ui.press({ key: 'task:fix-frame-812' })
  expect((await ui.find({ type: 'Text', text: /^check/ }))?.props.color).toBe('warning')
  await ui.unmount()
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

type Drawn = { type: string; props: Record<string, unknown>; children: unknown[] }
type Line = { columns: number; text: string }

const isElement = (node: unknown): node is Drawn => typeof node === 'object' && node !== null && 'children' in node
const marginOf = (node: Drawn): number => (Number(node.props.marginLeft) || 0) + (Number(node.props.marginRight) || 0)

/** What a drawn node shows as text, in document order. */
function textOf(node: unknown): string {
  return isElement(node) ? node.children.map(textOf).join('') : String(node)
}

/**
 * The terminal columns of a string, measured apart from the mod's own `columns` so the tests do not grade it by itself:
 * an emoji shown as emoji or a CJK character takes two, a joiner or variation selector none, except that the emoji
 * selector after the one-column marks the tests draw (`⚠️`) makes them two. Enough for the strings these tests draw.
 */
function cells(text: string): number {
  let sum = 0
  for (const char of text) {
    if (char === '\ufe0f') sum += 1
    else if (/[\u200b-\u200f\ufe00-\ufe0e]/u.test(char)) sum += 0
    else if (/[\p{Emoji_Presentation}\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Hangul}]/u.test(char)) sum += 2
    else sum += 1
  }
  return sum
}

/** The columns a drawn node takes, margins included: a text its cells, a row the sum of its parts, a column its widest. */
function columnsOf(node: unknown): number {
  if (!isElement(node)) return cells(String(node))
  if (node.type === 'Text') return cells(textOf(node))
  const parts = node.children.map(columnsOf)
  const inside = node.props.flexDirection === 'column' ? Math.max(0, ...parts) : parts.reduce((sum, part) => sum + part, 0)
  return inside + marginOf(node)
}

/** The lines of a drawn tree: each child of a column is a line of its own, shifted by the column's margin; a row is one. */
function linesOf(node: unknown): Line[] {
  if (isElement(node) && node.type === 'Box' && node.props.flexDirection === 'column') {
    return node.children.flatMap(linesOf).map(line => ({ ...line, columns: line.columns + marginOf(node) }))
  }
  return [{ columns: columnsOf(node), text: textOf(node) }]
}

/** Every line of a mounted pane with the columns it takes, indents of the open rows' details included. */
async function drawn(ui: { findAll: (target: { type: 'Box' }) => Promise<readonly Drawn[]> }): Promise<Line[]> {
  const [root] = await ui.findAll({ type: 'Box' })
  return linesOf(root)
}

/** The lines wider than the pane leaves room for (one column is kept spare, so a full line never wraps): none is allowed. */
function overflows(lines: readonly Line[], columns: number): string[] {
  return lines.filter(line => line.columns > columns - 1).map(line => `${line.columns}: ${line.text}`)
}

for (const columns of [60, 80, 109] as const) {
  test(`the pane at ${columns} columns is compact: the key fields first, and every line fits`, async ($, on) => {
    const world = engine(on, prints(snapshot()))
    await start($, world)

    const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: columns } })
    const rows = await ui.findAll({ type: 'Button' })
    expect(rows.map(row => row.key)).toEqual(['task:fix-frame-812', 'task:fix-new', 'task:add-task-start'])
    // line 1 of a row: the mark, the issue, the stage and the pull request; the title and the rest follow below it
    expect(rows[0]?.text).toContain('#741 compared failed  PR #812')
    expect(rows[0]?.text).not.toContain('gameplay: Fix the uncuff steer')
    expect(overflows(await drawn(ui), columns)).toEqual([])

    await ui.press({ key: 'task:fix-frame-812' })
    expect((await ui.findAll({ type: 'Text', text: /^stamps/ })).length).toBeGreaterThan(0)
    expect((await ui.findAll({ type: 'Text', text: /^reason/ })).length).toBe(1)
    // a long value wraps under its label, so nothing an open row says is lost to the width
    const shown = (await ui.findAll({ type: 'Text' })).map(item => item.text ?? '').join(' ')
    for (const word of ['2026-10-26', 'UTC)', '/work/worktrees/fix-frame-812', '· finished', 'overlaps']) expect(shown).toContain(word)
    expect(overflows(await drawn(ui), columns)).toEqual([])
    await ui.unmount()
  })
}

for (const columns of [30, 40, 59] as const) {
  test(`the pane at ${columns} columns is narrow: the summary, the rows and the claims, every line clipped to fit`, async ($, on) => {
    const world = engine(on, prints(snapshot(enforced)))
    await start($, world)

    const band = await $.ui.mount({ ...BAND, surface: 'terminal' })
    const items = (await band.findAll({ type: 'Button' })).map(item => item.text ?? '')
    await band.unmount()
    const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: columns } })
    const lines = await drawn(ui)
    const text = lines.map(line => line.text).join('\n')
    // never the refusal: the band's own items (pull requests, builds, critical issues, disk) head the pane
    expect(text).not.toContain('needs about')
    expect(items.length).toBeGreaterThan(3)
    for (const item of items) expect(text).toContain(item)
    for (const key of ['PRs 1', 'builds 1', 'P-critical #790', 'disk ']) expect(text).toContain(key)
    // every unit is a row that still opens, its issue and stage first; the pull request moves to the line below
    const rows = await ui.findAll({ type: 'Button' })
    expect(rows.map(row => row.key)).toEqual(['task:fix-frame-812', 'task:fix-new', 'task:add-task-start'])
    expect(rows[0]?.text).toContain('#741 compared failed')
    expect(text).toContain('PR #812')
    // each claim keeps its issue and its countdown whole, ahead of the claimant
    expect(text).toMatch(/^ {2}#741 2d 14h /m)
    expect(text).toMatch(/^ {2}#770 22h 00m /m)
    expect(text).toContain('CLAIMS')
    expect(overflows(lines, columns)).toEqual([])
    // a cut line ends in an ellipsis, never wraps
    expect(lines.some(line => line.text.endsWith('…'))).toBe(true)

    await ui.press({ key: 'task:fix-frame-812' })
    expect((await ui.findAll({ type: 'Text', text: /^stamps/ })).length).toBeGreaterThan(0)
    expect((await ui.findAll({ type: 'Text', text: /^reason/ })).length).toBe(1)
    expect(overflows(await drawn(ui), columns)).toEqual([])
    await ui.unmount()
  })
}

test('a narrow claim that is not enforced yet, or waits, says so in a few characters', async ($, on) => {
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        if (copy.claims !== null && copy.claims.error === null) {
          const second = copy.claims.claims[1]
          if (second !== undefined) {
            second.deadline = null
            second.waiting = 'S-blocked'
          }
        }
      }),
    ),
  )
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 30 } })
  const text = (await drawn(ui)).map(line => line.text).join('\n')
  expect(text).toMatch(/^ {2}#741 ~2d 14h /m)
  expect(text).toMatch(/^ {2}#770 waits: S-blocked/m)
  await ui.unmount()
})

for (const columns of [29, 20, 8] as const) {
  test(`under the narrowest layout, at ${columns} columns, the pane is one clipped summary line`, async ($, on) => {
    const world = engine(on, prints(snapshot()))
    await start($, world)

    const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: columns } })
    const lines = await drawn(ui)
    expect(lines.length).toBe(1)
    expect(lines[0]?.text).not.toContain('needs about')
    expect(lines[0]?.text.startsWith('2 units'.slice(0, columns - 2))).toBe(true)
    expect(overflows(lines, columns)).toEqual([])
    await ui.unmount()
  })
}

/** A title as wide as text gets: emoji and CJK, two columns each, on the units, the issues and an issue row of mine. */
const WIDE_TITLE = '修正する🚀 the 表示 of 宽字符 🔥🔥 everywhere 한국어'

/**
 * The fixture with `WIDE_TITLE` everywhere a title is drawn, and the signed-in account renamed `me`, so the title
 * reaches the narrow pane's second lines instead of being cut behind a long name.
 */
function wideTitles(copy: Snapshot): void {
  enforced(copy)
  if (copy.claims !== null && copy.claims.error === null) {
    copy.claims.account = 'me'
    for (const claim of copy.claims.claims) if (claim.claimant !== 'contrib-a') claim.claimant = 'me'
  }
  if (copy.units.error === null) {
    for (const unit of copy.units.units) {
      unit.goal = WIDE_TITLE
      unit.claimant = 'me'
    }
  }
  if (copy.issues !== null && copy.issues.error === null) for (const issue of copy.issues.issues) issue.title = WIDE_TITLE
  if (copy.claims !== null && copy.claims.error === null) {
    const account = copy.claims.account
    const second = copy.claims.claims[1]
    // #770 becomes the signed-in account's with no unit here, so it is drawn as an issue row with its title
    if (second !== undefined && account !== null) {
      second.claimant = account
      second.title = WIDE_TITLE
    }
  }
}

for (const surface of ['terminal', 'desktop'] as const) {
  for (const columns of [30, 40, 60, 80] as const) {
    test(`at ${columns} columns on the ${surface} a title of emoji and CJK is clipped by its columns`, async ($, on) => {
      const world = engine(on, prints(snapshot(wideTitles)))
      await start($, world)

      const ui = await $.ui.mount({ ...PANE, surface, props: { ...PANE.props, bodyColumns: columns } })
      const lines = await drawn(ui)
      expect(lines.some(line => line.text.includes('修正'))).toBe(true)
      expect(overflows(lines, columns)).toEqual([])
      const rows = await ui.findAll({ type: 'Button' })
      expect(rows.map(row => row.key)).toContain('#770')
      await ui.press({ key: '#770' })
      expect(overflows(await drawn(ui), columns)).toEqual([])
      await ui.unmount()
    })
  }
}

for (const surface of ['terminal', 'desktop'] as const) {
  test(`the narrow pane at the smallest pane_min_columns the conventions allow (15) still fits on the ${surface}`, async ($, on) => {
    const world = engine(
      on,
      prints(
        snapshot(copy => {
          wideTitles(copy)
          copy.config.pane_min_columns = 15
        }),
      ),
    )
    await start($, world)

    const ui = await $.ui.mount({ ...PANE, surface, props: { ...PANE.props, bodyColumns: 15 } })
    expect((await ui.findAll({ type: 'Button' })).length).toBeGreaterThan(0)
    expect(overflows(await drawn(ui), 15)).toEqual([])
    await ui.press({ key: 'task:fix-frame-812' })
    expect(overflows(await drawn(ui), 15)).toEqual([])
    await ui.unmount()
  })
}

test("the compact pane opens with what the band says, in the band's own words", async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  const band = await $.ui.mount({ ...BAND, surface: 'terminal' })
  const items = (await band.findAll({ type: 'Button' })).map(item => item.text ?? '')
  await band.unmount()
  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 80 } })
  const summary = (await ui.findAll({ type: 'Text' })).map(item => item.text ?? '').join(' · ')
  expect(items.length).toBeGreaterThan(3)
  for (const item of items) expect(summary).toContain(item)
  await ui.unmount()
})

test('the compact claims list gives each claim its countdown', async ($, on) => {
  const world = engine(on, prints(snapshot(enforced)))
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 60 } })
  const claims = await ui.findAll({ type: 'Text', text: /^ {2}#\d+ / })
  expect(claims.map(claim => claim.text)).toEqual([
    expect.stringContaining('#741'),
    expect.stringContaining('#770'),
  ])
  expect(claims[0]?.text).toContain('expires in 2d 14h')
  expect(overflows(await drawn(ui), 60)).toEqual([])
  await ui.unmount()
})

for (const columns of [60, 80] as const) {
  test(`the compact pane at ${columns} columns clips a long account and the errors of unread sections`, async ($, on) => {
    const long = 'task folders cannot be read: '.concat('x'.repeat(120))
    // the sections that could not be read hold only their error, and the signed-in account sits in the claims section
    const unitsFailed = engine(
      on,
      prints(
        snapshot(copy => {
          copy.units = { error: long }
        }),
      ),
    )
    await start($, unitsFailed)
    const first = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: columns } })
    expect(await first.find({ type: 'Text', text: /^units not read: / })).toBeDefined()
    // ... and the 39-character account of the fixture, in the header
    expect(await first.find({ type: 'Text', text: /^UNITS AND ISSUES \(mine: example-maintainer-who-/ })).toBeDefined()
    expect(overflows(await drawn(first), columns)).toEqual([])
    await first.unmount()
  })

  test(`the compact pane at ${columns} columns clips the error of unread claims`, async ($, on) => {
    const world = engine(
      on,
      prints(
        snapshot(copy => {
          copy.claims = { error: 'gh: '.concat('not signed in to github.com '.repeat(8)) }
        }),
      ),
    )
    await start($, world)
    const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: columns } })
    expect(await ui.find({ type: 'Text', text: /^claims not read: / })).toBeDefined()
    expect(overflows(await drawn(ui), columns)).toEqual([])
    await ui.unmount()
  })
}

test("the compact pane's summary keeps the colours the band gives its items", async ($, on) => {
  const world = engine(on, prints(snapshot(enforced)))
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 80 } })
  expect((await ui.find({ type: 'Text', text: /^✖1 failed$/ }))?.props.color).toBe('error')
  expect((await ui.find({ type: 'Text', text: /^PRs 1 / }))?.props.color).toBe('error')
  expect((await ui.find({ type: 'Text', text: /^claims 2$/ }))?.props.color).toBe('warning')
  expect((await ui.find({ type: 'Text', text: /^⚠ P-critical/ }))?.props.color).toBe('warning')
  expect((await ui.find({ type: 'Text', text: /^2 units$/ }))?.props.color).toBeUndefined()
  await ui.unmount()
})

test('from the wide width the pane keeps its full rows with the title on the row', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await start($, world)

  const ui = await $.ui.mount({ ...PANE, surface: 'terminal', props: { ...PANE.props, bodyColumns: 110 } })
  expect((await ui.findAll({ type: 'Button' }))[0]?.text).toContain('gameplay: Fix the uncuff steer')
  // the compact pane's summary is not drawn here, not even as an empty line
  expect((await drawn(ui))[0]?.text).toMatch(/^with GitHub/)
  await ui.unmount()
})

/** The agent types the fixture's hook settings allow. */
const ALLOWED = ['coordinator', 'reviewer', 'engineer', 'tooling', 'Explore', 'Plan', 'fork']

/** Whether the model is offered `agent` now. */
async function isOffered($: Engine, agent: string): Promise<boolean> {
  return (await $.agent.offer({ agent, ...OFFERED })).isOffered
}

test("a session that starts with the last session's list hides the generic types and keeps roles and helpers", async ($, on) => {
  engine(on, prints(snapshot()), { saved: { allowed: ALLOWED } })
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
  expect((world.stored.own as { allowed: string[] }).allowed).toEqual(ALLOWED)
})

test('the list is frozen for the session, so a later read never changes the agent listing', async ($, on) => {
  const world = engine(on, prints(snapshot()), { saved: { allowed: ALLOWED } })
  await $.session.start({ ...SESSION })

  world.answer = prints(
    snapshot(copy => {
      copy.config.roles = ['coordinator', 'general-purpose']
    }),
  )
  await world.clock.advance(60_000)

  expect(await isOffered($, 'general-purpose')).toBe(false)
  expect((world.stored.own as { allowed: string[] }).allowed).toContain('general-purpose')
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
    { saved: { allowed: ['coordinator', 'Explore'] } },
  )
  await $.session.start({ ...SESSION })
  // Until the read lands the saved list stands (nothing is known yet); then nothing is hidden and nothing is kept.
  expect(await isOffered($, 'general-purpose')).toBe(false)
  await world.clock.advance(1000)

  expect(await isOffered($, 'general-purpose')).toBe(true)
  expect(world.stored.own).toBeUndefined()
})

test('a failing read keeps the saved list: only a read that says the settings are unreadable drops it', async ($, on) => {
  const world = engine(on, fails, { saved: { allowed: ['coordinator', 'Explore'] } })
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)

  expect(await isOffered($, 'coordinator')).toBe(true)
  expect(await isOffered($, 'engineer')).toBe(false)
  expect(world.stored.own).toEqual({ allowed: ['coordinator', 'Explore'] })
})

test('a stored value of the wrong shape is ignored', async ($, on) => {
  engine(on, fails, { saved: { allowed: 'general-purpose' } })
  await $.session.start({ ...SESSION })

  expect(await isOffered($, 'general-purpose')).toBe(true)
})

test("the saved list is the checkout's own: another checkout's list is not applied and is left alone", async ($, on) => {
  const other = { allowed: ['coordinator'] }
  const world = engine(on, prints(snapshot()), { elsewhere: { 'offer:/work/worktrees/coney/other': other } })
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)

  // Nothing is saved for this checkout yet, so nothing is hidden, whatever the other one saved.
  expect(await isOffered($, 'general-purpose')).toBe(true)
  expect([...world.ownKeys]).toHaveLength(1)
  expect([...world.ownKeys][0]).toMatch(/^offer:.+/)
  expect([...world.ownKeys][0]).not.toContain('/tools/claude-mod')
  expect(world.stored.own).toEqual({ allowed: ALLOWED })
  expect(world.stored['offer:/work/worktrees/coney/other']).toEqual(other)
})

test("two checkouts keep different lists: a read that drops one leaves the other's alone", async ($, on) => {
  const other = { allowed: ['coordinator', 'engineer'] }
  const world = engine(
    on,
    prints(
      snapshot(copy => {
        copy.config.error = '.claude/hooks/hooks.toml: cannot be read'
        copy.config.roles = []
        copy.config.helpers = []
      }),
    ),
    { saved: { allowed: ['coordinator', 'Explore'] }, elsewhere: { 'offer:/work/repos/coney': other } },
  )
  await $.session.start({ ...SESSION })
  // This session hides by its own checkout's list, not the other's (which allows `engineer`).
  expect(await isOffered($, 'engineer')).toBe(false)
  expect(await isOffered($, 'Explore')).toBe(true)
  await world.clock.advance(1000)

  expect(world.stored.own).toBeUndefined()
  expect(world.stored['offer:/work/repos/coney']).toEqual(other)
})

test('a clear applies the list saved since the session started; a compaction and a resume do not', async ($, on) => {
  const world = engine(on, prints(snapshot()))
  await $.session.start({ ...SESSION })
  await world.clock.advance(1000)
  // The first read saved the list; this session started without one, so its listing stays whole.
  expect(await isOffered($, 'general-purpose')).toBe(true)

  // A compaction keeps the prompt cache of the listing, so the listing stays whole.
  await $.classic.SessionStart({ source: 'compact', agent_type: 'coordinator' })
  expect(await isOffered($, 'general-purpose')).toBe(true)

  // A resumed conversation within the cache lifetime has the old listing in its cached prefix: swapping would rewrite it.
  await $.classic.SessionStart({ source: 'resume', agent_type: 'coordinator' })
  expect(await isOffered($, 'general-purpose')).toBe(true)

  // A clear starts a new prompt cache anyway, so the saved list applies from the new conversation's first turn.
  await $.classic.SessionStart({ source: 'clear', agent_type: 'coordinator' })
  expect(await isOffered($, 'general-purpose')).toBe(false)
  for (const agent of ALLOWED) expect(await isOffered($, agent)).toBe(true)
})

test('a startup takes up the saved list and a resume leaves the listing as the conversation has it', async ($, on) => {
  const world = engine(on, prints(snapshot()), { saved: { allowed: ALLOWED } })
  await $.session.start({ ...SESSION })
  world.answer = prints(
    snapshot(copy => {
      copy.config.roles = ['coordinator', 'general-purpose']
    }),
  )
  await world.clock.advance(60_000)
  expect(await isOffered($, 'general-purpose')).toBe(false)

  await $.classic.SessionStart({ source: 'resume' })
  expect(await isOffered($, 'general-purpose')).toBe(false)

  await $.classic.SessionStart({ source: 'startup' })
  expect(await isOffered($, 'general-purpose')).toBe(true)
})

test('a clear takes up a changed list, and one with no saved list leaves the listing as it is', async ($, on) => {
  const world = engine(on, prints(snapshot()), { saved: { allowed: ALLOWED } })
  await $.session.start({ ...SESSION })
  world.answer = prints(
    snapshot(copy => {
      copy.config.roles = ['coordinator', 'general-purpose']
    }),
  )
  await world.clock.advance(60_000)
  expect(await isOffered($, 'general-purpose')).toBe(false)

  await $.classic.SessionStart({ source: 'clear' })
  expect(await isOffered($, 'general-purpose')).toBe(true)

  delete world.stored.own
  await $.classic.SessionStart({ source: 'clear' })
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
  expect(variable).toMatch(/\/scratch\/[^/]+\/conclave-venv$/)
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
