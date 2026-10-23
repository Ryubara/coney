// SPDX-License-Identifier: GPL-3.0-or-later
// Conclave: Coney's coordination view. Wiring only: which event calls which file.
//
// Read-only. It draws what `coney-tools conclave snapshot` prints and hides the generic agent types. It wakes nobody,
// guards no stop and adds no tool: session health (the context window, the prompt cache, /clear, HANDOFF, the idle
// wake) stays with Tether, and the one classic hook below only reads the session's agent type. Nothing here waits on
// anything: a session start, Tether's included, never pauses for Conclave (test_conclave_mod.py holds the boundary).
import type { Register } from 'claude-code'

import { drawBand, openPane } from '../src/band'
import { PANE } from '../src/format'
import { applySavedOffer, isOffered } from '../src/offer'
import { drawPane } from '../src/pane'
import { startPolling } from '../src/snapshot'
import { setRole } from '../src/state'
import { showStatus } from '../src/status'

export const register: Register = on => {
  // The session's folder, for the status line (a module variable resets on a reload, and session.start sets it again).
  let cwd = ''

  on('session.start', async ($, e, next) => {
    cwd = e.cwd
    try {
      await $.command.register({ name: 'conclave', description: 'Show the Conclave pane: units, issues and claims' })
      // The list this checkout saved, applied from the first turn; with none, nothing is hidden this session.
      await applySavedOffer($, false)
      // The first read runs in the background: the session never waits for it, and its list serves the next session.
      startPolling($, () => void showStatus($, cwd).catch(() => undefined))
      // Unasked, the pane seats only where the terminal is wide enough; below that it waits for /conclave.
      if (e.isInteractive) $.ui.open({ id: PANE, title: 'Conclave' }).catch(() => undefined)
    } catch {
      // A view that fails to set up is a view the person lacks, never a session that fails to start.
    }
    return next(e)
  })

  // Reads the agent type of the session's main thread; the event and its answer pass through untouched. A new
  // conversation after `/clear` also takes up the list saved since the session started: its prompt cache starts empty,
  // so nothing is rewritten. A compaction keeps the cache of the listing, so it keeps the list too.
  on('classic.SessionStart', async ($, e, next) => {
    const result = await next(e)
    try {
      await setRole($, e.agent_type ?? null)
      if (e.source !== 'compact') await applySavedOffer($, true)
      await showStatus($, cwd)
    } catch {
      // The status line is a convenience; the answer beneath (Tether's HANDOFF context) is returned whole.
    }
    return result
  }).catch(($, e, next) => next(e))

  on('command.run', { command: 'conclave' }, async $ => {
    await openPane($)
    return { text: 'Conclave pane opened.' }
  })

  on('agent.offer', async ($, e, next) => ((await isOffered($, e.agent)) ? next(e) : { isOffered: false })).catch(
    ($, e, next) => next(e),
  )

  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => (await drawBand($, e)) ?? next(e))

  on('ui.render', { component: 'Pane', requestId: PANE }, ($, e) => drawPane($, e))
}
