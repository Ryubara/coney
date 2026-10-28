# Coney Studio: a Human's properties

**Status: design.** Nothing on this page is built. Every property of a **Human** that Coney Studio's Inspector
shows, grouped as the Inspector groups them, with what Coney sets today and what waits, and the shorter lists of a
**Target** and a **Gang**. How the schema, the groups, the `live` field and the events work is on [Entities, kinds
and the Inspector](entities.md); this page is its list for the kind with the most properties.

**Set by** names the game native Coney calls at level entry and on a live edit (its
[masterlist](../../references/bindings/index.md) entry holds what it does and the evidence for it; meanings the
masterlist gives as inferred, from a native's name, keep that in their help), or *creation* (an argument of
`HuCreate`), or *Coney edit* (an edit of the Entity inspector, Coney's own). **Live** is the schema's field ([Level
and Running](entities.md#running)). **Coney** is *ready* when the native or edit exists, or what the property waits
on; every *ready* property set from a level file also waits, with all of its kind, on Coney making entities from
level files ([Proposed build issues](content-and-formats.md#build-issues)). *(A)* marks an Advanced property; a
*Debug* group is shown only for a running human.

## Entity {#human-entity}

Every kind has this group.

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Id `id` | Studio writes it; never edited | read-only | ready |
| Name `name` | creation (`HuCreate`'s name); `HuSetName` while playing. The game keeps 15 characters, and Studio warns at the 16th | live | ready |
| Tags `tags` | the level's lookup table ([Names and tags](project-formats.md#names-tags)) | next-play | ready |
| Parent, place in the tree | the Hierarchy | next-play | waits on level files of version 2 |
| Attributes | [Attributes](entities.md#attributes) | live | waits on the attribute natives |

## Identity

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Character type `type` ([Characters](../../references/characters.md)) | creation | next-play | ready |
| Model `model` (default: the type's) | Coney edit `model` | live | ready with [#1053](issue:1053) |
| Custom character `character` (a project `.character`) | creation | next-play | waits on custom characters |
| Player slot `player` (none, or player 1) | creation (`HuCreate`'s `player`) | next-play | ready; more slots wait on co-op in levels |
| Gang `gang` (a [Gang](#gang) entity) | creation; `GangAddMember` while playing | live | ready |
| Gang leader `leader` | `GangSetLeader` | live | ready |
| War chief `war-chief` | `HuSetWarChief` | live | waits on the native |
| Role, controlled by, player profile, brain handle | Coney accessors | watch | ready |

## Transform

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Position `at` | creation; Coney edit `teleport` | live | ready |
| Heading `heading` (a human turns about z only) | creation; Coney edit `heading` | live | ready |
| Scale `scale` | `HuSetScale`; Coney edit `scale` | live | ready |
| Scale collision with mesh `scale-collision` (A) | Coney edit | live | ready |
| Velocity, speed, lean | Coney accessors | watch | ready |

## Meters

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Health `health` (percent of the maximum) | `HuSetHealthPercent`; Coney edit `health` | live | ready |
| Maximum health `max-health` (default: the type's) | `HuSetMaxHealth` | live | ready |
| Stamina `stamina`, Power `power` (percent) | Coney edits | live | ready |
| Rage `rage` (percent) | `HuSetRageFrac`; Coney edit `rage` | live | ready |
| Rage locked `rage-locked` | `HuSetLockedRage` | live | ready |
| Rage allowed `rage-allowed` | `HuSetPreventRage` (its argument inverted) | live | ready |
| Raging `rage-mode` | `HuSetRageMode` | live | ready |
| Invincible `god` | `HuSetGodMode`; Coney edit `invincible` | live | ready |
| Demi-god `demi-god` and its share (A) | `HuSetDemiGodMode` | live | ready |
| Health now and maximum, hurt | Coney accessors | watch | ready |

## Carrying

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Money `money` (0 to 999; default: rolled from the type) | `HuSetMoney` | live | ready |
| Carried item `carry` (an object type) | `HuSetCarriedItem` | live | ready |
| Handcuffs `cuffs` (a player) | `HuGiveCuffs` | live | ready |
| Fighting gear `gear` (a player) | `HuAttachGear` | live | ready |
| Keeps hat `keep-hat`, keeps weapon `keep-weapon` | `HuSetKeepHat`, `HuSetKeepWeapon` | live | ready |
| Never throws weapon `no-throw-weapon` | `HuSetNoThrowWeapon` | live | ready |
| Weapon `weapon` | `HuGiveWeapon` | live | waits on the native |

## Combat

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Fight stance `combat-mode` | `HuSetCombatMode` | live | ready |
| Auto combat `auto-combat` | `HuSetAutoCombat` | live | ready |
| Double damage `double-damage`, killer `killer` | `HuSetDoubleDamage`, `HuSetKillerMode` | live | ready |
| Damage factor `damage-factor` (A) | `HuApplyDamageModifier` | live | ready |
| Accurate, strong, blocks knock-downs | `HuSetAccurate`, `HuSetStrong`, `HuSetCanBlockKD` | live | wait on the natives |
| Mode, attack, blocking, pair, holding, grabbed, target, strike shapes, hits | Coney accessors | watch | ready |

## Being hit

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Ungrabbable, ungroundable, unstunnable | `HuSetUngrabbable`, `HuSetUngroundable`, `HuSetUnstunnable` | live | ready |
| No reaction, reduced reaction, increased reaction | `HuSetNoReact`, `HuSetReducedReact`, `HuSetIncreasedReact` | live | ready |
| Blocks grabs, blocks rear grabs, blocks tackles | `HuBlockGrab`, `HuBlockRearGrab`, `HuBlockTackle` | live | ready |
| Escapes grabs `auto-escape` | `HuSetAutoEscape` | live | ready |
| Pushable `pushable` | `HuSetPushable` | live | ready |
| Blocks front grabs, cannot escape, manual stun, hard to stun, no reaction from blocks | `HuBlockFrontGrab`, `HuSetNoEscape`, `HuSetManualStun`, `HuSetHardToStun`, `HuSetBlockFromReact` | live | wait on the natives |
| Heavy, fire-proof, on fire | `HuSetMass`, `HuSetFireProof`, `HuSetOnFire` | live | wait on the natives |

## Condition

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Woundable, wounded, revivable | `HuSetWoundable`, `HuSetWounded`, `HuSetRevivable` | live | ready |
| Conscious, arrested | `HuSetConscious`, `HuSetArrested` | live | ready |
| The player can un-arrest him `unarrestable` (the native's name says the opposite of what it does) | `HuSetUnarrestable` | live | ready |
| Stunned, knocked down, beaten up | `HuSetStunned`, `HuMakeGrounded`, `HuMakeBeatUp` | live | wait on the natives |
| *Normal state* (an action: ends grabs, fire, wounds) | `HuSetNormalMode` | running-only | ready |

## Movement

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Speed scale `speed-scale` (A) | Coney edit | live | ready |
| Tireless, fast climber | `HuSetTireless`, `HuSetFastClimber` | live | ready |
| Blocks jumping, blocks looking round | `HuBlockJump`, `HuBlockLook` | live | ready |
| Commands enabled `commands` ([Commands](../../references/commands.md)) | `EnableCommand`, `EnableCommands` | live | ready |
| Wheelchair control (a player) | `HuSetWheelchairControl` | live | ready |
| Frozen `frozen` | Coney edit | running-only | ready |
| Blocks climbing, pad control, button-tap control | `HuBlockClimb`, `HuEnableController`, `HuSetButtonTapControl` | live | wait on the natives |
| Gait, support, traversal, stick, intent, command, buttons, brain move, ground, landings | Coney accessors | watch | ready |

## AI

Shown for a human with a brain. Defaults come from the type's behaviour and attack table ([Stats](#human-stats)).

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Brain type `brain` ([AI types](../../research/ai/index.md#types)) | `BrSetType` | live | ready |
| Brain on `brain-on`, suspended `suspended` | `BrDead` (inverted), `BrSuspend` | live | ready |
| Pedestrian type, pedestrian reaction | `BrSetPedType`, `HuSetPedReaction` | live | ready |
| Threat, damage and investigate responses | `BrSetThreatResponse`, `BrSetDamageResponse`, `BrSetInvestigateResponse` | live | ready |
| Reacts to violence, picks up weapons | `BrSetReactToViolence`, `BrSetThugWantsWeapon` | live | ready |
| Uses world flags, and how likely (A) | `BrCanUseWorldFlags` | live | ready |
| Sight range, field of view, hearing range, help hearing range (A) | `HuSetLOSRange`, `BrSetFOV`, `HuSetHearRange`, `HuSetHelpHearRange` | live | ready |
| Attack weights (A; one per attack kind, [Attacks](../../references/attacks.md)) | `BrSetAttackWeight` | live | ready |
| Picks up hats, answers the player, melee range, attack slots | `BrSetThugWantsHat`, `BrSetPlayerResponse`, `BrSetMeleeRange`, `BrSetNumAttackSlots` | live | wait on the natives |
| Goals (top first), reaction goal, actions, target, enemies, attack slots, melee range, thinks | Coney accessors | watch | ready |

## Behaviour

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Starting goal `goal`: one of the available `Goal*` natives ([AI natives](../../references/bindings/ai.md)), such as guard a flag, travel a path or walk an area, with its arguments by name from the native's signature (the debug menu's argument editors, [Natives](../debug-menu.md#natives)) and its callback, if it takes one, as an event field | the goal's native, once, after the human is made | next-play | ready |

The starting goal is the one behaviour a level gives a human without a script: what a level script does right after
`HuCreate`. Anything that reacts to events is the project's scripts' work. A gang's tactic is on the [Gang](#gang).

## Speech

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Says commands, comments on tags, cheers tags | `HuEnableSoundCommands`, `HuEnableTagDone`, `HuEnableTagCheer` | live | ready |
| Voice, state-response voice ([Speech](../../references/speech.md)) | `HuSetVoiceIndex`, `HuSetStateRespVoiceIndex` | live | wait on the natives |
| Speaks at all, the on-fire line, idle chatter lines | `HuEnableSpeaking`, `HuEnableOnFire`, `HuSetIdleDialogMaxIter` | live | wait on the natives |

## Interaction

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Muggable `muggable`, and the *On Mugged* event | `HuSetMug`, `HuSetMugCallback` | live | ready |
| Interrogation: four lines and the icon | `HuSetInterrogation` (its callback is the *On Interrogated* event) | live | ready |
| Spinning icon `icon` (an object type) | `HuAttachSpinningIcon` | live | ready |
| Not targetable `no-target`, skipped by auto lock `no-auto-lock` | `HuSetNoTarget`, `HuSetNoAutoLock` | live | ready |
| Icon colour, tint | `HuSetSpinningIconColor`, `HuColor` | live | wait on the natives |

## Animation

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Animation rate `anim-rate` (A) | Coney edit | live | ready |
| Looks at `look-at` (an entity), for how long and how strongly (A) | `HuSetLookTarget` | live | ready |
| Idle clip | `HuSetIdleAnim` | live | waits on the native |
| A clip to play | `Coney.PlayClip` ([Assets by path](lua-sdk.md#assets)) | running-only | waits on the native |
| Clip, state, time, task, held flags, gait value, idle clip, action event | Coney accessors | watch | ready |

## Stats {#human-stats}

What the character type gives the human when he is made, from the four configuration tables
([Characters: Stats](../../research/characters.md#stats)), shown read-only so a user sees where the other groups'
defaults come from: category, behaviour, speed class, power class, Warrior class, health, the stamina and power maxima
and refills, the hurt threshold, the stun and knockdown times, the damage and attack tables, the hat, the voice, the
drop group and chance. Setting one per human waits on the engine keeping per-human stats (the rest of the stats
inventory on [#723](issue:723)); until then a different set of stats is a different character type.

## Trigger and events

A human may carry a trigger sphere, as the scripts give one to a character to talk to (`TriggerSphereCfg` on his
handle): the Trigger group's armed switch, radius, acceptance and repeat period, all ready. His events (*On Enter*, *On
Exit*, *While Inside*, *On Interact*, *On Damaged*, *On Died* and the rest) are in his Events group, by the table of
[Events](entities.md#events).

## Debug groups

Shown for a running human only, closed at first: **State flags** (the flag word decoded by name, the scripts'
switches, the disabled commands, the target state), **Physics** (the walking sphere, the capsule, the air sphere, the
contacts, blocked and airborne updates, the climb) and **Mesh** (the model's counts and draw calls,
[The Mesh group](../debug-menu.md#mesh-group)). Each named flag the scripts can set already has its property above, so
the raw word stays a watch.

**Actions** on a running human, in the toolbar: *Drop to ground*, *Move to camera*, *Teleport player to it*, *Kill*
(`HuKill`, or the Coney edit), *Delete* (`HuDelete`, or the Coney edit), *Reset* (every property back to its
configured value) and *Log everything*.

## A Target {#target}

Coney's passive sandbox human has no brain and no body in the physics, so its schema is short: Entity, Identity (its
model, Rembrandt's, which the model swap of [#1053](issue:1053) changes while playing), Transform (position and
heading), Meters (*Health* `health`, 600 by default), each saved and `next-play` as a layout's `target` statement keeps
them ([Statements](../sandbox.md#statements)), *Frozen* (`running-only`), and the watches of Identity, Transform,
Animation, Meters and Combat that the Entity inspector shows for it. The Entity editor takes no other edit of a target,
so while playing its health is a watch until the play mode handles one.

## A Gang {#gang}

A human's relations are his gang's: two gangs are enemies or friends, and a human fights whoever his gang treats as an
enemy. So *gang and relations* are a kind of their own, an entity with no transform (Roblox's teams are instances in
the same way), and a Human's *Gang* points at one. The level file stores a gang as a `gang` statement and a
human's gang as a reference to it by id ([What the level file
stores](entities.md#level-file)).

| Property | Set by | Live | Coney |
| --- | --- | --- | --- |
| Name, gang type ([Gangs](../../references/gangs.md)) | `GangCreate` | next-play | ready |
| Enemies, friends (both ways) | `GangMakeEnemies`, `GangMakeFriends` | live | ready |
| Enemies and neutral by gang type | `GangMakeEnemiesOfType`, `GangMakeNeutralOfType` | live | ready |
| One-way enemy, friend, neutral | `GangSetEnemy`, `GangSetFriend`, `GangSetNeutral` | live | wait on the natives |
| Leader (a Human) | `GangSetLeader` | live | ready |
| Turf (volume boxes) | `GangAddTurfBox` | live | ready |
| Threat, damage and investigate responses; respond percentage | `GangSetThreatResponse`, `GangSetDamageResponse`, `GangSetInvestigateResponse`, `GangSetRespondPercentage` | live | ready |
| Targetable, attackable, always seen, hearing range | `GangSetTargetable`, `GangSetAttackable`, `GangSetAlwaysSeen`, `GangSetHearRange` | live | ready |
| Sight range, money, reacts to violence, spotting line | `GangSetLOS`, `GangSetMoney`, `GangSetReactToViolence`, `GangSetCustomSpotDialog` | live | wait on the natives |
| Starting tactic: one of the available `Tactic*` natives with its arguments and its callback as the *On Tactic* event | the tactic's native, once | next-play | ready |

A gang made by a base level's own scripts is shown read-only under the base level, from its set-up trace
([Levels and worlds](content-and-formats.md#levels)), and a project's human may join it by that gang's name.
