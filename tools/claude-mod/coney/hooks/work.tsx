// The Work tab: what waits for the coordinator (the action ids), the open issues and pull requests (from the last
// --github read) and the release queue. Ids, labels, states, counts and times only: no issue or pull request title.
import type { CoordStatus, View, Watch } from '../types'
import { ago, clock, GITHUB_KINDS, OWNED_ELSEWHERE } from './data'
import { C, columns, fit, More, NotRead, Row, SubTabs, tip } from './kit'
import type { Act, El } from './kit'

export const WORK_SUBS: [string, string][] = [
  ['waiting', 'Waiting'],
  ['issues', 'Issues'],
  ['prs', 'Pull requests'],
  ['release', 'Release'],
]

/** Issue statuses shown without "all": the ones that want the coordinator's eye. */
const TRIAGE = new Set(['S-needs-triage', null])

const STATUS_COLOR: Record<string, string | undefined> = {
  'S-needs-triage': C.warn,
  'S-in-progress': C.info,
  'S-pending-review': C.info,
  'S-blocked': C.bad,
  'S-conflict': C.bad,
  'S-failing': C.bad,
}

export function WorkTab(act: Act, el: El, s: CoordStatus, w: Watch, v: View, width: number, rows: number, now: number) {
  const { Box, Text } = el
  const sub = WORK_SUBS.some(([k]) => k === v.workSub) ? v.workSub : 'waiting'
  const set = (workSub: string) => () => act.edit(x => ({ ...x, workSub }))
  const room = Math.max(5, rows - 5)
  let body
  if (sub === 'issues') body = Issues(act, el, s, v, width, room, now)
  else if (sub === 'prs') body = Prs(el, s, width, room, now)
  else if (sub === 'release') body = Release(act, el, s, width, room, now)
  else body = Waiting(act, el, s, width, room)
  return (
    <Box flexDirection="column">
      {SubTabs(
        el,
        WORK_SUBS.map(([key, label]) => ({ key, label, active: sub === key, onPress: set(key) })),
      )}
      <Box marginTop={1} flexDirection="column">
        {body}
        <Text dimColor>
          read {clock(w.readAt)} UTC · GitHub {w.githubAt ? `${clock(w.githubAt)} UTC` : 'not read yet'}
        </Text>
        {w.githubFailure && <Text color={C.bad}>{fit(`Last GitHub read failed: ${w.githubFailure}`, width).trimEnd()}</Text>}
      </Box>
    </Box>
  )
}

function Waiting(act: Act, el: El, s: CoordStatus, width: number, room: number) {
  const { Box, Text } = el
  const ids = s.action
  const cap = Math.max(3, room - 2)
  const owned = (id: string) => OWNED_ELSEWHERE.some(p => id.startsWith(p))
  const github = !s.issues
  return (
    <Box flexDirection="column">
      {ids.length === 0 && <Text dimColor>Nothing waits for the coordinator.</Text>}
      {github && (
        <Text dimColor>
          {fit(`GitHub is read every two minutes; ${GITHUB_KINDS.join(', ')} ids come with it.`, width).trimEnd()}
        </Text>
      )}
      {ids.slice(0, cap).map(id =>
        Row(el, `id-${id}`, 'id', '›', () => act.fill(`${id} `), [
          <Text color={owned(id) ? undefined : id.startsWith('build:') ? C.good : C.warn} dimColor={owned(id)}>
            {fit(id, Math.max(8, width - 14)).trimEnd()}
          </Text>,
          <Text dimColor>{owned(id) ? '  (Tether)' : ''}</Text>,
        ]),
      )}
      {More(el, cap, ids.length)}
    </Box>
  )
}

function Issues(act: Act, el: El, s: CoordStatus, v: View, width: number, room: number, now: number) {
  const { Box, Button, Text } = el
  const g = s.issues
  if (!g) return <Text dimColor>Issues are read every two minutes; not read yet.</Text>
  if (g.error && !g.issues) return NotRead(el, 'Issues', g.error, width)
  const all = g.issues ?? []
  const list = v.allIssues ? all : all.filter(i => TRIAGE.has(i.status))
  const counts = Object.entries(g.counts ?? {})
    .map(([k, n]) => `${n} ${k}`)
    .join(' · ')
  const cap = Math.max(3, room - 4)
  const labelW = Math.max(6, width - 6 - 18 - 9 - 14 - 5 - 5)
  return (
    <Box flexDirection="column">
      <Text dimColor>{fit(`${all.length} open: ${counts || 'none'}`, width).trimEnd()}</Text>
      <Box flexDirection="row">
        <Button
          key="all-issues"
          plain
          hover={tip('all-issues')}
          label={v.allIssues ? '[x] every open issue' : '[ ] every open issue'}
          onPress={() => void act.edit(x => ({ ...x, allIssues: !x.allIssues }))}
        />
      </Box>
      {list.length === 0 && <Text dimColor>No issue waits for triage, and none lacks a status.</Text>}
      {list.length > 0 && (
        <Text dimColor>{columns([['#', 6], ['STATUS', 18], ['PRIORITY', 9], ['AREA', 14], ['AGE', 5], ['LABELS', labelW]], width)}</Text>
      )}
      {list.slice(0, cap).map(i => (
        <Text key={`issue-${i.number}`} color={STATUS_COLOR[i.status ?? ''] ?? (i.status ? undefined : C.warn)}>
          {columns(
            [
              [`#${i.number}`, 6],
              [i.status ?? 'no status', 18],
              [i.priority ?? '-', 9],
              [i.area ?? '-', 14],
              [ago(i.created, now), 5],
              [i.labels.join(' '), 0],
            ],
            width,
          )}
        </Text>
      ))}
      {More(el, cap, list.length)}
    </Box>
  )
}

const CI_COLOR = { pass: C.good, pending: C.info, fail: C.bad, none: undefined } as const

function Prs(el: El, s: CoordStatus, width: number, room: number, now: number) {
  const { Box, Text } = el
  const g = s.prs
  if (!g) return <Text dimColor>Pull requests are read every two minutes; not read yet.</Text>
  if (g.error && !g.prs) return NotRead(el, 'Pull requests', g.error, width)
  const prs = g.prs ?? []
  const cap = Math.max(3, room - 1)
  return (
    <Box flexDirection="column">
      {prs.length === 0 && <Text dimColor>No open pull requests.</Text>}
      {prs.length > 0 && (
        <Text dimColor>{columns([['#', 5], ['CHECKS', 7], ['MERGE', 6], ['AUTO', 5], ['AGE', 4], ['BRANCH · PR-HEALTH', 0]], width)}</Text>
      )}
      {prs.slice(0, cap).map(p => (
        <Text key={`pr-${p.number}`}>
          <Text>{fit(`#${p.number}`, 5)} </Text>
          <Text color={CI_COLOR[p.ci]} bold={p.ci === 'fail'}>
            {fit(p.ci, 7)}{' '}
          </Text>
          <Text color={p.mergeable === 'CONFLICTING' ? C.warn : undefined} dimColor={p.mergeable !== 'CONFLICTING'}>
            {fit(p.mergeable === 'MERGEABLE' ? 'ok' : p.mergeable === 'CONFLICTING' ? 'clash' : '?', 6)}{' '}
          </Text>
          <Text dimColor>{fit(p.auto_merge ? 'on' : 'off', 5)} </Text>
          <Text dimColor>{fit(ago(p.updated, now), 4)} </Text>
          <Text dimColor={p.draft} color={p.causes.length ? C.warn : undefined}>
            {fit(
              `${p.branch}${p.draft ? ' (draft)' : ''}${p.causes.length ? ` · ${p.causes.join(', ')}` : ''}`,
              Math.max(8, width - 31),
            ).trimEnd()}
          </Text>
        </Text>
      ))}
      {More(el, cap, prs.length)}
    </Box>
  )
}

function Release(act: Act, el: El, s: CoordStatus, width: number, room: number, now: number) {
  const { Box, Text } = el
  const r = s.release
  if (r.error && !r.units) return NotRead(el, 'The release queue', r.error, width)
  const units = r.units ?? []
  const cap = Math.max(3, room - 2)
  const queued = units.filter(u => u.queued).length
  const stateW = 14
  const nameW = Math.max(10, width - stateW - 7 - 5 - 6)
  return (
    <Box flexDirection="column">
      <Text dimColor>
        {fit(`${queued} queued of ${units.length} unit file${units.length === 1 ? '' : 's'} with a worktree`, width).trimEnd()}
      </Text>
      {units.length > 0 && <Text dimColor>{columns([['  BRANCH / FILE', nameW + 2], ['STATE', stateW], ['PUSHED', 7], ['AGE', 0]], width)}</Text>}
      {units.slice(0, cap).map(u => {
        const state = u.queued === null ? 'unknown yet' : u.queued ? 'queued' : `released #${u.pr ?? '?'}`
        return Row(
          el,
          `unit-${u.branch}/${u.file}`,
          'unit',
          '›',
          () => act.fill(`${u.path} `),
          <Text color={u.queued ? C.warn : undefined} dimColor={u.queued === false}>
            {columns(
              [
                [`${u.branch}/${u.file}`, nameW],
                [state, stateW],
                [u.pushed ? 'yes' : 'no', 7],
                [ago(u.changed, now), 0],
              ],
              width - 2,
            )}
          </Text>,
        )
      })}
      {More(el, cap, units.length)}
    </Box>
  )
}
