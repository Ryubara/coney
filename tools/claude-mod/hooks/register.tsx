// SPDX-License-Identifier: GPL-3.0-or-later
// Conclave: Coney's coordination view. Wiring only: which event calls which file.
//
// It draws what `coney-tools conclave snapshot` prints, hides the generic agent types and, in a coordinator's session,
// watches Coney's own events: a toast and one wake for each event the first time it is seen, and a stop guard on pull
// request problems nobody holds (docs/guides/conclave.md#watchers). It never wakes an idle session and adds no tool:
// session health (the context window, the prompt cache, /clear, HANDOFF, the idle wake) stays with Tether, and the one
// classic hook below that reads the session's start only reads its agent type. Nothing here waits on anything: a session
// start, Tether's included, never pauses for Conclave (test_conclave_mod.py holds the boundary).
import type { Register } from 'claude-code'

import { drawBand, openPane } from '../src/band'
import { PANE } from '../src/format'
import { applySavedOffer, isOffered } from '../src/offer'
import { drawPane } from '../src/pane'
import { startPolling } from '../src/snapshot'
import { setRole } from '../src/state'
import { showStatus } from '../src/status'
import { guardReason, watchEvents } from '../src/watch'

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
      // After each read the status line is redrawn and the watchers look at the events it brought.
      startPolling($, (state, at) => {
        void showStatus($, cwd).catch(() => undefined)
        void watchEvents($, state, at).catch(() => undefined)
      })
      // Unasked, the pane seats only where the terminal is wide enough; below that it waits for /conclave.
      if (e.isInteractive) $.ui.open({ id: PANE, title: 'Conclave' }).catch(() => undefined)
    } catch {
      // A view that fails to set up is a view the person lacks, never a session that fails to start.
    }
    return next(e)
  })

  // Reads the agent type of the session's main thread; the event and its answer pass through untouched. A new
  // conversation (a startup or a `/clear`) also takes up the list saved since the session started: its prompt cache
  // starts empty, so nothing is rewritten. A compaction and a resume keep the listing's cache, so they keep the list.
  on('classic.SessionStart', async ($, e, next) => {
    const result = await next(e)
    try {
      await setRole($, e.agent_type ?? null)
      if (e.source === 'startup' || e.source === 'clear') await applySavedOffer($, true)
      await showStatus($, cwd)
    } catch {
      // The status line is a convenience; the answer beneath (Tether's HANDOFF context) is returned whole.
    }
    return result
  }).catch(($, e, next) => next(e))

  // The stop guard: the end of a coordinator's turn is held back, at most a few times in a row, while an open pull request
  // has a problem nobody holds. The answer beneath (Tether's own guard for stopped agents) stays whole.
  on('classic.Stop', async ($, e, next) => {
    const r = await next(e)
    const reason = await guardReason($)
    return reason === null ? r : { ...r, block: reason }
  }).catch(($, e, next) => next(e))

  on('command.run', { command: 'conclave' }, async $ => {
    await openPane($)
    return { text: 'Conclave pane opened.' }
  })

  on('agent.offer', async ($, e, next) => ((await isOffered($, e.agent)) ? next(e) : { isOffered: false })).catch(
    ($, e, next) => next(e),
  )

  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => (await drawBand($, e)) ?? next(e))

  // The filter is a literal: the engine's own validation reads it (a const shows as `?`), and so does the check.
  on('ui.render', { component: 'Pane', requestId: 'conclave' }, ($, e) => drawPane($, e))
}
