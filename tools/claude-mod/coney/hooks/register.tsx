// Coney's coordinator mod (docs/guides/coordinator-mod.md): one pane, live, a band of counts above the prompt, a status
// line, and the session's waking.
//
// Every POLL_MS it runs `coney-tools coord status --json` from the session's folder (the checkout), and every
// GITHUB_EVERY-th read with --github, so the local parts (PCSX2 copies, build slots, release queue, tracks) are fresh
// and GitHub (issues, pull requests, CI) is read every two minutes. The report's `action` ids are what waits for the
// coordinator. When new ids wait (or ids still wait after the last wake: REWAKE_MS, then twice as long after each
// repeat, at most REWAKE_MAX_MS), it submits "Coordinator: new work." with the ids (at most REASON_ITEMS named), which
// starts a turn once the session is idle: no timer loop, no model turn while nothing happens. It never wakes while the
// main loop's turn runs or a wake it sent has not started yet, so one turn takes in everything that changed meanwhile;
// a wake no turn picks up within DROPPED_MS of idle is sent again. A wake the engine refuses (after the person
// interrupts a turn) is shown in the status line and a toast once, and tried again DROPPED_MS later or after the
// person's next turn. With CONEY_IDLE_WAKE_MINUTES set, the session is also woken once it has sat that long idle, its
// turn ended and no subagent at work. Pull-request problems (`pr:` ids) are Tether's to wake for (data.ts,
// OWNED_ELSEWHERE): they show here and never wake the session twice. A failed read is shown and told, never taken for
// "nothing waits". It reads only: it never writes a file and never runs anything but coney-tools and git status.
//
// The pane ("Coney", /coney) has the tabs Work, Machine and Files, each with sub-tabs, and a tip row at its foot. No
// tab carries a count or a hotkey: the counts live in the band. Every function that touches $ lives in this file,
// beside the hooks, and so do the atoms: the tabs and the band draw from plain data and act through `actions`.
import { atom, read, update } from 'claude-code'
import type { EngineInterface, Register } from 'claude-code'

import type { Changes, CoordStatus, Listing, Preview, Reading, View, Watch } from '../types'
import { drawBand } from './band'
import { coordArgv, failed, HIDDEN, merged, PREVIEW_BYTES, wakeable, why } from './data'
import { FilesTab } from './files'
import { C, kindOf, Tabs, TipRow } from './kit'
import type { Act } from './kit'
import { MachineTab } from './machine'
import { WorkTab } from './work'

const PANE = 'coney'
const TITLE = 'Coney'
const POLL_MS = 15_000
const GITHUB_EVERY = 8 // reads: GitHub is read with every eighth (two minutes)
const REWAKE_MS = 30 * 60_000
const REWAKE_MAX_MS = 4 * 60 * 60_000 // ids left waiting are mostly blocked elsewhere: each repeat waits longer
const CHANGES_MS = 30_000
const READ_TIMEOUT_MS = 90_000
const GITHUB_TIMEOUT_MS = 240_000

// What the mod keeps in $.state: what it read, and where the person is in the pane.
const reading = atom({ plugin: 'coney', key: 'reading' } as const, { last: null, github: null } as Reading)
const watch = atom({ plugin: 'coney', key: 'watch' } as const, {
  readAt: null,
  githubAt: null,
  failure: null,
  githubFailure: null,
  wokenFor: [],
  wokenAt: null,
  refused: null,
  agents: 0,
} as Watch)
const view = atom({ plugin: 'coney', key: 'view' } as const, {
  tab: 'work',
  workSub: 'waiting',
  machineSub: 'pcsx2',
  allIssues: false,
  filesSub: 'tree',
  open: [''],
  file: null,
} as View)
const tree = atom({ plugin: 'coney', key: 'tree' } as const, {} as Record<string, Listing>)
const changes = atom({ plugin: 'coney', key: 'changes' } as const, { at: null, lines: [], error: null } as Changes)
const preview = atom({ plugin: 'coney', key: 'preview' } as const, null as Preview | null)
const focusTip = atom({ plugin: 'coney', key: 'focusTip' } as const, null as string | null)

// The main loop's turns: whether one runs, when the last one ended (the mod's start counts), and when a wake was
// submitted that no turn has picked up yet (0: none). One wake stands at a time: a second while one waits, or while
// a turn runs, would queue a turn per poll.
// A wake the engine refused (after the person interrupts a turn, a Stop stands until they type) sets refusedAt and
// keeps the refusal's reason, so it is told once.
const main = { busy: false, idleSince: 0, wakeQueuedAt: 0, refusedAt: 0, refusal: null as string | null }
// A submitted wake that no turn picked up while the session sat idle this long was dropped: wake again. A refused
// wake is tried again after the same time.
const DROPPED_MS = 2 * 60_000
const AT_WORK = new Set(['pending', 'running', 'waiting'])

// Ids a wake names, at most; the rest are counted. The Work tab's Waiting list stays the full list.
const REASON_ITEMS = 8
const WAKE_LINE = 'Coordinator: new work.'

function named(ids: string[]): string {
  const more = ids.length > REASON_ITEMS ? ` (+${ids.length - REASON_ITEMS} more)` : ''
  return `${ids.slice(0, REASON_ITEMS).join(', ')}${more}`
}

// One read of coord status: the report, or why it could not be read. Exit 0 is the only good one.
async function readStatus($: EngineInterface, repo: string, github: boolean): Promise<CoordStatus | string> {
  try {
    const timeoutMs = github ? GITHUB_TIMEOUT_MS : READ_TIMEOUT_MS
    const r = await $.process.run(coordArgv(repo, github), { timeoutMs })
    if (r.exitCode !== 0) return failed(r)
    const doc = JSON.parse(r.stdout) as CoordStatus
    return Array.isArray(doc?.action) ? doc : 'coord status gave no action list (is the checkout older?)'
  } catch (err) {
    return why(err)
  }
}

// Subagents at work now (their count shows in the status line and the band; the idle wake waits for none).
async function agentsAtWork($: EngineInterface): Promise<number> {
  try {
    return (await $.agent.list()).filter(a => AT_WORK.has(a.status)).length
  } catch {
    return 0
  }
}

const polling = { busy: false, reads: 0, idleWakeMinutes: 0 }

async function poll($: EngineInterface, repo: string): Promise<void> {
  if (polling.busy) return // a slow read never piles up behind itself
  polling.busy = true
  try {
    await pollOnce($, repo, polling.reads++ % GITHUB_EVERY === 0)
  } finally {
    polling.busy = false
  }
}

async function pollOnce($: EngineInterface, repo: string, github: boolean): Promise<void> {
  const got = await readStatus($, repo, github)
  const ms = await $.clock.now()
  const now = new Date(ms).toISOString()
  const before = await read($, watch)
  const agents = await agentsAtWork($)
  const failure = typeof got === 'string' ? got : null
  if (failure && failure !== (github ? before.githubFailure : before.failure)) {
    void $.ui.toast(`Coney: coord status${github ? ' --github' : ''} could not be read (${failure})`)
  }
  if (typeof got !== 'string') await update($, reading, r => ({ last: got, github: github ? got : r.github }))
  const s = merged(await read($, reading))
  const items = wakeable(s)
  const refusedNote = () => (main.refusedAt ? ' · wake refused' : '')
  if (!s && failure) void $.ui.status('Coney: coord status not read')
  if (s) void $.ui.status(`Coney: ${items.length} waiting · ${agents} agent${agents === 1 ? '' : 's'}${refusedNote()}`)
  const readTimes = {
    readAt: failure ? before.readAt : now,
    githubAt: github && !failure ? now : before.githubAt,
    // A --github read holds the local parts too, so either good read clears a local failure; a failed --github read
    // counts as one only before anything was read
    failure: failure ? (github && before.readAt ? before.failure : failure) : null,
    githubFailure: github ? failure : before.githubFailure,
    agents,
  }
  if (failure || !s) {
    // Nothing new was read: the ids waiting are as they were, so the wake state stays as it is
    await update($, watch, w => ({ ...w, ...readTimes }))
    return
  }

  // A dropped wake woke no one: what it named counts as not woken for, so it is sent again now. So does a refused
  // one, once its wait is over or a turn the person started runs (what that turn did not handle wakes after it).
  let dropped = false
  if (main.wakeQueuedAt && !main.busy && ms - Math.max(main.wakeQueuedAt, main.idleSince) > DROPPED_MS) {
    main.wakeQueuedAt = 0
    dropped = true
  }
  if (main.refusedAt && (main.busy || ms - main.refusedAt > DROPPED_MS)) {
    main.refusedAt = 0
    dropped = true
  }
  const prior = dropped ? [] : before.wokenFor
  const free = !main.busy && !main.wakeQueuedAt && !main.refusedAt
  const wake = (reason: string): void => {
    main.wakeQueuedAt = ms
    void $.prompt.submit({ text: `${WAKE_LINE}\n${reason}` }).then(
      r => (r.drop === undefined ? (main.refusal = null) : refused($, r.drop)),
      err => refused($, why(err)),
    )
  }
  // Ids handled since the last wake leave the set, so one that comes back counts as new again.
  const repeats = before.repeats ?? 0
  let woken = { wokenFor: prior.filter(i => items.includes(i)), wokenAt: before.wokenAt, repeats }
  if (items.length > 0) {
    // Only new ids wake, never work shrinking as the coordinator handles it; and never while a turn runs or a wake waits
    const fresh = items.filter(i => !prior.includes(i))
    const after = Math.min(REWAKE_MS * 2 ** repeats, REWAKE_MAX_MS)
    const stale = before.wokenAt !== null && ms - Date.parse(before.wokenAt) > after
    if ((fresh.length > 0 || stale) && free) {
      const reason =
        fresh.length > 0
          ? `${named(fresh)}.`
          : `Still waiting ${after / 60_000} minutes after the last wake: ${named(items)}.`
      wake(reason)
      woken = { wokenFor: items, wokenAt: now, repeats: fresh.length > 0 ? 0 : repeats + 1 }
    }
  } else woken.repeats = 0
  const every = polling.idleWakeMinutes
  if (every > 0 && woken.wokenAt === before.wokenAt && free && agents === 0) {
    const last = Math.max(main.idleSince, before.wokenAt ? Date.parse(before.wokenAt) : 0)
    if (ms - last > every * 60_000) {
      wake(`Nothing new; woken after ${every} idle minutes with no subagent at work: take the next item in your state.`)
      woken = { ...woken, wokenAt: now }
    }
  }
  const refusal = main.refusedAt ? main.refusal : null
  await update($, watch, () => ({ ...readTimes, ...woken, refused: refusal }))
}

// The engine refused a wake: no turn will pick it up, so it stops counting as queued and waits to be tried again.
async function refused($: EngineInterface, reason: string): Promise<void> {
  main.wakeQueuedAt = 0
  main.refusedAt = await $.clock.now()
  if (main.refusal !== reason) {
    void $.ui.toast(`Coney: the wake was refused (${reason}). Type any line in this window to resume.`)
  }
  main.refusal = reason
}

// --- the Files tab's reads: the session's own folder ----------------------------------------------------------

async function listFolder($: EngineInterface, rel: string): Promise<void> {
  let listing: Listing
  try {
    const entries = (await $.fs.list(rel || undefined))
      .filter(e => !HIDDEN.has(e.name))
      .map(e => ({ name: e.name, dir: e.kind === 'dir', size: e.size, mtimeMs: e.mtimeMs }))
      .sort((a, b) => (a.dir === b.dir ? a.name.localeCompare(b.name) : a.dir ? -1 : 1))
    listing = { entries, error: null }
  } catch (err) {
    listing = { entries: [], error: why(err) }
  }
  await update($, tree, t => ({ ...t, [rel]: listing }))
}

async function readChanges($: EngineInterface): Promise<void> {
  const at = new Date(await $.clock.now()).toISOString()
  try {
    const r = await $.process.run(['git', 'status', '--porcelain=v1'], { timeoutMs: 20_000 })
    if (r.exitCode !== 0) throw new Error(failed(r))
    const lines = r.stdout
      .split('\n')
      .filter(l => l.length > 3)
      .map(l => ({ mark: l.slice(0, 2), path: l.slice(3).trim().replace(/\//g, '\\') }))
    await update($, changes, () => ({ at, lines, error: null }))
  } catch (err) {
    await update($, changes, c => ({ ...c, at, error: why(err) }))
  }
}

async function openPreview($: EngineInterface, rel: string): Promise<void> {
  try {
    const st = await $.fs.stat(rel)
    const size = (st as { size?: number }).size ?? 0
    const mtimeMs = (st as { mtimeMs?: number }).mtimeMs ?? 0
    const text = size > PREVIEW_BYTES ? null : await $.fs.read(rel)
    const error = size > PREVIEW_BYTES ? `too large to preview (${size} bytes)` : null
    await update($, preview, () => ({ path: rel, text, size, mtimeMs, error }))
  } catch (err) {
    await update($, preview, () => ({ path: rel, text: null, size: 0, mtimeMs: 0, error: why(err) }))
  }
}

// What a drawing's handlers may do, made here where $ is, and handed to the tabs as plain functions.
function actions($: EngineInterface): Act {
  return {
    edit: fn => update($, view, fn),
    fill: text => $.prompt.fill({ text, mode: 'insert' }),
    list: rel => listFolder($, rel),
    preview: rel => openPreview($, rel),
    changes: () => readChanges($),
  }
}

// Opens the pane for the person's command and brings it in front of any other pane's tab (Tether's pane may be the
// one shown): `focus` raises it. Returns why it is not drawn, or null when it is.
async function raise($: EngineInterface): Promise<string | null> {
  const opened = await $.ui.open({ id: PANE, title: TITLE, focus: true })
  return opened.isPlaced ? null : `The Coney pane is open but not drawn: ${opened.reason}`
}

async function changesTick($: EngineInterface): Promise<void> {
  if ((await read($, view)).tab === 'files') await readChanges($)
}

// The idle wake's minutes from the environment the launcher sets (tools/coordinator.ps1 -IdleWakeMinutes); 0: off.
async function idleWakeMinutes($: EngineInterface): Promise<number> {
  try {
    const raw = await $.env.get('CONEY_IDLE_WAKE_MINUTES')
    const n = Number(raw ?? 0)
    return Number.isFinite(n) && n > 0 ? n : 0
  } catch {
    return 0
  }
}

// The commands and the reads start once per load of the mod: at the session's start, or, when the mod was loaded
// into a session already running, at the main loop's next turn or the next /coney. Never from a drawing: a render
// hook's $ calls nothing once it has drawn, so reads started there never come back.
let started = false

async function start($: EngineInterface): Promise<void> {
  if (started) return
  started = true
  // the commands of an earlier load stay registered, so one refused here still runs: the reads start regardless
  void $.command
    .register({ name: 'coney', description: "Show the coordinator's Coney pane (Work, Machine, Files)" })
    .catch(err => $.ui.toast(`Coney: the /coney command cannot be registered (${why(err)})`))
  void (async () => {
    try {
      const repo = await $.session.cwd()
      polling.idleWakeMinutes = await idleWakeMinutes($)
      await poll($, repo)
      $.clock.every(POLL_MS, () => poll($, repo))
      $.clock.every(CHANGES_MS, () => changesTick($))
    } catch (err) {
      await update($, watch, w => ({ ...w, failure: `cannot start: ${why(err)}` }))
      void $.ui.toast(`Coney: the coordinator pane cannot start (${why(err)})`)
    }
  })()
}

export const register: Register = on => {
  on('turn.start', async ($, e, next) => {
    if (e.agentId === undefined) {
      main.busy = true
      main.wakeQueuedAt = 0
      void start($)
    }
    return next(e)
  })

  on('turn.complete', async ($, e, next) => {
    if (e.agentId === undefined) {
      main.busy = false
      main.idleSince = await $.clock.now()
    }
    return next(e)
  })

  on('session.start', async ($, e, next) => {
    main.idleSince = await $.clock.now()
    void $.ui.open({ id: PANE, title: TITLE })
    await start($)
    return next(e)
  })

  on('command.run', { command: 'coney' }, async $ => {
    void start($)
    const why = await raise($)
    return { text: why ?? 'Coney pane opened.' }
  })

  on('ui.render', { component: 'Pane', requestId: PANE }, async ($, e) => {
    const el = $.ui.resolve(e)
    const { Box, Text } = el
    const width = Math.max(24, e.props.bodyColumns)
    const full = Math.max(10, e.props.scroll.bodyRows)
    const rows = full - 5 // the tab row and the tip row with its rule take the rest
    const s = merged(await read($, reading))
    const w = await read($, watch)
    const v = await read($, view)
    const now = await $.clock.now()
    const act = actions($)
    const go = (t: View['tab']) => async () => {
      await update($, view, x => ({ ...x, tab: t }))
      // The Files tab lists the session's folder the first time it is opened; a press on a folder lists the rest
      if (t === 'files' && !(await read($, tree))['']) await Promise.all([listFolder($, ''), readChanges($)])
    }
    const tabs = Tabs(el, [
      { key: 'work', label: 'Work', active: v.tab === 'work', onPress: go('work') },
      { key: 'machine', label: 'Machine', active: v.tab === 'machine', onPress: go('machine') },
      { key: 'files', label: 'Files', active: v.tab === 'files', onPress: go('files') },
    ])
    let body
    if (v.tab === 'files') {
      const root = await $.session.cwd()
      const listed = await read($, tree)
      body = FilesTab(act, el, root, listed, await read($, changes), await read($, preview), v, width, rows)
    } else if (!s) {
      const said = w.failure ? `Not read: ${w.failure}` : 'Reading coord status…'
      body = <Text color={w.failure ? C.bad : undefined}>{said}</Text>
    } else if (v.tab === 'machine') {
      body = MachineTab(act, el, s, v, width, rows, now)
    } else {
      body = WorkTab(act, el, s, w, v, width, rows, now)
    }
    // The tip row sits at the pane's foot: the body grows to push it there while the tab's content is short.
    return (
      <Box flexDirection="column" minHeight={full}>
        {tabs}
        <Box flexDirection="column" flexGrow={1}>
          {s && w.failure && v.tab !== 'files' && <Text color={C.bad}>Last read failed: {w.failure}</Text>}
          {body}
        </Box>
        {TipRow(el, width, await read($, focusTip))}
      </Box>
    )
  })

  // The keyboard's focus moving in the pane: the focused element's tip shows in the tip row.
  on('ui.focus', { requestId: PANE }, async ($, e, next) => {
    await update($, focusTip, () => kindOf(e.element))
    return next(e)
  }).catch(($, e, next) => next(e)) // a tip that cannot be set never holds the focus back

  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => {
    if (e.props.hasSurvey) return next(e)
    const el = $.ui.resolve(e)
    const s = merged(await read($, reading))
    const w = await read($, watch)
    if (!s && !w.failure) return next(e)
    const drawn = drawBand(el, { s, w, width: e.props.bodyColumns })
    return drawn ?? next(e)
  })
}
