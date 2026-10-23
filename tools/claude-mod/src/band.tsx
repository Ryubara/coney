// SPDX-License-Identifier: GPL-3.0-or-later
// The band above the prompt: units, failures, pull requests, claims, builds, critical issues and free disk, each a
// button that opens the pane. Dimmed with `stale Nm` when the last good read is old. Draws; never decides.
import type { EngineInterface, RenderElement, RenderInput } from 'claude-code'

import { bandItems, COLORS, PANE, staleMinutes } from './format'
import type { BandItem } from './format'
import { getHeld, getNow } from './state'

/** The first four keys stay on the first line when the band is narrow (how narrow is `band_narrow_columns`). */
const FIRST_LINE = new Set(['units', 'failed', 'prs', 'claims'])

type BandInput = RenderInput<'AbovePrompt'>

/** Opens the Conclave pane, as the person's press on the band asks. */
export async function openPane($: EngineInterface): Promise<void> {
  await $.ui.open({ id: PANE, title: 'Conclave' })
}

/** The band's tree, or null when there is nothing to show (the engine's own band then stands). */
export async function drawBand($: EngineInterface, e: BandInput): Promise<RenderElement | null> {
  if (e.props.hasSurvey) return null
  const state = await getHeld($)
  const nowMs = Math.max(await getNow($), state.localAt)
  const { Box, Button, Text } = $.ui.resolve(e)
  if (state.newer !== null) {
    return (
      <Box>
        <Text color="warning">Conclave: update the checkout (the snapshot is schema {state.newer})</Text>
      </Box>
    )
  }
  if (state.snapshot === null) {
    if (state.error === null) return null
    return (
      <Box>
        <Text color="warning">Conclave: no snapshot yet ({state.error})</Text>
      </Box>
    )
  }
  const stale = staleMinutes(state, nowMs)
  const items = bandItems(state, nowMs)
  const lines: BandItem[][] =
    e.props.bodyColumns < state.snapshot.config.band_narrow_columns
      ? [items.filter(item => FIRST_LINE.has(item.key)), items.filter(item => !FIRST_LINE.has(item.key))]
      : [items]
  const button = (item: BandItem) => (
    <Button key={item.key} onPress={() => openPane($)}>
      <Text color={COLORS[item.tone]} dimColor={stale !== null || item.tone === 'dim'}>
        {item.text}
      </Text>
    </Button>
  )
  return (
    <Box flexDirection="column">
      {lines
        .filter(line => line.length > 0)
        .map((line, index) => (
          <Box key={`line-${index}`}>
            {index === 0 && (
              <Text bold dimColor={stale !== null}>
                {' '}
                Conclave{' '}
              </Text>
            )}
            {line.map(item => (
              <Box key={`item-${item.key}`} marginRight={2}>
                {button(item)}
              </Box>
            ))}
            {index === 0 && stale !== null && <Text color="warning">stale {stale}m</Text>}
          </Box>
        ))}
    </Box>
  )
}
