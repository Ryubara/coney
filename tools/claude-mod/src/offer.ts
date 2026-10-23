// Part A: the agent types the model is offered. Anything that is neither a role nor a helper in the hook settings
// (general-purpose and the rest) is hidden; the dispatcher's agent guard still refuses a spawn of one, so this is a
// convenience and the hook is the rule. The list is frozen for the session: a changing agent listing would rewrite
// the prompt cache, so a snapshot that changes the settings mid-session takes effect in the next session.
import type { EngineInterface } from 'claude-code'

import { getOffer } from './state'

/** Whether the model is offered `agent`; true while no list is known (nothing is hidden on a guess). */
export async function isOffered($: EngineInterface, agent: string): Promise<boolean> {
  const frozen = await getOffer($)
  return frozen === null || frozen.allowed.includes(agent)
}
