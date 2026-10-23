// SPDX-License-Identifier: GPL-3.0-or-later
// The values the mod keeps in `$.state` (declared in types/index.d.ts): what the drawing reads, never what decides.
// Every read and write of them lives in this file, because the engine's scan finds a state reference only in the file
// that reads it; the rest of the mod calls these functions.
import { atom, read, update } from 'claude-code'
import type { EngineInterface } from 'claude-code'

import type { Held, Offer } from '../types'

export const EMPTY: Held = {
  snapshot: null,
  localAt: 0,
  githubAt: 0,
  error: null,
  githubError: null,
  newer: null,
}

// Change a tag when the value's shape changes: a reload then declines what the old code stored.
const held = atom({ plugin: 'conclave', key: 'held' } as const, EMPTY, { shape: 'held-1' })
const open = atom({ plugin: 'conclave', key: 'open' } as const, null)
const now = atom({ plugin: 'conclave', key: 'now' } as const, 0)
const role = atom({ plugin: 'conclave', key: 'role' } as const, null)
const offer = atom({ plugin: 'conclave', key: 'offer' } as const, null, { shape: 'offer-1' })

/** The sections of the last good local and GitHub reads, merged. */
export function getHeld($: EngineInterface): Promise<Held> {
  return read($, held)
}

/** Replaces the held sections with what `change` makes of them. */
export function changeHeld($: EngineInterface, change: (state: Held) => Held): Promise<unknown> {
  return update($, held, change)
}

/** The unit or claim row that is open on the pane, or null. */
export function getOpen($: EngineInterface): Promise<string | null> {
  return read($, open)
}

/** Opens the row `key`, or closes it when it is the open one. */
export function toggleOpen($: EngineInterface, key: string): Promise<unknown> {
  return update($, open, current => (current === key ? null : key))
}

/** The mod's own clock in milliseconds, as the poll last moved it. */
export function getNow($: EngineInterface): Promise<number> {
  return read($, now)
}

/** Moves the mod's clock to `ms`, which redraws the countdowns that read it. */
export function setNow($: EngineInterface, ms: number): Promise<unknown> {
  return update($, now, () => ms)
}

/** The main session's agent type, or null when it started with none. */
export function getRole($: EngineInterface): Promise<string | null> {
  return read($, role)
}

/** Records the main session's agent type (null: it started with none). */
export function setRole($: EngineInterface, type: string | null): Promise<unknown> {
  return update($, role, () => type)
}

/** The agent types the model is offered, frozen for the session; null while none is known (nothing is hidden). */
export function getOffer($: EngineInterface): Promise<Offer | null> {
  return read($, offer)
}

/** Freezes the agent types the model is offered for the rest of the session. */
export function setOffer($: EngineInterface, allowed: string[]): Promise<unknown> {
  return update($, offer, () => ({ allowed }))
}

/** Drops the frozen list, so nothing is hidden (the hook settings can no longer be read). */
export function clearOffer($: EngineInterface): Promise<unknown> {
  return update($, offer, () => null)
}
