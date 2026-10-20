import { expect, mock, test } from 'claude-code/testing'

import type { CoordStatus } from '../types'

// Synthetic data only: made-up branches, agents, ids and times. The mocked clock starts at NOW.
const NOW = 100_000_000
const MIN = 60_000
const iso = (ms: number) => new Date(ms).toISOString().replace(/\.\d{3}Z$/, 'Z')

const LOCAL: CoordStatus = {
  time: iso(NOW),
  checkout: 'C:\\Work',
  github: false,
  worktrees: { error: null, count: 3 },
  pcsx2: { error: null, copies: [
    { copy: 'copy-a', path: 'C:\\E\\a', port: 1, state: 'held', claim: { agent: 'an-ai', time: iso(NOW - 20 * MIN) },
      running: true, sdl: false, pids: [4] },
    { copy: 'copy-b', path: 'C:\\E\\b', port: 2, state: 'free', claim: null, running: false, sdl: true, pids: [] },
  ] },
  builds: { error: null, lanes: {
    main: { slots: [
      { slot: 1, stale: false, claim: { slot: 1, agent: 'fix-x', worktree: 'fix-x', preset: 'dev', step: 'build', pid: 9,
        started: iso(NOW - 5 * MIN), lane: 'main' } },
      { slot: 2, stale: false, claim: null },
    ], queue: [{ position: 1, stale: false, agent: 'impl-y', worktree: 'impl-y', preset: 'dev', time: 0, pid: 3 }] },
    fast: { slots: [{ slot: 1, stale: false, claim: null }], queue: [] },
  } },
  results: { error: null, results: [
    { worktree: 'fix-x', branch: 'fix-x', state: 'done', agent: 'fix-x', preset: 'dev', exit_code: 0,
      started: iso(NOW - 40 * MIN), ended: iso(NOW - 10 * MIN), recent: true },
  ] },
  tracks: { error: null, tracks: [
    { branch: 'fix-x', status: 'C:\\S\\fix-x\\STATUS.md', changed: iso(NOW - 3 * MIN), worktree: 'C:\\W\\fix-x', busy: true },
    { branch: 'old-z', status: 'C:\\S\\old-z\\STATUS.md', changed: iso(NOW - 26 * 60 * MIN), worktree: null, busy: false },
  ] },
  release: { error: null, units: [
    { branch: 'fix-x', file: 'PR.md', path: 'C:\\S\\fix-x\\PR.md', changed: iso(NOW - 2 * MIN), worktree: 'C:\\W\\fix-x',
      pushed: false, pr: null, pr_state: null, queued: null },
    { branch: 'impl-y', file: 'PR.md', path: 'C:\\S\\impl-y\\PR.md', changed: iso(NOW - 90 * MIN), worktree: 'C:\\W\\impl-y',
      pushed: true, pr: 41, pr_state: 'OPEN', queued: null },
  ] },
  issues: null,
  prs: null,
  ci: null,
  action: ['build:done:fix-x'],
} // prettier-ignore

const GITHUB: CoordStatus = {
  ...LOCAL,
  github: true,
  release: { error: null, units: [
    { ...LOCAL.release.units![0], queued: true },
    { ...LOCAL.release.units![1], queued: false },
  ] },
  issues: { error: null, counts: { 'S-needs-triage': 1, 'S-blocked': 1 }, notices: [], issues: [
    { number: 12, status: 'S-needs-triage', priority: null, area: 'A-ai', labels: ['bug'], created: iso(NOW - 48 * 60 * MIN),
      updated: iso(NOW - MIN), age_days: 2 },
    { number: 3, status: 'S-blocked', priority: 'P-high', area: 'A-tools', labels: ['task'], created: iso(NOW - 10 * 1440 * MIN),
      updated: iso(NOW - MIN), age_days: 10 },
  ] },
  prs: { error: null, health: { stuck: [{ number: 41, cause: 'conflict' }] }, prs: [
    { number: 41, branch: 'impl-y', base: 'main', draft: false, mergeable: 'CONFLICTING', auto_merge: true, ci: 'fail',
      updated: iso(NOW - 5 * MIN), causes: ['conflict'] },
  ] },
  ci: { error: null, queued: 3, in_progress: 7 },
  action: ['build:done:fix-x', 'issue:12:S-needs-triage', 'pr:41:conflict', 'unit:fix-x:PR.md'],
} // prettier-ignore

// Boots the mod: coney-tools answers a --github read with `github` (or fails with it as a message) and a local read
// with `local`; git status lists one change.
async function boot($: any, on: any, z: { local: CoordStatus | string; github: CoordStatus | string }) {
  const clock = mock.clock(on, { now: NOW })
  const runs: string[][] = []
  const fills: string[] = []
  on('process.run', async (_$: unknown, e: any) => {
    runs.push(e.argv)
    const doc = e.argv[0] === 'git' ? ' M docs\\x.md\n' : e.argv.includes('--github') ? z.github : z.local
    if (typeof doc === 'string' && e.argv[0] !== 'git') {
      return { value: { exitCode: 2, stdout: '', stderr: doc, isStdoutTruncated: false, isStderrTruncated: false } }
    }
    const stdout = typeof doc === 'string' ? doc : JSON.stringify(doc)
    return { value: { exitCode: 0, stdout, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }
  })
  on('fs.list', async () => ({ value: [{ name: 'docs', kind: 'dir', size: 0, mtimeMs: 0 }, { name: 'README.md', kind: 'file', size: 10, mtimeMs: 0 }] }))
  on('prompt.submit', async (_$: unknown, e: any) => ({ text: e.text, context: [] }))
  on('prompt.fill', async (_$: unknown, e: any) => {
    fills.push(e.text)
    return { isFilled: true }
  })
  on('agent.list', async () => ({ value: [{ id: 'a', status: 'running' }, { id: 'b', status: 'completed' }] }))
  on('env.get', async () => ({ value: undefined }))
  on('command.register', async (_$: unknown, e: any) => ({ value: { command: e.name } }))
  on('ui.open', async () => ({ value: { isPlaced: true } }))
  on('ui.panes', async () => ({ value: [] }))
  on('ui.status', async () => ({ value: undefined }))
  on('ui.toast', async () => ({ value: undefined }))
  on('session.start', async () => ({ cwd: 'C:/Work' }))
  on('session.cwd', () => ({ value: 'C:\\Work' }))
  await $.session.start({ source: 'startup', cwd: 'C:/Work' })
  await clock.settle()
  return { clock, runs, fills }
}

const pane = (columns = 110, rows = 40) =>
  ({
    component: 'Pane',
    requestId: 'coney',
    props: { title: 'Coney', isFocused: true, bodyColumns: columns, placement: 'dock', scroll: { offset: 0, bodyRows: rows }, view: {} },
  }) as const

const band = (columns = 160) =>
  ({
    component: 'AbovePrompt',
    requestId: 'band',
    props: { hasSurvey: false, isWorking: false, maxRows: 5, bodyColumns: columns, scroll: { offset: 0, bodyRows: 5 }, view: {} },
  }) as const

const SURFACES = ['terminal', 'desktop'] as const

test('Work: the waiting ids, issues to triage, pull requests with pr-health, and the release queue', async ($, on) => {
  const { fills } = await boot($, on, { local: LOCAL, github: GITHUB })
  for (const surface of SURFACES) {
    const ui = await $.ui.mount({ plugin: 'coney', surface, ...pane() })
    for (const key of ['tab-work', 'tab-machine', 'tab-files']) expect(await ui.find({ key })).toBeDefined()
    for (const t of await ui.findAll({ type: 'Button' })) expect(t.props.hotkey).toBeUndefined()
    await ui.press({ key: 'tab-work' })
    await ui.press({ key: 'sub-waiting' })
    expect(await ui.find({ key: 'id-issue:12:S-needs-triage' })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /\(Tether\)/ })).toBeDefined()
    await ui.press({ key: 'id-unit:fix-x:PR.md' })
    expect(fills.at(-1)).toBe('unit:fix-x:PR.md ')

    await ui.press({ key: 'sub-issues' })
    expect(await ui.find({ type: 'Text', text: /^#12\s+S-needs-triage\s+-\s+A-ai\s+2d\s+bug/ })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /^#3\s/ })).toBeUndefined() // triaged: shown only with every issue
    await ui.press({ key: 'all-issues' })
    expect(await ui.find({ type: 'Text', text: /^#3\s+S-blocked\s+P-high/ })).toBeDefined()
    await ui.press({ key: 'all-issues' })

    await ui.press({ key: 'sub-prs' })
    expect(await ui.find({ type: 'Text', text: /impl-y · conflict/ })).toBeDefined()
    await ui.press({ key: 'sub-release' })
    expect(await ui.find({ type: 'Text', text: /1 queued of 2 unit files/ })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /^fix-x\/PR\.md\s+queued\s+no/ })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /^impl-y\/PR\.md\s+released #41\s+yes/ })).toBeDefined()
    await ui.press({ key: 'unit-fix-x/PR.md' })
    expect(fills.at(-1)).toBe('C:\\S\\fix-x\\PR.md ')
    await ui.press({ key: 'sub-waiting' })
    await ui.unmount()
  }
})

test('Machine: PCSX2 copies, build slots and detached builds, and the tracks', async ($, on) => {
  await boot($, on, { local: LOCAL, github: GITHUB })
  for (const surface of SURFACES) {
    const ui = await $.ui.mount({ plugin: 'coney', surface, ...pane() })
    await ui.press({ key: 'tab-machine' })
    await ui.press({ key: 'sub-pcsx2' })
    expect(await ui.find({ type: 'Text', text: /^copy-a\s+held\s+yes\s+20m\s+an-ai/ })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /SDL is on in copy-b/ })).toBeDefined()
    expect(await ui.findAll({ type: 'Image' })).toHaveLength(0)
    await ui.press({ key: 'sub-builds' })
    expect(await ui.find({ type: 'Text', text: /^slot 1\s+build\s+5m\s+fix-x · fix-x · dev/ })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /queue: 1\. impl-y \(impl-y\)/ })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /exit 0, ended 10m ago/ })).toBeDefined()
    await ui.press({ key: 'sub-tracks' })
    expect(await ui.find({ type: 'Text', text: /2 tracks: 1 busy/ })).toBeDefined()
    expect(await ui.find({ key: 'track-fix-x' })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /^old-z\s+gone/ })).toBeDefined()
    await ui.press({ key: 'sub-pcsx2' })
    await ui.press({ key: 'tab-work' })
    await ui.unmount()
  }
})

test('Files: the checkout as a tree and its git changes', async ($, on) => {
  const { clock } = await boot($, on, { local: LOCAL, github: GITHUB })
  const ui = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...pane() })
  await ui.press({ key: 'tab-files' })
  await clock.settle()
  expect(await ui.find({ key: 'dir-docs' })).toBeDefined()
  expect(await ui.find({ key: 'file-README.md' })).toBeDefined()
  await ui.press({ key: 'sub-changes' })
  await clock.settle()
  expect(await ui.find({ key: 'change-docs\\x.md' })).toBeDefined()
  await ui.press({ key: 'sub-tree' })
  await ui.press({ key: 'tab-work' })
  await ui.unmount()
})

test('a local read after a --github read keeps the GitHub sections and ids', async ($, on) => {
  const { clock } = await boot($, on, { local: LOCAL, github: GITHUB })
  await clock.advance(15_000) // a local read
  const ui = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...pane() })
  await ui.press({ key: 'tab-work' })
  await ui.press({ key: 'sub-waiting' })
  expect(await ui.find({ key: 'id-issue:12:S-needs-triage' })).toBeDefined()
  await ui.press({ key: 'sub-release' })
  expect(await ui.find({ type: 'Text', text: /^fix-x\/PR\.md\s+queued/ })).toBeDefined()
  await ui.press({ key: 'sub-waiting' })
  await ui.unmount()
})

test('a GitHub read that fails is said, and the local parts still draw', async ($, on) => {
  const { clock } = await boot($, on, { local: LOCAL, github: 'coney-tools: gh is not signed in' })
  const first = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...band() })
  // the first read is the --github one: with nothing read yet, its failure is the band's whole text
  expect((await first.findAll({ type: 'Text' })).map(t => t.text).join('')).toContain('cannot be read')
  await first.unmount()
  await clock.advance(15_000) // a local read
  const b = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...band() })
  const text = (await b.findAll({ type: 'Text' })).map(t => t.text).join('')
  expect(text).toContain('1 waiting')
  expect(text).toContain('GitHub not read')
  await b.unmount()
  const ui = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...pane() })
  await ui.press({ key: 'tab-work' })
  await ui.press({ key: 'sub-issues' })
  expect(await ui.find({ type: 'Text', text: /Issues are read every two minutes; not read yet/ })).toBeDefined()
  expect(await ui.find({ type: 'Text', text: /Last GitHub read failed: coney-tools: gh is not signed in/ })).toBeDefined()
  await ui.press({ key: 'sub-waiting' })
  await ui.unmount()
})

test('the band holds the counts and drops its least needed parts when narrow', async ($, on) => {
  await boot($, on, { local: LOCAL, github: GITHUB })
  const wide = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...band(160) })
  const text = (await wide.findAll({ type: 'Text' })).map(t => t.text).join('')
  expect(text).toContain('Coney coordinator')
  expect(text).toContain('3 waiting')
  expect(text).toContain('1 agent')
  expect(text).toContain('1 PR (Tether)')
  expect(text).toContain('CI 3 queued, 7 running')
  expect(text).toContain('builds 1/3 +1 queued')
  expect(text).toContain('PCSX2 1/2')
  await wide.unmount()
  const narrow = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...band(40) })
  const short = (await narrow.findAll({ type: 'Text' })).map(t => t.text).join('')
  expect(short).toContain('3 waiting')
  expect(short).not.toContain('read ')
  await narrow.unmount()
})

test('at a phone width every tab still draws, with the tip row at the foot', async ($, on) => {
  await boot($, on, { local: LOCAL, github: GITHUB })
  const ui = await $.ui.mount({ plugin: 'coney', surface: 'terminal', ...pane(36, 20) })
  for (const key of ['tab-work', 'tab-machine', 'tab-files']) {
    await ui.press({ key })
    expect(await ui.drawn()).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /Point at anything here/ })).toBeDefined()
  }
  await ui.press({ key: 'tab-work' })
  await ui.unmount()
})
