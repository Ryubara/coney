// SPDX-License-Identifier: GPL-3.0-or-later
// The pane: units and issues with rows that open, and every held claim with a live countdown. Draws; never decides,
// and writes nothing but which row is open. Three layouts from one set of fields, picked by the widths the snapshot
// carries (format.ts `paneLayout`): the wide one puts a row on one line (from `pane_wide_columns`), the compact one gives
// the key fields a first line and the rest a dim second one (from `pane_compact_columns`), and the narrow one keeps only
// the issue and the stage on the first line and abbreviates the claims (from `pane_min_columns`). Every line of the two
// shorter ones is clipped to the pane with an ellipsis; under the narrow one the pane is one clipped summary line. The
// words come from format.ts, which the band and the status line use too.
import type { EngineInterface, RenderInputOf } from 'claude-code'

import type { ClaimRow, Held, Snapshot, StepState, Unit } from '../types'
import {
  bandItems,
  buildText,
  claimExpiry,
  claimExpiryTiny,
  claimLineText,
  claimTone,
  clip,
  color,
  elapsed,
  issueIds,
  mark,
  markTone,
  packLines,
  paneLayout,
  prText,
  showUtc,
  stageText,
  unitTitle,
  visibleUnits,
  wrapTokens,
} from './format'
import type { BandItem, MarkKind, Surface, Tone } from './format'
import { getHeld, getNow, getOpen, toggleOpen } from './state'

type PaneInput = RenderInputOf<'Pane', Surface>

/** The mark of a step of the stamp log in the pane. */
function stepMark(state: StepState): string {
  switch (state) {
    case 'pass':
      return '✔'
    case 'fail':
      return '✖'
    case 'none':
      return '–'
    case 'missing':
      return '·'
  }
}

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

/** The tone of a line that stands for several band items: the worst of theirs. */
function worstTone(items: readonly BandItem[]): Tone {
  const tones = items.map(item => item.tone)
  return tones.includes('bad') ? 'bad' : tones.includes('warn') ? 'warn' : 'normal'
}

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
  // One column spare, so a line that fills the pane never wraps by the terminal's own rule (and never less than one).
  const room = Math.max(1, e.props.bodyColumns - 1)
  if (snapshot === null) {
    return (
      <Box flexDirection="column">
        <Text dimColor>
          {clip(state.error === null ? 'Reading the first snapshot...' : `No snapshot yet: ${state.error}`, room)}
        </Text>
      </Box>
    )
  }
  const layout = paneLayout(e.props.bodyColumns, snapshot.config)
  if (layout === 'line') {
    // Too narrow for rows: the band's items on one line, clipped, in the colour of the worst of them.
    const items = bandItems(state, nowMs)
    const text = items.length === 0 ? 'Conclave: nothing to count' : items.map(item => item.text).join(' · ')
    return (
      <Box flexDirection="column">
        <Text color={color(worstTone(items))}>{clip(text, room)}</Text>
      </Box>
    )
  }
  const isCompact = layout !== 'wide'
  const isNarrow = layout === 'narrow'
  // How far the details of an open row sit in, and the width of their label column (`expiry` and the rest).
  const indent = isNarrow ? 1 : isCompact ? 2 : 4
  const LABEL = isNarrow ? 9 : 10
  const surface = e.surface
  const toggle = (key: string) => toggleOpen($, key)
  const line = (text: string, tone: Tone = 'normal', key?: string) => (
    <Text key={key} color={color(tone)} dimColor={tone === 'dim'}>
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
        {detail('stamps', unit.steps.map(step => `${stepMark(step.state)} ${step.step}`), '  ')}
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
      // Compact: line 1 the mark, the issue, the stage and the pull request; line 2, dim, the title, who, the build.
      // Narrow: line 1 only the issue (the task when it names none) and the stage; line 2 the pull request, who, the
      // build and last the title, the first thing a clip cuts.
      const who = unit.agent?.id ?? unit.claimant ?? '?'
      const head = isNarrow
        ? `${issueIds(unit) || unit.task} ${stageText(unit)}`
        : [`${issueIds(unit)} ${stageText(unit)}`, prText(unit, nowMs)].filter(Boolean).join('  ')
      const rest = isNarrow
        ? [prText(unit, nowMs), who, buildText(unit, nowMs), unitTitle(snapshot, unit)]
        : [unitTitle(snapshot, unit), who, buildText(unit, nowMs)]
      return (
        <Box key={key} flexDirection="column">
          <Button key={key} onPress={() => toggle(key)}>
            <Text color={color(markTone(kind))}>{caret}</Text>
            <Text bold>{clip(head, room - 4)}</Text>
          </Button>
          {line(clip(`${isNarrow ? '  ' : '   '}${rest.filter(Boolean).join(' · ')}`, room), 'dim')}
          {open}
        </Box>
      )
    }
    return (
      <Box key={key} flexDirection="column">
        <Button key={key} onPress={() => toggle(key)}>
          <Text color={color(markTone(kind))}>{caret}</Text>
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
      // The narrow row has the bare countdown, and its second line puts the title last, where a clip cuts first.
      const expiry = isNarrow ? claimExpiryTiny(claim, rules, nowMs) : claimExpiry(claim, rules, nowMs, true)
      const rest = isNarrow
        ? [claim.claimant, claim.stage ?? claim.status, claim.title ?? '']
        : [claim.title ?? '', claim.claimant, claim.stage ?? claim.status]
      return (
        <Box key={key} flexDirection="column">
          <Button key={key} onPress={() => toggle(key)}>
            <Text color={color('warn')}>{caret}</Text>
            <Text bold color={color(tone)}>
              {clip(`#${claim.issue} ${expiry}`, room - 4)}
            </Text>
          </Button>
          {line(clip(`${isNarrow ? '  ' : '   '}${rest.filter(Boolean).join(' · ')}`, room), 'dim')}
          {open}
        </Box>
      )
    }
    return (
      <Box key={key} flexDirection="column">
        <Button key={key} onPress={() => toggle(key)}>
          <Text color={color('warn')}>{caret}</Text>
          <Text bold>{`#${claim.issue} `}</Text>
          <Text>{`${claim.title ?? ''}  `}</Text>
          <Text dimColor>{`${claim.claimant}  ${claim.stage ?? claim.status}  `}</Text>
          <Text color={color(tone)}>{claimExpiry(claim, rules, nowMs)}</Text>
        </Button>
        {open}
      </Box>
    )
  }

  // One held claim's line in the claims list: the words are format.ts's, and the shorter ones are clipped to the pane.
  const claimLine = (claim: ClaimRow) => {
    const tone = claimTone(claim, rules, redHours, nowMs)
    const text = claimLineText(claim, rules, nowMs, layout)
    return line(isCompact ? clip(text, room) : text, tone, `claim-${claim.issue}`)
  }

  // The compact and narrow panes open with the band's own items, in the band's own colours, so what the band counts is what the
  // rows below list. They pack into lines by their text; each is drawn on its own, with a dim separator between.
  const items = isCompact ? bandItems(state, nowMs) : []
  let from = 0
  const summary = (items.length === 0 ? [] : packLines(items.map(item => item.text), ' · ', room)).map(texts => {
    from += texts.length
    return items.slice(from - texts.length, from)
  })
  const summaryLine = (row: BandItem[], index: number) => (
    <Box key={`summary-${index}`}>
      {row.flatMap((item, at) => [
        ...(at > 0 ? [<Text key={`sep-${item.key}`} dimColor>{' · '}</Text>] : []),
        <Text key={item.key} color={color(item.tone)} dimColor={item.tone === 'dim'}>
          {clip(item.text, room)}
        </Text>,
      ])}
    </Box>
  )
  // A line of the compact or narrow pane that may be long (an account, an error) is cut to the pane; the wide pane lets
  // it wrap.
  const fit = (text: string) => (isCompact ? clip(text, room) : text)
  const readLine = `${snapshot.github ? 'with GitHub' : 'local only'} · read ${fresh}${state.error ? ` · local read failed: ${state.error}` : ''}${state.githubError ? ` · GitHub read failed: ${state.githubError}` : ''}`
  return (
    <Box flexDirection="column">
      {summary.map(summaryLine)}
      <Text dimColor>{fit(readLine)}</Text>
      <Text bold>{fit(`UNITS AND ISSUES${account ? ` (mine: ${account})` : ''}`)}</Text>
      {snapshot.units.error !== null && line(fit(`units not read: ${snapshot.units.error}`), 'bad')}
      {units.length === 0 && mine.length === 0 && snapshot.units.error === null && line(fit('No unit or claim of yours.'), 'dim')}
      {units.map(unitRow)}
      {mine.map(issueRow)}
      <Text> </Text>
      <Text bold>{fit('CLAIMS (all contributors)')}</Text>
      {snapshot.claims === null && line(fit('Waiting for the first GitHub read.'), 'dim')}
      {snapshot.claims !== null && snapshot.claims.error !== null && line(fit(`claims not read: ${snapshot.claims.error}`), 'bad')}
      {claims !== null && claims.claims.length === 0 && line(fit('No held claims.'), 'dim')}
      {claims !== null && claims.claims.map(claimLine)}
    </Box>
  )
}
