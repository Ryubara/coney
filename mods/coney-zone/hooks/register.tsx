// Coney's zone mod (docs/workflow.md): one pane per zone, live, and the session's waking.
//
// Every POLL_MS it runs `coney-tools zone status --json` as the account the session runs as, so it shows only what
// that zone may see (ids, states and counts, never page text), and draws the zone's role, the role's inbox by state,
// the walls and, on the public side, the pages in main. When new items wait for the role (or items still wait
// REWAKE_MS after the last wake), it submits the role's line, "Follow <brief>", and a second line saying why (the
// new items by station, state and id, or the reason for a repeat or idle wake), which starts a turn once the
// session is idle: no timer loop, no model turn while nothing happens. It never wakes while the main loop's turn
// runs or a wake it sent has not started yet, so one turn takes in everything that changed meanwhile. A role whose own work goes on between inbox
// items (the status names its idle_wake_minutes) is also woken once it has sat that long idle, its turn ended and no
// subagent of it at work. A failed read is shown and told, never taken for an empty inbox. It reads only; it never
// writes and never sends anything to another zone.
import { atom, read, update } from 'claude-code'
import type { EngineInterface, Register } from 'claude-code'

import type { Watch, ZoneStatus } from '../types'

const PANE = 'coney-zone'
const POLL_MS = 15_000
const REWAKE_MS = 30 * 60_000
const status = atom({ plugin: 'coney-zone', key: 'status' } as const, null)
const watch = atom({ plugin: 'coney-zone', key: 'watch' } as const, {
  readAt: null,
  failure: null,
  wokenFor: [],
  wokenAt: null,
} as Watch)

type Paths = { python: string; tools: string; workspace: string }

// The main loop's turns: whether one runs, when the last one ended (the mod's start counts), and when a wake was
// submitted that no turn has picked up yet (0: none). One wake stands at a time: a second while one waits, or while
// a turn runs, would queue a turn per poll.
const main = { busy: false, idleSince: 0, wakeQueuedAt: 0 }
// A submitted wake that no turn picked up while the session sat idle this long was dropped: wake again.
const DROPPED_MS = 2 * 60_000
const AT_WORK = new Set(['pending', 'running', 'waiting'])

// The machine folder is where coney-tools and every installer put it: %ProgramData%\Coney.
async function paths($: EngineInterface): Promise<Paths> {
  const pd = await $.process.run(['cmd.exe', '/d', '/c', 'echo %ProgramData%'], { timeoutMs: 10_000 })
  const machine = `${pd.stdout.trim()}\\Coney`
  const workspace = `${machine}\\coney.workspace.json`
  const ws = JSON.parse(await $.fs.read(workspace))
  if (typeof ws.python !== 'string' || !ws.python) throw new Error('the workspace names no python')
  return { python: ws.python, tools: `${machine}\\tools\\coney-tools.py`, workspace }
}

function waiting(s: ZoneStatus | null): string[] {
  return [...(s?.inbox?.action ?? [])].sort()
}

// Items a wake names, at most; the rest are counted. The inbox, run by the woken turn, stays the full list.
const REASON_ITEMS = 8

// An inbox item ("<station>:<id>") as the wake names it: its station, state and id, as the pane shows them.
function described(s: ZoneStatus, item: string): string {
  const cut = item.indexOf(':')
  const [station, id] = cut < 0 ? ['', item] : [item.slice(0, cut), item.slice(cut + 1)]
  const state = s.inbox?.stations[station]?.pages.find(p => p.id === id)?.state
  return [station, state, id].filter(Boolean).join(' ')
}

// The wake's second line: why it was sent, in ids, states and counts only.
function newWork(s: ZoneStatus, fresh: string[]): string {
  const named = fresh.slice(0, REASON_ITEMS).map(i => described(s, i))
  const more = fresh.length > REASON_ITEMS ? ` (+${fresh.length - REASON_ITEMS} more)` : ''
  return `Woken for new work in your inbox: ${named.join(', ')}${more}.`
}

async function poll($: EngineInterface, where: Paths): Promise<void> {
  let next: ZoneStatus | null = null
  let failure: string | null = null
  try {
    const r = await $.process.run(
      [where.python, '-I', where.tools, 'zone', 'status', '--json', '--workspace', where.workspace],
      { timeoutMs: 60_000 },
    )
    if (r.exitCode === 0 || r.exitCode === 1) next = JSON.parse(r.stdout) as ZoneStatus
    else failure = r.stderr.trim().split('\n').pop() || `exit ${r.exitCode}`
  } catch (err) {
    failure = err instanceof Error ? err.message : String(err)
  }
  const ms = await $.clock.now()
  const now = new Date(ms).toISOString()
  const before = await read($, watch)
  if (failure && failure !== before.failure) void $.ui.toast(`Coney: the zone status could not be read (${failure})`)
  if (next) await update($, status, () => next)
  const s = next ?? (await read($, status))
  if (s?.walls.breach) void $.ui.status(`Coney ${s.zone}: BREACH FLAG`)
  else if (s?.walls.legacy_folders?.length) void $.ui.status(`Coney ${s.zone}: LEGACY COPY`)
  else if (s) void $.ui.status(`Coney ${s.zone}: ${waiting(s).length} waiting`)

  if (main.wakeQueuedAt && !main.busy && ms - Math.max(main.wakeQueuedAt, main.idleSince) > DROPPED_MS) {
    main.wakeQueuedAt = 0
  }
  const free = !main.busy && !main.wakeQueuedAt
  const wake = (brief: string, why: string): void => {
    void $.prompt.submit({ text: `Follow ${brief}\n${why}` })
    main.wakeQueuedAt = ms
  }
  // Items handled since the last wake leave the set, so one that comes back counts as new again
  const items = waiting(next)
  let woken = { wokenFor: before.wokenFor.filter(i => items.includes(i)), wokenAt: before.wokenAt }
  if (next && next.brief && items.length > 0) {
    // Only new work wakes, never work shrinking as the role handles it; and never while a turn runs or a wake waits
    const fresh = items.filter(i => !before.wokenFor.includes(i))
    const stale = before.wokenAt !== null && ms - Date.parse(before.wokenAt) > REWAKE_MS
    if ((fresh.length > 0 || stale) && free) {
      const why =
        fresh.length > 0
          ? newWork(next, fresh)
          : `Woken because ${items.length} waiting ${items.length === 1 ? 'item is' : 'items are'} still in your inbox ${REWAKE_MS / 60_000} minutes after the last wake.`
      wake(next.brief, why)
      woken = { wokenFor: items, wokenAt: now }
    }
  }
  const every = next?.idle_wake_minutes
  if (next && next.brief && every && woken.wokenAt === before.wokenAt && free) {
    const last = Math.max(main.idleSince, before.wokenAt ? Date.parse(before.wokenAt) : 0)
    if (ms - last > every * 60_000) {
      const agents = await $.agent.list()
      if (!agents.some(a => AT_WORK.has(a.status))) {
        wake(next.brief, `Woken after ${every} idle minutes with no subagent at work, for your own work between inbox items.`)
        woken = { wokenFor: woken.wokenFor, wokenAt: now }
      }
    }
  }
  await update($, watch, () => ({ readAt: now, failure, ...woken }))
}

export const register: Register = on => {
  on('turn.start', async ($, e, next) => {
    if (e.agentId === undefined) {
      main.busy = true
      main.wakeQueuedAt = 0
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
    await $.command.register({ name: 'coney', description: "Show this zone's Coney pane" })
    void $.ui.open({ id: PANE, title: 'Coney zone' })
    void (async () => {
      try {
        const where = await paths($)
        await poll($, where)
        $.clock.every(POLL_MS, () => poll($, where))
      } catch (err) {
        const why = err instanceof Error ? err.message : String(err)
        await update($, watch, w => ({ ...w, failure: `cannot start: ${why}` }))
        void $.ui.toast(`Coney: the zone pane cannot start (${why})`)
      }
    })()
    return next(e)
  })

  on('command.run', { command: 'coney' }, async $ => {
    await $.ui.open({ id: PANE, title: 'Coney zone' })
    return { text: 'Coney zone pane opened.' }
  })

  on('ui.render', { component: 'Pane', requestId: PANE }, async ($, e) => {
    const { Box, Text } = $.ui.resolve(e)
    const s = await read($, status)
    const w = await read($, watch)
    const room = Math.max(3, (e.viewport?.rows ?? 24) - 12)
    if (!s) {
      return (
        <Box flexDirection="column">
          <Text bold>Coney zone</Text>
          <Text dimColor>{w.failure ? `Not read: ${w.failure}` : 'Reading the zone status...'}</Text>
        </Box>
      )
    }
    const walls = s.walls
    const stations = Object.entries(s.inbox?.stations ?? {})
    const items = waiting(s)
    return (
      <Box flexDirection="column">
        <Text bold>
          {s.zone ?? 'no zone'} · {s.role ?? 'no looping role'} · {s.account}
        </Text>
        {walls.breach ? (
          <Text bold color="red">
            BREACH FLAG: {walls.breach_folders.join(', ') || 'see the zones check'}
          </Text>
        ) : (
          <Text dimColor={walls.passed !== null && walls.passed === walls.checks}>
            Walls: {walls.passed ?? '?'}/{walls.checks ?? '?'} checks passed at {walls.checked ?? 'never'}
          </Text>
        )}
        {walls.legacy_folders?.length ? (
          <Text bold color="yellow">
            LEGACY COPY: {walls.legacy_folders.join(', ')}
          </Text>
        ) : null}
        {s.pages_in_main !== null && <Text>Research pages in main: {s.pages_in_main}</Text>}
        {s.error && <Text color="red">Inbox not read: {s.error}</Text>}
        {stations.map(([name, st]) => (
          <Box flexDirection="column" marginTop={1}>
            <Text bold>
              {name}: {Object.entries(st.counts).filter(([, n]) => n > 0).map(([k, n]) => `${n} ${k}`).join(' · ') ||
                'empty'}
            </Text>
            {st.pages
              .filter(p => st.action.includes(p.id))
              .slice(0, room)
              .map(p => (
                <Text>
                  {'  '}
                  {p.state} {p.id}
                </Text>
              ))}
          </Box>
        ))}
        <Box marginTop={1} flexDirection="column">
          <Text dimColor>
            {items.length} waiting for this role · read {w.readAt?.slice(11, 19) ?? '-'} UTC every {POLL_MS / 1000} s
          </Text>
          <Text dimColor>Last woken: {w.wokenAt ? `${w.wokenAt.slice(11, 19)} UTC for ${w.wokenFor.length}` : 'not yet'}</Text>
          {w.failure && <Text color="red">Last read failed: {w.failure}</Text>}
        </Box>
      </Box>
    )
  })
}
