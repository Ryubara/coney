// The status line: `role · unit · stage`, set after each read and when the session's role is known.
import type { EngineInterface } from 'claude-code'

import { statusText } from './format'
import { getHeld, getNow, getRole } from './state'

/** Pins the status line from what the mod holds now. `cwd` is the session's folder, to find its unit. */
export async function showStatus($: EngineInterface, cwd: string): Promise<void> {
  const state = await getHeld($)
  const nowMs = Math.max(await getNow($), state.localAt)
  $.ui.status(statusText(await getRole($), cwd, state, nowMs))
}
