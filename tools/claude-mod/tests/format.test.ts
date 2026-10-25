// SPDX-License-Identifier: GPL-3.0-or-later
// The mod's plain functions: the clock arithmetic, the merge of a local read over a GitHub one, the checkout of a folder.
import { expect, test } from 'claude-code/testing'

import type { Held, Snapshot } from '../types'
import { claimLineText, paneLayout, claimTone, clip, countdown, elapsed, issueIds, mark, packLines, showUtc, stageText, wrapTokens } from '../src/format'
import { checkoutOf, firstLine, merged, offerKey, venvOf } from '../src/snapshot'
import { EMPTY } from '../src/state'
import fixture from './fixtures/snapshot'

const snapshot = (): Snapshot => JSON.parse(JSON.stringify(fixture)) as Snapshot
const NOW = Date.parse('2026-10-23T14:00:00Z')

test('a countdown reads days and hours, hours and minutes, then minutes and seconds', () => {
  expect(countdown('2026-10-26T04:00:00Z', NOW)).toBe('2d 14h')
  expect(countdown('2026-10-24T11:04:00Z', NOW)).toBe('21h 04m')
  expect(countdown('2026-10-23T14:42:10Z', NOW)).toBe('42m 10s')
  expect(countdown('2026-10-23T14:00:00Z', NOW)).toBe('expired')
  expect(countdown('2026-10-23T13:00:00Z', NOW)).toBe('expired')
  expect(countdown('nonsense', NOW)).toBe('?')
})

test('an age reads in its largest unit', () => {
  expect(elapsed('2026-10-23T13:59:20Z', NOW)).toBe('40s')
  expect(elapsed('2026-10-23T13:56:00Z', NOW)).toBe('4m')
  expect(elapsed('2026-10-23T11:00:00Z', NOW)).toBe('3h')
  expect(elapsed('2026-10-21T14:00:00Z', NOW)).toBe('2d')
  expect(elapsed('2026-10-23T14:00:05Z', NOW)).toBe('0s')
})

test('a moment is shown in UTC as people read it', () => {
  expect(showUtc('2026-10-25T20:14:09Z')).toBe('2026-10-25 20:14 UTC')
  expect(showUtc('not a time')).toBe('not a time')
})

test('marks are single-width symbols on the terminal and emoji elsewhere', () => {
  expect(mark('failed', 'terminal')).toBe('✖')
  expect(mark('failed', 'desktop')).toBe('❌')
  expect(mark('merged', 'vscode')).toBe('✅')
})

test('a claim is amber from the warning and red in its last hours, but only once expiry is enforced', () => {
  const claims = snapshot().claims
  if (claims === null || claims.error !== null) throw new Error('the fixture holds claims')
  const [claim] = claims.claims
  if (claim === undefined) throw new Error('the fixture holds a claim')
  const rules = { warn_hours: 48, deadline_hours: 72, enforced: true }
  const at = (hours: number) => ({ ...claim, deadline: new Date(NOW + hours * 3_600_000).toISOString() })
  expect(claimTone(at(30), rules, 6, NOW)).toBe('normal')
  expect(claimTone(at(24), rules, 6, NOW)).toBe('warn')
  expect(claimTone(at(5), rules, 6, NOW)).toBe('bad')
  expect(claimTone(at(5), { ...rules, enforced: false }, 6, NOW)).toBe('normal')
  expect(claimTone({ ...claim, deadline: null }, rules, 6, NOW)).toBe('dim')
})

test('the mod folder belongs to the checkout two levels up, with either slash', () => {
  expect(checkoutOf(String.raw`C:\ws\repos\coney\tools\claude-mod`)).toBe('C:/ws/repos/coney')
  expect(checkoutOf('/home/a/coney/tools/claude-mod/')).toBe('/home/a/coney')
})

test('an error is cut to its first line', () => {
  expect(firstLine('\nfirst\nsecond')).toBe('first')
  expect(firstLine('')).toBe('unknown error')
  expect(firstLine('x'.repeat(200))).toHaveLength(120)
})

test('a local read keeps the pull requests and claims the last GitHub read found', () => {
  const github: Held = merged(EMPTY, { ok: true, snapshot: snapshot() }, true, NOW)
  expect(github.githubAt).toBe(NOW)

  const local = snapshot()
  local.github = false
  local.claims = null
  local.prs = null
  local.issues = null
  if (local.units.error === null) {
    for (const unit of local.units.units) {
      unit.pr = null
      unit.overlaps = unit.overlaps.filter(overlap => overlap.kind === 'unit')
    }
  }
  const after = merged(github, { ok: true, snapshot: local }, false, NOW + 15_000)

  expect(after.localAt).toBe(NOW + 15_000)
  expect(after.githubAt).toBe(NOW)
  expect(after.snapshot?.github).toBe(true)
  expect(after.snapshot?.claims).not.toBeNull()
  expect(after.snapshot?.prs).not.toBeNull()
  const units = after.snapshot?.units
  const frame = units !== undefined && units.error === null ? units.units.find(unit => unit.task === 'fix-frame-812') : undefined
  expect(frame?.pr?.number).toBe(812)
  expect(frame?.overlaps.map(overlap => overlap.kind)).toContain('unit')
})

test('a failed read keeps what was held and says why, local and GitHub apart', () => {
  const held: Held = merged(EMPTY, { ok: true, snapshot: snapshot() }, true, NOW)
  const local = merged(held, { ok: false, error: 'uv missing' }, false, NOW + 1)
  expect(local.error).toBe('uv missing')
  expect(local.githubError).toBeNull()
  expect(local.snapshot).toBe(held.snapshot)
  const github = merged(held, { ok: false, error: 'gh down' }, true, NOW + 1)
  expect(github.githubError).toBe('gh down')
  expect(github.error).toBeNull()
})

test('a snapshot of a newer schema is not taken in', () => {
  const next = { ...snapshot(), schema: 99 }
  const after = merged(EMPTY, { ok: true, snapshot: next }, false, NOW)
  expect(after.snapshot).toBeNull()
  expect(after.newer).toBe(99)
})

test("the private Python environment sits in the checkout's own scratch folder", () => {
  expect(venvOf('C:/ws/worktrees/coney/fix-x')).toBe('C:/ws/scratch/fix-x/conclave-venv')
  expect(venvOf('/home/a/ws/worktrees/coney/b/')).toBe('/home/a/ws/scratch/b/conclave-venv')
  // the main checkout has no branch folder of its own: it gets `main`, which no worktree can be named
  expect(venvOf(String.raw`C:\ws\repos\coney`)).toBe('C:/ws/scratch/main/conclave-venv')
  // a worktree named like the main checkout's folder shares nothing with it
  expect(venvOf('/home/a/ws/worktrees/coney/coney')).toBe('/home/a/ws/scratch/coney/conclave-venv')
  // outside the workspace layout the mod cannot say where the scratch folder is, so it leaves the variable alone
  expect(venvOf('/home/a/code/coney')).toBeNull()
})

test('the saved agent list is kept per checkout, whatever the slashes', () => {
  const main = offerKey('/ws/repos/coney/tools/claude-mod')
  const tree = offerKey('/ws/worktrees/coney/fix-x/tools/claude-mod')
  expect(main).not.toBe(tree)
  expect(offerKey(String.raw`\ws\worktrees\coney\fix-x\tools\claude-mod`)).toBe(tree)
  expect(tree).toBe('offer:/ws/worktrees/coney/fix-x')
})

test('a line is clipped with an ellipsis, and tokens wrap without splitting', () => {
  expect(clip('abcdef', 6)).toBe('abcdef')
  expect(clip('abcdefg', 6)).toBe('abcde…')
  expect(wrapTokens(['✔ assigned', '✔ claimed', '✖ compared'], '  ', 22)).toEqual(['✔ assigned  ✔ claimed', '✖ compared'])
  expect(wrapTokens(['x'.repeat(30)], ' ', 10)).toEqual(['xxxxxxxxx…'])
  expect(wrapTokens([], ' ', 10)).toEqual([''])
})

test('a claim line says the same in every layout, and warns only when it has a deadline', () => {
  const claims = snapshot().claims
  if (claims === null || claims.error !== null) throw new Error('the fixture holds claims')
  const [first] = claims.claims
  if (first === undefined) throw new Error('the fixture holds a claim')
  const rules = { ...claims.rules, enforced: true }
  const timed = { ...first, warned: true }
  const waiting = { ...first, warned: true, deadline: null, waiting: 'S-blocked' }

  // wide: who and stage in the middle, the countdown last; compact: the countdown leads, the stage is left to the row
  expect(claimLineText(timed, rules, NOW, 'wide')).toBe(`  #741  ${first.claimant}  claimed  expires in 2d 14h  (warned)`)
  expect(claimLineText(timed, rules, NOW, 'compact')).toBe(`  #741 expires in 2d 14h  ${first.claimant} (warned)`)
  // narrow: the bare countdown and the warning ahead of the claimant, so a clipped line keeps them; `~` while unenforced
  expect(claimLineText(timed, rules, NOW, 'narrow')).toBe(`  #741 2d 14h (warned) ${first.claimant}`)
  expect(claimLineText(timed, { ...rules, enforced: false }, NOW, 'narrow')).toBe(`  #741 ~2d 14h (warned) ${first.claimant}`)
  // a claim that waits has no deadline to warn about, in any layout
  for (const layout of ['wide', 'compact', 'narrow'] as const) expect(claimLineText(waiting, rules, NOW, layout)).not.toContain('warned')
  expect(claimLineText(waiting, rules, NOW, 'compact')).toContain('waits while S-blocked')
  expect(claimLineText(waiting, rules, NOW, 'narrow')).toBe(`  #741 waits: S-blocked ${first.claimant}`)
  expect(claimLineText({ ...first, warned: false }, rules, NOW, 'wide')).not.toContain('warned')
})

test("the pane picks its layout by width from the snapshot's three limits", () => {
  const config = { ...snapshot().config, pane_min_columns: 30, pane_compact_columns: 60, pane_wide_columns: 110 }
  expect([1, 29, 30, 59, 60, 109, 110, 200].map(columns => paneLayout(columns, config))).toEqual([
    'line', 'line', 'narrow', 'narrow', 'compact', 'compact', 'wide', 'wide',
  ])
})

test('items are packed into lines no wider than the pane, each item whole', () => {
  expect(packLines(['abc', 'def', 'ghi'], ' · ', 9)).toEqual([['abc', 'def'], ['ghi']])
  expect(packLines(['x'.repeat(30)], ' · ', 10)).toEqual([['xxxxxxxxx…']])
  expect(packLines([], ' · ', 10)).toEqual([[]])
})

test('a stage reads the same on the band, the status line and the pane', () => {
  const units = snapshot().units
  const unit = units.error === null ? units.units.find(one => one.task === 'fix-frame-812') : undefined
  expect(unit && stageText(unit)).toBe('compared failed')
  expect(unit && issueIds(unit)).toBe('#741')
})
