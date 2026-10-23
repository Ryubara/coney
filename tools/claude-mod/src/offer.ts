// SPDX-License-Identifier: GPL-3.0-or-later
// Part A: the agent types the model is offered. Anything that is neither a role nor a helper in the hook settings
// (general-purpose and the rest) is hidden; the dispatcher's agent guard still refuses a spawn of one, so this is a
// convenience and the hook is the rule. The list is frozen: a changing agent listing would rewrite the prompt cache, so
// a snapshot that changes the settings mid-session takes effect where a new cache starts (the next session, or a
// `/clear`). Each checkout saves its own list, because each has its own hook settings.
import type { EngineInterface } from 'claude-code'

import { isOffer, offerKey } from './snapshot'
import { getOffer, setOffer } from './state'

/**
 * Freezes the list this checkout saved, if it saved a valid one. `replace` swaps one already frozen for it (a new
 * conversation after `/clear`, whose prompt cache starts empty anyway); without it a frozen list stands, so a reload of
 * the mod mid-session changes nothing.
 */
export async function applySavedOffer($: EngineInterface, replace: boolean): Promise<void> {
  const saved = await $.store.get(offerKey($.plugin.root))
  if (isOffer(saved) && (replace || (await getOffer($)) === null)) await setOffer($, saved.allowed)
}

/** Whether the model is offered `agent`; true while no list is known (nothing is hidden on a guess). */
export async function isOffered($: EngineInterface, agent: string): Promise<boolean> {
  const frozen = await getOffer($)
  return frozen === null || frozen.allowed.includes(agent)
}
