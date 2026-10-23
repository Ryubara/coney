// Conclave: Coney's coordination view. Wiring only: which event calls which file.
//
// Read-only. It draws what `coney-tools conclave snapshot` prints and hides the generic agent types. It wakes nobody,
// guards no stop and adds no tool: session health (the context window, the prompt cache, /clear, HANDOFF) stays
// with Tether, and the one classic hook below only reads the session's agent type.
import type { Register } from 'claude-code'

import { drawBand, openPane, PANE } from '../src/band'
import { isOffered } from '../src/offer'
import { drawPane } from '../src/pane'
import { isOffer, startPolling } from '../src/snapshot'
import { getOffer, setOffer, setRole } from '../src/state'
import { showStatus } from '../src/status'

export const register: Register = on => {
  // The session's folder, for the status line (a module variable resets on a reload, and session.start sets it again).
  let cwd = ''

  on('session.start', async ($, e, next) => {
    cwd = e.cwd
    await $.command.register({ name: 'conclave', description: 'Show the Conclave pane: units, issues and claims' })
    const stored = await $.store.get('offer')
    if (isOffer(stored) && (await getOffer($)) === null) await setOffer($, stored.allowed)
    const first = startPolling($, () => void showStatus($, cwd))
    // With no list from an earlier session, the first listing waits for the first read (a second, as a rule).
    if ((await getOffer($)) === null && e.isInteractive) await first
    // Unasked, the pane seats only where the terminal is wide enough; below that it waits for /conclave.
    if (e.isInteractive) void $.ui.open({ id: PANE, title: 'Conclave' })
    return next(e)
  })

  // Reads the agent type of the session's main thread and passes the event on untouched.
  on('classic.SessionStart', async ($, e, next) => {
    await setRole($, e.agent_type ?? null)
    await showStatus($, cwd)
    return next(e)
  })

  on('command.run', { command: 'conclave' }, async $ => {
    await openPane($)
    return { text: 'Conclave pane opened.' }
  })

  on('agent.offer', async ($, e, next) => ((await isOffered($, e.agent)) ? next(e) : { isOffered: false }))

  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => (await drawBand($, e)) ?? next(e))

  on('ui.render', { component: 'Pane', requestId: PANE }, ($, e) => drawPane($, e))
}
