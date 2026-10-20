// The Machine tab: what the shared machine is doing (docs/guides/how-we-work.md#shared-machine): the PCSX2 copies
// and their claims, the build slots and queues with each worktree's last detached build, and the tracks' STATUS.md
// files. Words only: no screenshot, savestate or recording is ever drawn.
import type { CoordStatus, View } from '../types'
import { ago, clock } from './data'
import { C, columns, fit, More, NotRead, Row, Rule, SubTabs } from './kit'
import type { Act, El } from './kit'

export const MACHINE_SUBS: [string, string][] = [
  ['pcsx2', 'PCSX2'],
  ['builds', 'Builds'],
  ['tracks', 'Tracks'],
]

export function MachineTab(act: Act, el: El, s: CoordStatus, v: View, width: number, rows: number, now: number) {
  const { Box } = el
  const sub = MACHINE_SUBS.some(([k]) => k === v.machineSub) ? v.machineSub : 'pcsx2'
  const set = (machineSub: string) => () => act.edit(x => ({ ...x, machineSub }))
  const room = Math.max(5, rows - 4)
  let body
  if (sub === 'builds') body = Builds(el, s, width, room, now)
  else if (sub === 'tracks') body = Tracks(act, el, s, width, room, now)
  else body = Copies(el, s, width, now)
  return (
    <Box flexDirection="column">
      {SubTabs(
        el,
        MACHINE_SUBS.map(([key, label]) => ({ key, label, active: sub === key, onPress: set(key) })),
      )}
      <Box marginTop={1} flexDirection="column">
        {body}
      </Box>
    </Box>
  )
}

const COPY_COLOR: Record<string, string | undefined> = { held: C.info, stale: C.warn, 'in use?': C.warn }

function Copies(el: El, s: CoordStatus, width: number, now: number) {
  const { Box, Text } = el
  const p = s.pcsx2
  if (p.error && !p.copies) return NotRead(el, 'PCSX2', p.error, width)
  const copies = p.copies ?? []
  return (
    <Box flexDirection="column">
      {copies.length === 0 && <Text dimColor>No PCSX2 copies on this machine.</Text>}
      {copies.length > 0 && (
        <Text dimColor>{columns([['COPY', 10], ['STATE', 8], ['RUNS', 5], ['SINCE', 6], ['HOLDER', 0]], width)}</Text>
      )}
      {copies.map(c => (
        <Text key={`copy-${c.copy}`} color={COPY_COLOR[c.state]} dimColor={c.state === 'free'}>
          {columns(
            [
              [c.copy, 10],
              [c.state, 8],
              [c.running ? 'yes' : 'no', 5],
              [c.claim ? ago(c.claim.time, now) : '-', 6],
              [c.claim?.agent ?? '', 0],
            ],
            width,
          )}
        </Text>
      ))}
      {copies.some(c => c.sdl) && (
        <Text color={C.warn}>SDL is on in {copies.filter(c => c.sdl).map(c => c.copy).join(', ')}: a gamepad would drive it.</Text>
      )}
    </Box>
  )
}

function Builds(el: El, s: CoordStatus, width: number, room: number, now: number) {
  const { Box, Text } = el
  const b = s.builds
  const out = []
  if (b.error && !b.lanes) out.push(NotRead(el, 'Build slots', b.error, width))
  for (const [lane, l] of Object.entries(b.lanes ?? {})) {
    out.push(<Box key={`rule-${lane}`}>{Rule(el, `${lane} lane`, width, `${l.slots.length} slots`)}</Box>)
    for (const slot of l.slots) {
      const c = slot.claim
      out.push(
        <Text key={`slot-${lane}-${slot.slot}`} color={slot.stale ? C.warn : c ? C.info : undefined} dimColor={!c}>
          {columns(
            [
              [`slot ${slot.slot}`, 7],
              [c ? (slot.stale ? 'STALE' : c.step) : 'free', 7],
              [c ? ago(c.started, now) : '', 5],
              [c ? `${c.agent} · ${c.worktree} · ${c.preset}` : '', 0],
            ],
            width,
          )}
        </Text>,
      )
    }
    const waiting = l.queue.filter(t => !t.stale)
    out.push(
      <Text key={`queue-${lane}`} dimColor={!waiting.length} color={waiting.length ? C.warn : undefined}>
        {fit(
          waiting.length
            ? `queue: ${waiting.map(t => `${t.position}. ${t.agent} (${t.worktree})${t.priority ? ' priority' : ''}`).join(', ')}`
            : 'queue: empty',
          width,
        ).trimEnd()}
      </Text>,
    )
  }
  const r = s.results
  out.push(<Box key="rule-results">{Rule(el, 'Detached builds', width)}</Box>)
  if (r.error && !r.results) out.push(NotRead(el, 'Detached builds', r.error, width))
  const results = r.results ?? []
  if (!r.error && results.length === 0) out.push(<Text key="no-results" dimColor>None recorded in the worktrees.</Text>)
  for (const x of results) {
    const color = x.state === 'running' ? C.info : x.state === 'lost' ? C.bad : x.exit_code ? C.bad : C.good
    const end = x.state === 'done' ? `exit ${x.exit_code ?? '?'}, ended ${ago(x.ended, now)} ago` : x.state
    out.push(
      <Text key={`result-${x.worktree}`} color={color} dimColor={x.state === 'done' && !x.recent}>
        {columns([[x.worktree, Math.max(10, Math.floor(width / 3))], [`${x.agent ?? '?'} · ${x.preset ?? '?'} · ${end}`, 0]], width)}
      </Text>,
    )
  }
  const cap = Math.max(5, room)
  return (
    <Box flexDirection="column">
      {out.slice(0, cap)}
      {More(el, cap, out.length)}
    </Box>
  )
}

function Tracks(act: Act, el: El, s: CoordStatus, width: number, room: number, now: number) {
  const { Box, Text } = el
  const t = s.tracks
  if (t.error && !t.tracks) return NotRead(el, 'Tracks', t.error, width)
  const tracks = t.tracks ?? []
  const cap = Math.max(3, room - 2)
  const nameW = Math.max(10, width - 2 - 9 - 6 - 6)
  return (
    <Box flexDirection="column">
      <Text dimColor>
        {fit(`${tracks.length} tracks: ${tracks.filter(x => x.busy).length} busy · STATUS.md changed, newest first`, width).trimEnd()}
      </Text>
      {tracks.length > 0 && <Text dimColor>{columns([['  BRANCH', nameW + 2], ['WORKTREE', 9], ['BUSY', 6], ['CHANGED', 0]], width)}</Text>}
      {tracks.slice(0, cap).map(x =>
        Row(
          el,
          `track-${x.branch}`,
          'track',
          '›',
          () => act.fill(`${x.status} `),
          <Text color={x.busy ? C.info : undefined} dimColor={!x.worktree}>
            {columns(
              [
                [x.branch, nameW],
                [x.worktree ? 'yes' : 'gone', 9],
                [x.busy ? 'yes' : '-', 6],
                [`${ago(x.changed, now)} (${clock(x.changed)})`, 0],
              ],
              width - 2,
            )}
          </Text>,
        ),
      )}
      {More(el, cap, tracks.length)}
    </Box>
  )
}
