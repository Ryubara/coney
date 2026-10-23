// SPDX-License-Identifier: GPL-3.0-or-later
// The pane: units and issues with rows that open, and every held claim with a live countdown. Draws; never decides,
// and writes nothing but which row is open. Two layouts from one set of fields: the wide one puts a row on one line
// (from `pane_wide_columns`), the compact one gives the key fields a first line and wraps the rest underneath, down to
// `pane_min_columns`; the words come from format.ts, which the band and the status line use too.
import type { EngineInterface, RenderInputOf } from 'claude-code'

import type { ClaimRow, Held, Snapshot, StepState, Unit } from '../types'
import {
  bandItems,
  buildText,
  claimExpiry,
  claimTone,
  clip,
  COLORS,
  elapsed,
  issueIds,
  mark,
  markTone,
  prText,
  showUtc,
  stageText,
  unitTitle,
  visibleUnits,
  wrapTokens,
} from './format'
import type { MarkKind, Surface, Tone } from './format'
import { getHeld, getNow, getOpen, toggleOpen } from './state'

type PaneInput = RenderInputOf<'Pane', Surface>

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

/** Why a row is red: the unit's own failure, its pull request's, or its unreadable task file. */
function reason(unit: Unit): string | null {
  if (unit.error !== null) return unit.error
  if (unit.failed !== null) return `step ${unit.failed} failed`
  if (unit.pr !== null && (unit.pr.causes.length > 0 || unit.pr.ci === 'fail')) {
    return `PR #${unit.pr.number}: ${unit.pr.causes.length > 0 ? unit.pr.causes.join(', ') : 'checks failed'}`
  }
  return null
}

const NO_RULES = { warn_hours: 0, deadline_hours: 0, enforced: false }
/** The width of a detail's label column (`expiry` and the rest) in an open row. */
const LABEL = 10

/** What a collapsed unit row says after its title in the wide layout: who, the stage, the build, the pull request, the age. */
function unitSummary(unit: Unit, nowMs: number): string {
  const who = unit.agent?.id ?? unit.claimant ?? '?'
  const age = unit.claimed_at === null ? '' : elapsed(unit.claimed_at, nowMs)
  return [who, stageText(unit), buildText(unit, nowMs), prText(unit, nowMs), age].filter(Boolean).join('  ')
}

/** The pane's tree: units and issues with rows that open, then every held claim with its countdown. */
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
  const { pane_min_columns: minColumns, pane_wide_columns: wideColumns } = snapshot.config
  if (e.props.bodyColumns < minColumns) {
    return (
      <Text dimColor>
        {`The Conclave pane needs about ${minColumns} columns; the band and the status line show the summary.`}
      </Text>
    )
  }
  const isCompact = e.props.bodyColumns < wideColumns
  // One column spare, so a line that fills the pane never wraps by the terminal's own rule.
  const room = e.props.bodyColumns - 1
  // How far the details of an open row sit in.
  const indent = isCompact ? 2 : 4
  const surface = e.surface
  const toggle = (key: string) => toggleOpen($, key)
  const line = (text: string, tone: Tone = 'normal', key?: string) => (
    <Text key={key} color={COLORS[tone]} dimColor={tone === 'dim'}>
      {text}
    </Text>
  )
  // One labelled detail of an open row, wrapped to the pane: continuation lines sit under the value.
  const detail = (label: string, tokens: string[], sep = ' ', tone: Tone = 'normal') =>
    wrapTokens(sep === ' ' ? tokens.flatMap(token => token.split(' ')).filter(Boolean) : tokens, sep, room - indent - LABEL).map((chunk, index) =>
      line(`${index === 0 ? label.padEnd(LABEL) : ' '.repeat(LABEL)}${chunk}`, tone, `${label}-${index}`),
    )

  const units = visibleUnits(snapshot, nowMs)
  const claims = snapshot.claims !== null && snapshot.claims.error === null ? snapshot.claims : null
  const account = claims?.account ?? null
  const taken = new Set(units.flatMap(row => row.unit.issues))
  // An issue the signed-in account holds and has no unit here for (a claim still settling, or made elsewhere).
  const mine = claims === null ? [] : claims.claims.filter(claim => claim.claimant === account && !taken.has(claim.issue))
  const fresh = state.localAt === 0 ? 'no read' : `${elapsed(new Date(state.localAt).toISOString(), nowMs)} ago`
  const rules = claims?.rules ?? NO_RULES
  const redHours = snapshot.config.claim_red_hours

  // The lines under an open unit row.
  const unitDetails = (unit: Unit, claim: ClaimRow | undefined) => {
    const why = reason(unit)
    const expiry =
      claim === undefined
        ? ''
        : `${claimExpiry(claim, rules, nowMs)}${claim.deadline ? ` (${showUtc(claim.deadline)})` : ''}`
    const last = unit.last_build
    const overlaps = unit.overlaps.length > 0 ? `   ⚠ overlaps ${unit.overlaps.map(o => o.with).join(', ')}` : ''
    const links = [...unit.issues.map(n => `issue #${n}`), unit.pr ? `PR #${unit.pr.number}` : '', unit.worktree ?? '']
    return (
      <Box flexDirection="column" marginLeft={indent}>
        {detail('claimant', [`${unit.claimant ?? '?'}${unit.agent ? ` (${unit.agent.type} ${unit.agent.id})` : ''}`])}
        {detail('status', [
          `${stageText(unit)}${unit.stage_since ? `, ${elapsed(unit.stage_since, nowMs)} in stage` : ''}${unit.busy ? ', busy' : ''}`,
        ])}
        {claim !== undefined && detail('expiry', [expiry], ' ', claimTone(claim, rules, redHours, nowMs))}
        {detail('scope', [`${unit.scope.join(', ') || 'not stated'}${overlaps}`], ' ', overlaps ? 'warn' : 'normal')}
        {detail('stamps', unit.steps.map(step => `${STEP_MARKS[step.state]} ${step.step}`), '  ')}
        {unit.stamp_problems > 0 &&
          detail('', [`${unit.stamp_problems} problem(s) in the stamp log (coney-tools stamp verify)`], ' ', 'warn')}
        {why !== null && detail('reason', [why], ' ', 'bad')}
        {last !== null &&
          detail('build', [
            `last ${last.state ?? '?'}${last.exit_code !== null ? ` exit ${last.exit_code}` : ''}${last.ended ? `, ${elapsed(last.ended, nowMs)} ago` : ''}`,
          ])}
        {detail('links', links.filter(Boolean), ' · ', 'dim')}
      </Box>
    )
  }

  const unitRow = (row: { unit: Unit; kind: MarkKind }) => {
    const { unit, kind } = row
    const key = unitKey(unit)
    const isOpen = openRow === key
    const caret = `${isOpen ? '▾' : '▸'} ${mark(kind, surface)} `
    const open = isOpen && unitDetails(unit, claimOf(snapshot, unit))
    if (isCompact) {
      // Line 1: the mark, the issue, the stage and the pull request. Line 2, dim: the title, who, the build.
      const head = [`${issueIds(unit)} ${stageText(unit)}`, prText(unit, nowMs)].filter(Boolean).join('  ')
      const rest = [unitTitle(snapshot, unit), unit.agent?.id ?? unit.claimant ?? '?', buildText(unit, nowMs)]
      return (
        <Box key={key} flexDirection="column">
          <Button key={key} onPress={() => toggle(key)}>
            <Text color={COLORS[markTone(kind)]}>{caret}</Text>
            <Text bold>{clip(head, room - 4)}</Text>
          </Button>
          {line(clip(`   ${rest.filter(Boolean).join(' · ')}`, room), 'dim')}
          {open}
        </Box>
      )
    }
    return (
      <Box key={key} flexDirection="column">
        <Button key={key} onPress={() => toggle(key)}>
          <Text color={COLORS[markTone(kind)]}>{caret}</Text>
          <Text bold>{`${issueIds(unit)} `}</Text>
          <Text>{`${unitTitle(snapshot, unit)}  `}</Text>
          <Text dimColor>{unitSummary(unit, nowMs)}</Text>
        </Button>
        {open}
      </Box>
    )
  }

  const issueRow = (claim: ClaimRow) => {
    const key = claimKey(claim)
    const isOpen = openRow === key
    const tone = claimTone(claim, rules, redHours, nowMs)
    const caret = `${isOpen ? '▾' : '▸'} ${mark('pending', surface)} `
    const open = isOpen && (
      <Box flexDirection="column" marginLeft={indent}>
        {detail('status', [
          `${claim.status}${claim.stage ? ` (${claim.stage})` : ''}, last activity ${elapsed(claim.last_activity, nowMs)} ago`,
        ])}
        {detail('scope', [claim.scope.join(', ') || 'not stated'])}
        {claim.task !== null && detail('task', [claim.task])}
        {claim.pr !== null && detail('links', [`PR #${claim.pr}`], ' · ', 'dim')}
      </Box>
    )
    if (isCompact) {
      const rest = [claim.title ?? '', claim.claimant, claim.stage ?? claim.status]
      return (
        <Box key={key} flexDirection="column">
          <Button key={key} onPress={() => toggle(key)}>
            <Text color={COLORS.warn}>{caret}</Text>
            <Text bold color={COLORS[tone]}>
              {clip(`#${claim.issue} ${claimExpiry(claim, rules, nowMs, true)}`, room - 4)}
            </Text>
          </Button>
          {line(clip(`   ${rest.filter(Boolean).join(' · ')}`, room), 'dim')}
          {open}
        </Box>
      )
    }
    return (
      <Box key={key} flexDirection="column">
        <Button key={key} onPress={() => toggle(key)}>
          <Text color={COLORS.warn}>{caret}</Text>
          <Text bold>{`#${claim.issue} `}</Text>
          <Text>{`${claim.title ?? ''}  `}</Text>
          <Text dimColor>{`${claim.claimant}  ${claim.stage ?? claim.status}  `}</Text>
          <Text color={COLORS[tone]}>{claimExpiry(claim, rules, nowMs)}</Text>
        </Button>
        {open}
      </Box>
    )
  }

  // One held claim's line in the claims list: the compact one leads with the countdown.
  const claimLine = (claim: ClaimRow) => {
    const tone = claimTone(claim, rules, redHours, nowMs)
    const key = `claim-${claim.issue}`
    if (isCompact) {
      const text = `  #${claim.issue} ${claimExpiry(claim, rules, nowMs, true)}  ${claim.claimant}${claim.warned ? ' (warned)' : ''}`
      return line(clip(text, room), tone, key)
    }
    const expiry = claimExpiry(claim, rules, nowMs)
    const warned = claim.deadline !== null && claim.warned ? '  (warned)' : ''
    return line(`  #${claim.issue}  ${claim.claimant}  ${claim.stage ?? claim.status}  ${expiry}${warned}`, tone, key)
  }

  // The compact pane opens with the band's own items, so what the band counts is what the rows below list.
  const summary = isCompact ? wrapTokens(bandItems(state, nowMs).map(item => item.text), ' · ', room) : []
  const readLine = `${snapshot.github ? 'with GitHub' : 'local only'} · read ${fresh}${state.error ? ` · local read failed: ${state.error}` : ''}${state.githubError ? ` · GitHub read failed: ${state.githubError}` : ''}`
  return (
    <Box flexDirection="column">
      {summary.map((text, index) => line(text, 'normal', `summary-${index}`))}
      <Text dimColor>{isCompact ? clip(readLine, room) : readLine}</Text>
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
      {claims !== null && claims.claims.map(claimLine)}
    </Box>
  )
}
