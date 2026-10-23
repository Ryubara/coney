// The pane: units and issues with rows that open, and every held claim with a live countdown. Draws; never decides,
// and writes nothing but which row is open.
import type { EngineInterface, RenderInputOf } from 'claude-code'

import type { ClaimRow, Held, Snapshot, StepState, Unit } from '../types'
import { claimExpiry, claimTone, elapsed, mark, markTone, showUtc, unitTitle, visibleUnits } from './format'
import type { MarkKind, Surface, Tone } from './format'
import { getHeld, getNow, getOpen, toggleOpen } from './state'

export const PANE = 'conclave'

type PaneInput = RenderInputOf<'Pane', Surface>

const COLORS: Record<Tone, string | undefined> = {
  normal: undefined,
  ok: 'success',
  warn: 'warning',
  bad: 'error',
  dim: undefined,
}

const STEP_MARKS: Record<StepState, string> = { pass: '✔', fail: '✖', none: '–', missing: '·' }

/** The row key a unit opens under. */
export const unitKey = (unit: Unit): string => `task:${unit.task}`
/** The row key an issue row (a claim with no unit here) opens under. */
export const claimKey = (claim: ClaimRow): string => `#${claim.issue}`

/** The claim of the unit's first issue, if the GitHub read has it. */
function claimOf(snapshot: Snapshot, unit: Unit): ClaimRow | undefined {
  if (snapshot.claims === null || snapshot.claims.error !== null) return undefined
  return snapshot.claims.claims.find(claim => unit.issues.includes(claim.issue))
}

/** What the right-hand part of a collapsed unit row says: who, the stage, the build, the age. */
function unitSummary(unit: Unit, nowMs: number): string {
  const who = unit.agent?.id ?? unit.claimant ?? '?'
  const stage = unit.error !== null ? 'task file unreadable' : `${unit.stage ?? '?'}${unit.failed !== null ? ' failed' : ''}`
  const build =
    unit.build === null
      ? ''
      : unit.build.state === 'running'
        ? `build: slot ${unit.build.slot ?? '?'} ${unit.build.since ? elapsed(unit.build.since, nowMs) : ''}`.trimEnd()
        : `build: queue ${unit.build.position ?? '?'}`
  const age = unit.claimed_at === null ? '' : elapsed(unit.claimed_at, nowMs)
  const pr = unit.pr === null ? '' : unit.pr.state === 'merged' ? `merged ${unit.pr.merged_at ? elapsed(unit.pr.merged_at, nowMs) : ''} ago` : `PR #${unit.pr.number}`
  return [who, stage, build, pr, age].filter(Boolean).join('  ')
}

function ids(unit: Unit): string {
  return unit.issues.map(number => `#${number}`).join(' ')
}

/** Why a row is red: the unit's own failure, its pull request's, or its unreadable task file. */
function reason(unit: Unit): string | null {
  if (unit.error !== null) return unit.error
  if (unit.failed !== null) return `step ${unit.failed} failed`
  if (unit.pr !== null && (unit.pr.causes.length > 0 || unit.pr.ci === 'fail')) {
    return `PR #${unit.pr.number}: ${unit.pr.causes.length > 0 ? unit.pr.causes.join(', ') : 'checks failed'}`
  }
  return null
}

export async function drawPane($: EngineInterface, e: PaneInput) {
  const state: Held = await getHeld($)
  const nowMs = Math.max(await getNow($), state.localAt)
  const openRow = await getOpen($)
  const { Box, Button, Text } = $.ui.resolve(e)
  const snapshot = state.snapshot
  if (snapshot === null) {
    return (
      <Box flexDirection="column">
        <Text dimColor>{state.error === null ? 'Reading the first snapshot...' : `No snapshot yet: ${state.error}`}</Text>
      </Box>
    )
  }
  if (e.props.bodyColumns < snapshot.config.pane_min_columns) {
    return (
      <Text dimColor>
        The Conclave pane needs about {snapshot.config.pane_min_columns} columns; the band and the status line show the
        summary.
      </Text>
    )
  }
  const surface = e.surface
  const toggle = (key: string) => toggleOpen($, key)
  const line = (text: string, tone: Tone = 'normal', key?: string) => (
    <Text key={key} color={COLORS[tone]} dimColor={tone === 'dim'}>
      {text}
    </Text>
  )

  const units = visibleUnits(snapshot, nowMs)
  const claims = snapshot.claims !== null && snapshot.claims.error === null ? snapshot.claims : null
  const account = claims?.account ?? null
  const taken = new Set(units.flatMap(row => row.unit.issues))
  // An issue the signed-in account holds and has no unit here for (a claim still settling, or made elsewhere).
  const mine = claims === null ? [] : claims.claims.filter(claim => claim.claimant === account && !taken.has(claim.issue))
  const fresh = state.localAt === 0 ? 'no read' : `${elapsed(new Date(state.localAt).toISOString(), nowMs)} ago`

  const unitRow = (row: { unit: Unit; kind: MarkKind }) => {
    const { unit, kind } = row
    const key = unitKey(unit)
    const isOpen = openRow === key
    const claim = claimOf(snapshot, unit)
    const why = reason(unit)
    return (
      <Box key={key} flexDirection="column">
        <Button key={key} onPress={() => toggle(key)}>
          <Text color={COLORS[markTone(kind)]}>{`${isOpen ? '▾' : '▸'} ${mark(kind, surface)} `}</Text>
          <Text bold>{`${ids(unit)} `}</Text>
          <Text>{`${unitTitle(snapshot, unit)}  `}</Text>
          <Text dimColor>{unitSummary(unit, nowMs)}</Text>
        </Button>
        {isOpen && (
          <Box flexDirection="column" marginLeft={4}>
            {line(`claimant  ${unit.claimant ?? '?'}${unit.agent ? `  (${unit.agent.type} ${unit.agent.id})` : ''}`)}
            {line(
              `status    ${unit.stage ?? '?'}${unit.stage_since ? `, ${elapsed(unit.stage_since, nowMs)} in stage` : ''}${unit.busy ? ', busy' : ''}`,
            )}
            {claim !== undefined &&
              line(
                `expiry    ${claimExpiry(claim, claims?.rules ?? { warn_hours: 0, deadline_hours: 0, enforced: false }, nowMs)}${claim.deadline ? ` (${showUtc(claim.deadline)})` : ''}`,
                claims ? claimTone(claim, claims.rules, snapshot.config.claim_red_hours, nowMs) : 'normal',
              )}
            {line(
              `scope     ${unit.scope.join(', ') || 'not stated'}${unit.overlaps.length > 0 ? `   ⚠ overlaps ${unit.overlaps.map(o => o.with).join(', ')}` : ''}`,
              unit.overlaps.length > 0 ? 'warn' : 'normal',
            )}
            {line(`stamps    ${unit.steps.map(step => `${STEP_MARKS[step.state]} ${step.step}`).join('  ')}`)}
            {unit.stamp_problems > 0 && line(`          ${unit.stamp_problems} problem(s) in the stamp log (coney-tools stamp verify)`, 'warn')}
            {why !== null && line(`reason    ${why}`, 'bad')}
            {unit.last_build !== null &&
              line(
                `last build  ${unit.last_build.state ?? '?'}${unit.last_build.exit_code !== null ? ` exit ${unit.last_build.exit_code}` : ''}${unit.last_build.ended ? `, ${elapsed(unit.last_build.ended, nowMs)} ago` : ''}`,
              )}
            {line(
              `links     ${[...unit.issues.map(n => `issue #${n}`), unit.pr ? `PR #${unit.pr.number}` : '', unit.worktree ?? ''].filter(Boolean).join(' · ')}`,
              'dim',
            )}
          </Box>
        )}
      </Box>
    )
  }

  const issueRow = (claim: ClaimRow) => {
    const key = claimKey(claim)
    const isOpen = openRow === key
    const rules = claims?.rules ?? { warn_hours: 0, deadline_hours: 0, enforced: false }
    const tone = claimTone(claim, rules, snapshot.config.claim_red_hours, nowMs)
    return (
      <Box key={key} flexDirection="column">
        <Button key={key} onPress={() => toggle(key)}>
          <Text color={COLORS.warn}>{`${isOpen ? '▾' : '▸'} ${mark('pending', surface)} `}</Text>
          <Text bold>{`#${claim.issue} `}</Text>
          <Text>{`${claim.title ?? ''}  `}</Text>
          <Text dimColor>{`${claim.claimant}  ${claim.stage ?? claim.status}  `}</Text>
          <Text color={COLORS[tone]}>{claimExpiry(claim, rules, nowMs)}</Text>
        </Button>
        {isOpen && (
          <Box flexDirection="column" marginLeft={4}>
            {line(`status    ${claim.status}${claim.stage ? ` (${claim.stage})` : ''}, last activity ${elapsed(claim.last_activity, nowMs)} ago`)}
            {line(`scope     ${claim.scope.join(', ') || 'not stated'}`)}
            {claim.task !== null && line(`task      ${claim.task}`)}
            {claim.pr !== null && line(`links     PR #${claim.pr}`, 'dim')}
          </Box>
        )}
      </Box>
    )
  }

  return (
    <Box flexDirection="column">
      <Text dimColor>
        {`${snapshot.github ? 'with GitHub' : 'local only'} · read ${fresh}${state.error ? ` · local read failed: ${state.error}` : ''}${state.githubError ? ` · GitHub read failed: ${state.githubError}` : ''}`}
      </Text>
      <Text bold>{`UNITS AND ISSUES${account ? ` (mine: ${account})` : ''}`}</Text>
      {snapshot.units.error !== null && line(`units not read: ${snapshot.units.error}`, 'bad')}
      {units.length === 0 && mine.length === 0 && snapshot.units.error === null && line('No unit or claim of yours.', 'dim')}
      {units.map(unitRow)}
      {mine.map(issueRow)}
      <Text> </Text>
      <Text bold>CLAIMS (all contributors)</Text>
      {snapshot.claims === null && line('Waiting for the first GitHub read.', 'dim')}
      {snapshot.claims !== null && snapshot.claims.error !== null && line(`claims not read: ${snapshot.claims.error}`, 'bad')}
      {claims !== null && claims.claims.length === 0 && line('No held claims.', 'dim')}
      {claims !== null &&
        claims.claims.map(claim => {
          const tone = claimTone(claim, claims.rules, snapshot.config.claim_red_hours, nowMs)
          const text = claim.deadline === null ? claimExpiry(claim, claims.rules, nowMs) : `${claimExpiry(claim, claims.rules, nowMs)}${claim.warned ? '  (warned)' : ''}`
          return line(`  #${claim.issue}  ${claim.claimant}  ${claim.stage ?? claim.status}  ${text}`, tone, `claim-${claim.issue}`)
        })}
    </Box>
  )
}
