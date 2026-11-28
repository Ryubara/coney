---
kind: topic
system: characters
owns:
  functions: [
    "0x00233ef0", "0x00233f60", "0x00234038", "0x002340a8", "0x00234118", "0x00234318", "0x00234530", "0x002345d8",
    "0x002345f8", "0x002346a0", "0x00234768", "0x00234830", "0x002348f8", "0x002349a0", "0x00234a68", "0x00234b30",
    "0x00234bf8", "0x00234cb0", "0x00234d08", "0x00234d70", "0x00234e58", "0x00234ef8", "0x00234f68", "0x00234fb8",
    "0x00235010", "0x002350c8", "0x00235130", "0x00235198", "0x00235200", "0x00235268", "0x002352b0", "0x002352f8",
    "0x00235478", "0x002354b8", "0x00235530", "0x002355a0", "0x002355e0", "0x00235628", "0x00235688", "0x002356c8",
    "0x00235718", "0x00235758", "0x00235798", "0x002357d8", "0x00235818", "0x00235890", "0x002358c8", "0x002359a0",
    "0x00235a28", "0x00235a88", "0x00235ae8", "0x00235b68", "0x00235bc8", "0x00235c28", "0x00235c88", "0x00235ce8",
    "0x00235d50", "0x00235db0", "0x00235e10", "0x00235e70", "0x00235f10", "0x00235f20", "0x00235f80", "0x00235fc0",
    "0x002360f8", "0x00236188", "0x002361e8", "0x00236420", "0x00236450", "0x00236460", "0x00236470", "0x00236480",
    "0x00236490", "0x002364d8", "0x002364e8", "0x002364f8", "0x00236978", "0x002369b8", "0x00236a68", "0x00236ad0",
    "0x00236b38", "0x00236be0", "0x00236c28", "0x00236c48", "0x00236c58", "0x00237128", "0x002371d8", "0x002372a8",
    "0x00237328", "0x00237388", "0x002373f8", "0x00237468", "0x002374d8", "0x00237548", "0x002375b8", "0x00237628",
    "0x002376a0", "0x00237700", "0x00237778", "0x002377f8", "0x00237848", "0x002378a8", "0x00237958", "0x002379c0",
    "0x00237a28", "0x00237a98", "0x00237b00", "0x00237b70", "0x00237be0", "0x00237c38", "0x00237c78", "0x00237d08",
    "0x00237d48", "0x00237db8", "0x00237e00", "0x00237e70", "0x00238030", "0x00238078", "0x002380c0", "0x00238158",
    "0x002381f0", "0x00238230", "0x00238288", "0x002383a0", "0x002383e8", "0x00238430", "0x00238478", "0x002384f8",
    "0x002385f8", "0x00238640", "0x00238690", "0x00238748", "0x002387a8", "0x002387e8", "0x00238828", "0x00238948",
    "0x002389e8", "0x00238a88", "0x00238ae0", "0x00238b18", "0x00238bd8", "0x00238cd8", "0x00238d30", "0x00238f10",
    "0x00239080", "0x00239188", "0x002391a8", "0x002391c0", "0x00239200", "0x00239240", "0x00239280", "0x002392c0",
    "0x00239300", "0x00239340", "0x00239370", "0x00239558", "0x002395a0", "0x00239788", "0x00239850", "0x00239888",
    "0x002398b0", "0x00239e08", "0x00239e30", "0x00239e78", "0x00239ee0", "0x0023a058", "0x0023a210", "0x0023a258",
    "0x0023a2c0", "0x0023a328", "0x0023a390", "0x0023a3b8", "0x0023a460", "0x0023a468", "0x0023a4e0", "0x0023a540",
    "0x0023a550", "0x0023a560", "0x0023a588", "0x0023a598", "0x0023a5a8", "0x0023a5b8", "0x0023a5c8", "0x0023a5d8",
    "0x0023a5e8", "0x0023a5f8", "0x0023a630", "0x0023a658", "0x0023a7a0", "0x0023a970", "0x0023a980", "0x0023a990",
    "0x0023a9a0", "0x0023ad88", "0x0023adc8", "0x0023ae00", "0x0023ae40", "0x0023ae90", "0x0023aee0", "0x0023b0e0",
    "0x0023b128", "0x0023b138", "0x0023b778",
  ]
---
# Characters: the script functions

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

The `Hu...` functions the level scripts call to create, equip, move and ask about humans, with the function that
does each. The creation itself is [Creation](characters/index.md#creation); the bindings are on
[Character bindings](../references/bindings/character.md).

## Map {#original-structure}

### Script functions {#code-script}

The engine side of the `Hu*` and `Cfg*` script functions (`0x00233ef0`-`0x0023b838`, inferred from where the rows lie;
each row is graded); each row names its script function. A setter with a flag turns that bit of the human flags `+0xe0`
on or off; a `Cfg*` setter writes one global the human code reads.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00233ef0` | `Human_Delete` | Marks the human to be taken away (record state 0x200000000: faded out and destroyed by `Humans_Update`, [Falling out of the world](characters-ground.md#out-of-world)), clears its foe and its script handlers. | confirmed (code) |
| `0x00233f60` | `Human_SwapPlayerControl` | Moves player control from one human to another, swapping their camera targets. | confirmed (code) |
| `0x00234038` | `Human_SetNoTarget` | Human flag 0x100000000000 on or off: the human cannot be targeted. | confirmed (code) |
| `0x002340a8` | `Human_SetNoAutoLock` | Human flag 0x8000000000 on or off: no automatic lock-on. | confirmed (code) |
| `0x00234118` | `Human_SetAutoCombat` | Human flag 0x200000000000 on or off. | confirmed (code) |
| `0x00234318` | `Human_SetButtonTapControl` | Puts the human under button-tap movement (or back): saves control, swaps the commands and sets the state. | confirmed (code) |
| `0x00234530` | `Cfg_SetWorkoutParams` | Fills the workout parameter tables at 0x00510690-0x005106b0. | confirmed (code) |
| `0x002345d8` | `Human_SetWorkoutParams` | Sets three workout values at 0x00510680. | confirmed (code) |
| `0x002345f8` | `Human_SetWorkoutCallbacks` | Interns the workout start, rep and end Lua callbacks. | confirmed (code) |
| `0x002346a0` | `Workout_CallStart` | Calls the workout start callback with the human. | confirmed (code) |
| `0x00234768` | `Workout_CallRep` | Calls the workout repetition callback with the human. | confirmed (code) |
| `0x00234830` | `Workout_CallEnd` | Calls the workout end callback with the human. | confirmed (code) |
| `0x002348f8` | `Human_SetSpecialCallbacks` | Interns the special-move start, step and end Lua callbacks. | confirmed (code) |
| `0x002349a0` | `Special_CallStart` | Calls the special start callback with the human. | confirmed (code) |
| `0x00234a68` | `Special_CallStep` | Calls the special step callback with the human. | confirmed (code) |
| `0x00234b30` | `Special_CallEnd` | Calls the special end callback with the human. | confirmed (code) |
| `0x00234bf8` | `Human_StartSpecial` | Script: puts a clip on anim 0x2a0 and starts the special action. | confirmed (code) |
| `0x00234cb0` | `Human_StopSpecial` | Script: ends a running special with action 0x1e. | confirmed (code) |
| `0x00234d08` | `Human_SetAccurate` | `HuSetAccurate`: human flag 0x40000 on or off. | confirmed (code) |
| `0x00234d70` | `Human_MakeBeatUp` | Script: holds the brain and knocks the human down lying, dropping what it holds. | confirmed (code) |
| `0x00234e58` | `Human_MakeGrounded` | Script: lays the human down, or lifts the down latch. | confirmed (code) |
| `0x00234ef8` | `Human_SetLockMovement` | `HuLockMovement`: human flag 0x200000000 on or off; the rumble menus use it on preview fighters. | confirmed (code) |
| `0x00234f68` | `Human_GetHandleByIndex` | `HuGet`: the handle of human n, or null. | confirmed (code) |
| `0x00234fb8` | `Human_GetPlayerHandle` | `HuGetPlayer`: the handle of the game state's human n. | confirmed (code) |
| `0x00235010` | `Human_FindByName` | `HuFind`: the first human whose name matches. | confirmed (code) |
| `0x002350c8` | `Human_SetTireless` | Human flag 0x4000000 on or off. | confirmed (code) |
| `0x00235130` | `Human_SetManualStun` | Human flag 0x8000000 on or off. | confirmed (code) |
| `0x00235198` | `Human_SetNoReact` | Human flag 0x800 on or off. | confirmed (code) |
| `0x00235200` | `Human_SetAutoEscape` | `HuSetAutoEscape`: human flag 0x20000 on or off. | confirmed (code) |
| `0x00235268` | `Human_SetPushable` | `HuSetPushable`: resolves the handle and calls the human's pushable setter (0x0021d848). | confirmed (code) |
| `0x002352b0` | `Human_GetPositionRef` | `HuGetPosition`: the human's position, or the zero vector 0x005116c0. | confirmed (code) |
| `0x002352f8` | `Human_GetHeadingDegrees` | `HuGetHeading`: the heading of the pose-slot rotation, from an arctangent approximation. | confirmed (code) |
| `0x00235478` | `Human_GetGangType` | `HuGetGangType`: the brain's gang (`+0x20c`), -1 without a human. | confirmed (code) |
| `0x002354b8` | `Human_SetStunned` | `HuSetStunned`: stuns the human or ends the stun. | confirmed (code) |
| `0x00235530` | `Human_GetHat` | `HuHasHat`: the handle of the worn hat `+0x364`, or null. | confirmed (code) |
| `0x002355a0` | `Human_SetStrong` | `HuSetStrong`: byte `+0x3b9`. | confirmed (code) |
| `0x002355e0` | `Human_IsGrabbed` | `HuIsGrabbed`: any of state flags 0x830 (grabbed from the front or rear, tackled). | confirmed (code) |
| `0x00235628` | `Human_IsAlive` | `HuIsAlive`: not down or dead and up. | confirmed (code) |
| `0x00235688` | `Human_IsDead` | `HuIsDead`: dead (true without a human). | confirmed (code) |
| `0x002356c8` | `Human_IsInScene` | `HuIsInScene`: in a scene state. | confirmed (code) |
| `0x00235718` | `Human_IsArrested` | `HuIsArrested`: cuffed. | confirmed (code) |
| `0x00235758` | `Human_IsPlayer` | `HuIsAPlayer`: player index `+0x1b0` is not -1. | confirmed (code) |
| `0x00235798` | `Human_IsHidden` | `HuIsHidden`: the brain's hidden byte `+0x2d4`. | confirmed (code) |
| `0x002357d8` | `Human_IsMuggedByHandle` | `HuIsMugged`: being mugged. | confirmed (code) |
| `0x00235818` | `Human_IsProne` | `HuIsProne`: in states 0xe0000 or 0x2000. | confirmed (code) |
| `0x00235890` | `Human_GetCharType` | `HuGetCharType`: the character type `+0xcc`. | confirmed (code) |
| `0x002358c8` | `Human_SetDamage` | `HuSetDamage`: queues pending damage from an attacker's held object. | confirmed (code) |
| `0x002359a0` | `Human_SetDemiGodMode` | `HuSetDemiGodMode`: human flag 0x20000000000 and the minimum health 0x0051024c. | confirmed (code) |
| `0x00235a28` | `Human_SetGodMode` | `HuSetGodMode`: human flag 0x10; the tired goal also uses it. | confirmed (code) |
| `0x00235a88` | `Human_SetKillerMode` | `HuSetKillerMode`: human flag 0x20. | confirmed (code) |
| `0x00235ae8` | `Human_SetUntouchable` | AI only (two goal functions): human flags 0x2c0 together (ungrabbable, ungroundable, reduced reactions). | confirmed (code) |
| `0x00235b68` | `Human_SetFastClimber` | `HuSetFastClimber`: a human flag on or off. | confirmed (code) |
| `0x00235bc8` | `Human_SetUngrabbable` | `HuSetUngrabbable`: human flag 0x40. | confirmed (code) |
| `0x00235c28` | `Human_SetUngroundable` | `HuSetUngroundable`: human flag 0x80. | confirmed (code) |
| `0x00235c88` | `Human_SetUnstunnable` | `HuSetUnstunnable`: human flag 0x100. | confirmed (code) |
| `0x00235ce8` | `Human_SetIncreasedReact` | `HuSetIncreasedReact`: human flag 0x200000. | confirmed (code) |
| `0x00235d50` | `Human_SetReducedReact` | `HuSetReducedReact`: human flag 0x200. | confirmed (code) |
| `0x00235db0` | `Human_SetRevivable` | `HuSetRevivable`: a human flag on or off. | confirmed (code) |
| `0x00235e10` | `Human_SetDoubleDamage` | `HuSetDoubleDamage`: human flag 0x4000. | confirmed (code) |
| `0x00235e70` | `Human_SetUnarrestable` | `HuSetUnarrestable`: human flag 0x8000, removing a pending arrest prompt. | confirmed (code) |
| `0x00235f10` | `Cfg_SetClimbWithGhetto` | `CfgClimbWithGhetto`: sets 0x00510250. | confirmed (code) |
| `0x00235f20` | `Human_SetOnFire` | `HuSetOnFire`: ignites or extinguishes the human. | confirmed (code) |
| `0x00235f80` | `HumanHandle_IsOnFire` | `HuIsOnFire`: state 0x4000. | confirmed (code) |
| `0x00235fc0` | `Human_SetFireProof` | `HuSetFireProof`: human flag 0x800000, extinguishing when set. | confirmed (code) |
| `0x002360f8` | `Human_ScriptDrop` | `HuDrop`: knocks off the hat, drops the held object and gear as asked. | confirmed (code) |
| `0x00236188` | `Human_SetUnlockedGear` | `HuAttachGear`: attaches or removes the unlocked gear. | confirmed (code) |
| `0x002361e8` | `Cfg_SetGearData` | `CfgGearData`: fills the gear transforms at 0x00715480. | confirmed (code) |
| `0x00236420` | `Cfg_SetButtonHeldFrames` | `CfgButtonHeldFrames`: frames for a held button, clamped to 2-7 (0x0050b708). | confirmed (code) |
| `0x00236450` | `Cfg_SetGrabCounterWithAttack` | `CfgGrabCounterWithAttack`: sets 0x00510254. | confirmed (code) |
| `0x00236460` | `Cfg_SetPlayerRunButton` | `CfgPlayerRunButton`: sets 0x00510258. | confirmed (code) |
| `0x00236470` | `Cfg_SetStickDeflection` | `CfgStickDeflection`: sets 0x005102e8. | confirmed (code) |
| `0x00236480` | `Cfg_SetPlayerCombatWalkOnly` | `CfgPlayerCombatWalkOnly`: sets 0x0051031c. | confirmed (code) |
| `0x00236490` | `Cfg_SetQueueGrappleCombos` | `CfgQueueGrappleCombos`: sets 0x0051025c. | confirmed (code) |
| `0x002364d8` | `Cfg_SetAttackFromIdle` | `CfgAttackFromIdle`: sets 0x00510270. | confirmed (code) |
| `0x002364e8` | `Cfg_SetDamageEndurance` | `CfgDamageEndurance`: sets 0x00510274. | confirmed (code) |
| `0x002364f8` | `Cfg_SetPowerEndurance` | `CfgPowerEndurance`: sets 0x00510278. | confirmed (code) |
| `0x00236978` | `Human_ForceEnableReticule` | `HuForceEnableReticule`: sets 0x005104f8. | confirmed (code) |
| `0x002369b8` | `Human_SetRageFrac` | `HuSetRageFrac`: sets the rage meter to a fraction of its maximum. | confirmed (code) |
| `0x00236a68` | `Human_SetFullRage` | `HuSetFullRage`: fills the rage meter. | confirmed (code) |
| `0x00236ad0` | `Human_SetPreventRage` | `HuSetPreventRage`: human flag 0x2000000. | confirmed (code) |
| `0x00236b38` | `Human_SetLockedRage` | `HuSetLockedRage`: human flag 0x100000 and the rage meter held where it is. | confirmed (code) |
| `0x00236be0` | `Rage_ResetConfig` | At level unload: clears the stereo, lock-pick and rage handlers and the rage handler table 0x006b6810. | confirmed (code) |
| `0x00236c28` | `Cfg_SetRagePoints` | `CfgRagePoints`: three values at 0x005108d8-0x005108e4. | confirmed (code) |
| `0x00236c48` | `Cfg_SetRagePowerMode` | `CfgRagePowerMode`: sets 0x00510298. | confirmed (code) |
| `0x00236c58` | `Cfg_SetRageHandlers` | `CfgRageHandlers`: interns the rage Lua handlers (0x00510290, 0x00510294, table 0x006b6810). | confirmed (code) |
| `0x00237128` | `Human_SetRageMode` | `HuSetRageMode`: starts or ends rage (human flag 0x80000) for a player. | confirmed (code) |
| `0x002371d8` | `Human_SetBlurMode` | `HuSetBlurMode`: a player's screen blur (0x0051029c). | confirmed (code) |
| `0x002372a8` | `Human_GiveCuffs` | `HuGiveCuffs`: adds handcuffs (inventory item 5 for a player). | confirmed (code) |
| `0x00237328` | `Human_SetKeepWeapon` | `HuSetKeepWeapon`: human flag 0x2000; Diego's boss goal uses it. | confirmed (code) |
| `0x00237388` | `Human_SetKeepHat` | `HuSetKeepHat`: human flag 0x10000000000. | confirmed (code) |
| `0x002373f8` | `Human_SetHardToStun` | `HuSetHardToStun`: human flag 0x1000000000. | confirmed (code) |
| `0x00237468` | `Human_SetNoThrowWeapon` | `HuSetNoThrowWeapon`: human flag 0x4000000000. | confirmed (code) |
| `0x002374d8` | `Human_SetNoEscape` | `HuSetNoEscape`: human flag 0x2000000000. | confirmed (code) |
| `0x00237548` | `Human_SetCanBlockKD` | `HuSetCanBlockKD`: human flag 0x400000000. | confirmed (code) |
| `0x002375b8` | `Human_SetBlockFromReact` | `HuSetBlockFromReact`: human flag 0x800000000. | confirmed (code) |
| `0x00237628` | `Human_SetWounded` | `HuSetWounded`: starts or ends the wounded state. | confirmed (code) |
| `0x002376a0` | `Human_SetWoundable` | `HuSetWoundable`: a human flag on or off. | confirmed (code) |
| `0x00237700` | `Human_SetArrested` | `HuSetArrested`: arrests or frees the human ([Crimes](crimes.md#arrest)); the bum goal uses it too. | confirmed (code) |
| `0x00237778` | `Human_SetConscious` | `HuSetConscious`: knocks out or wakes the human. | confirmed (code) |
| `0x002377f8` | `Human_Revive` | `HuRevive`: revives the human. | confirmed (code) |
| `0x00237848` | `Human_SetHealth` | `HuSetHealth`: health `+0x144`, at most the maximum. | confirmed (code) |
| `0x002378a8` | `Human_SetHealthPercent` | `HuSetHealthPercent`: health as a percentage of the maximum. | confirmed (code) |
| `0x00237958` | `Human_SetBlockJump` | `HuBlockJump`: human flag 0x10000000. | confirmed (code) |
| `0x002379c0` | `Human_SetBlockClimb` | `HuBlockClimb`: human flag 0x20000000. | confirmed (code) |
| `0x00237a28` | `Human_BlockGrab` | `HuBlockGrab`: human flags 0xc0000000 (both grabs). | confirmed (code) |
| `0x00237a98` | `Human_SetBlockFrontGrab` | `HuBlockFrontGrab`: human flag 0x40000000. | confirmed (code) |
| `0x00237b00` | `Human_SetBlockRearGrab` | `HuBlockRearGrab`: human flag 0x80000000. | confirmed (code) |
| `0x00237b70` | `Human_SetBlockTackle` | `HuBlockTackle`: human flag 0x100000000. | confirmed (code) |
| `0x00237be0` | `Human_SetMaxHealth` | `HuSetMaxHealth`: maximum health `+0x146`, and health with it. | confirmed (code) |
| `0x00237c38` | `Human_GetHealthPercent` | `HuGetHealthPercent`: health as a percentage of the maximum. | confirmed (code) |
| `0x00237c78` | `Human_SetMass` | `HuSetMass`: the body's mass. | confirmed (code) |
| `0x00237d08` | `Human_GetHeldObjectType` | `HuWhatAmIHolding`: the held object's type. | confirmed (code) |
| `0x00237d48` | `Human_GetHeldObject` | `HuGetHeldObject`: the held object's handle, or null. | confirmed (code) |
| `0x00237db8` | `Human_IsInCombat` | `HuInCombat`: whether the human fights. | confirmed (code) |
| `0x00237e00` | `Human_SetIdleAnim` | `HuSetIdleAnim`: overrides the idle clip. | confirmed (code) |
| `0x00237e70` | `Human_Kill` | `HuKill`: kills the human. | confirmed (code) |
| `0x00238030` | `Human_SetShadow` | `HuShadow`: the render instance's shadow switch (`+0x2b4`). | confirmed (code) |
| `0x00238078` | `Human_SetName` | `HuSetName`: the human's name. | confirmed (code) |
| `0x002380c0` | `Human_GetControlName` | `HuGetControlName`: the name of the control the human is under. | confirmed (code) |
| `0x00238158` | `Human_GetMoney` | `HuGetMoney`: money `+0x370`. | confirmed (code) |
| `0x002381f0` | `Human_RemoveItemInPocket` | `HuRemoveItemInPocket`: clears the pocket item (`+0x250`, `+0x254`). | confirmed (code) |
| `0x00238230` | `Human_SetCarriedItemName` | `HuSetCarriedItem`: the carried item name (`+0x278`). | confirmed (code) |
| `0x00238288` | `Human_CanSee` | `HuCanSee`: whether the human sees another. | confirmed (code) |
| `0x002383a0` | `Human_SetLOSRange` | `HuSetLOSRange`: sight range `+0x130`. | confirmed (code) |
| `0x002383e8` | `Human_SetHearRange` | `HuSetHearRange`: hearing range `+0x134`. | confirmed (code) |
| `0x00238430` | `Human_SetHelpHearRange` | `HuSetHelpHearRange`: the range for hearing calls for help `+0x138`. | confirmed (code) |
| `0x00238478` | `Human_ExitWorld` | `HuExitWorld`: takes the human out of the world. | confirmed (code) |
| `0x002384f8` | `Human_EnableController` | `HuEnableController`: the player record flag `+0x1b` (pad control on or off). | confirmed (code) |
| `0x002385f8` | `Human_PlaceHatOnHead` | `HuPlaceHatOnHead`: puts a hat object on the human's head. | confirmed (code) |
| `0x00238640` | `Human_SetPedReaction` | `HuSetPedReaction`: the pedestrian reaction `+0x270`. | confirmed (code) |
| `0x00238690` | `Human_UseAnim` | Plays an anim by id on the human (scripts and many AI goals). | confirmed (code) |
| `0x00238748` | `Human_UseAnyAnim` | `HuUseAnyAnim`: plays any clip by name. | confirmed (code) |
| `0x002387a8` | `Human_AreActionsBlocked` | `HuAreActionsBlocked`: whether the human cannot act. | confirmed (code) |
| `0x002387e8` | `Human_IsTagging` | `HuIsTagging`: state 0x2000000. | confirmed (code) |
| `0x00238828` | `Human_PlayDynPair` | `HuPlayDynPair`: a paired dynamic clip on two humans; the dealer goal uses it to sell. | confirmed (code) |
| `0x00238948` | `Human_PlayDynAnim` | `HuPlayDynAnim`: a dynamic clip on the human. | confirmed (code) |
| `0x002389e8` | `Human_PlayDynamicAnim` | `HuPlayDynamicAnim`: a dynamic clip by name. | confirmed (code) |
| `0x00238a88` | `Human_AttachSpinningIcon` | `HuAttachSpinningIcon`: the [spinning icon](characters-appearance.md#spinning-icon) over the human (`+0x360`); AI crime and dealer goals use it. | confirmed (code) |
| `0x00238ae0` | `Human_RemoveSpinningIcon` | `HuRemoveSpinningIcon`: removes it. | confirmed (code) |
| `0x00238b18` | `Human_SetSpinningIconColor` | `HuSetSpinningIconColor`: messages the icon's colour. | confirmed (code) |
| `0x00238bd8` | `Human_StartWorkout` | `HuWorkout`: starts a workout on the human (Human_StartWorkout state, start callback). | confirmed (code) |
| `0x00238cd8` | `Human_SetWorkoutBlend` | `HuSetWorkoutBlend`: the workout blend `+0x670`. | confirmed (code) |
| `0x00238d30` | `Human_StopWorkout` | `HuStopWorkout`: ends the workout and its clip. | confirmed (code) |
| `0x00238f10` | `Cfg_SetTagStartCallback` | `CfgTagStartCallback`: interns the tag-start Lua callback (0x006b6870). | confirmed (code) |
| `0x00239080` | `Human_SetTagColour` | `HuTagColor`: the tag colour `+0x640`. | confirmed (code) |
| `0x00239188` | `Tag_SetPattern` | `HuTagPattern`: the tag's button pattern. | confirmed (code) |
| `0x002391a8` | `Human_SetTagDifficulty` | `HuTagDifficulty`: the three tag difficulty values 0x0051093c-0x00510944. | confirmed (code) |
| `0x002391c0` | `Human_SetIdleDialogMaxIter` | `HuSetIdleDialogMaxIter`: the idle-dialogue line count `+0x197`. | confirmed (code) |
| `0x00239200` | `Human_EnableSpeaking` | `HuEnableSpeaking`: switch `+0x198`. | confirmed (code) |
| `0x00239240` | `Human_EnableSoundCommands` | `HuEnableSoundCommands`: switch `+0x199`, which every speech path checks. | confirmed (code) |
| `0x00239280` | `Human_EnableOnFireSpeech` | `HuEnableOnFire`: the on-fire speech switch `+0x19b`. | confirmed (code) |
| `0x002392c0` | `Human_EnableTagDoneSpeech` | `HuEnableTagDone`: the tag-done speech switch `+0x19a`. | confirmed (code) |
| `0x00239300` | `Human_EnableTagCheer` | `HuEnableTagCheer`: the tag-cheer switch `+0x19c`. | confirmed (code) |
| `0x00239340` | `Human_Say` | `HuSay`: Human_Speak with the default arguments. | confirmed (code) |
| `0x00239370` | `Human_Speak` | `HuSpeak`: plays a named speech line on the human with an optional Lua end callback (interned through the script system). | confirmed (code) |
| `0x00239558` | `Human_ShutUp` | `HuShutUp`: stops the human's line; the roof boss banter uses it. | confirmed (code) |
| `0x002395a0` | `Human_SpeakInterrupt` | `HuSpeakNI`: the variant of `HuSpeak` that Human_ActionDialog and AI dialogue call. | confirmed (code) |
| `0x00239788` | `Human_ActionDialog` | `HuActionDialog`: a line spoken to a partner (`+0x18c`), whom the speaker faces. | confirmed (code) |
| `0x00239850` | `Human_GetHUD` | `HuGetHUD`: the HUD slot of the human's player (`+0x380`). | confirmed (code) |
| `0x00239888` | `GameState_SetWarChiefHUD` | `HuSetWarChiefHUD`: the game state's war-chief HUD switch. | confirmed (code) |
| `0x002398b0` | `Human_SetWarChief` | `HuSetWarChief`: makes the human the war chief (the commanding player), re-binding player slots; GameState_SyncPlayers uses it. | confirmed (code) |
| `0x00239e08` | `GameState_SwitchPlayer` | `HuSwitchPlayer`: switches the controlled player in the game state. | confirmed (code) |
| `0x00239e30` | `Human_MarkReachable` | `HuMarkReachable`: byte `+0x11e`. | confirmed (code) |
| `0x00239e78` | `Human_SetMugCallback` | `HuSetMugCallback`: interns the mug Lua callback into `+0x5a4` ([Crimes](crimes.md#mugging)). | confirmed (code) |
| `0x00239ee0` | `Human_SetMug` | `HuSetMug`: the mug settings `+0x5b0`. | confirmed (code) |
| `0x0023a058` | `Human_TeleportFollowers` | `HuTeleportFollowers`: moves the human's gang followers (brain `+0x20c`) to it. | confirmed (code) |
| `0x0023a210` | `Human_SetNormalMode` | `HuSetNormalMode`: clears the special modes; AI goals use it. | confirmed (code) |
| `0x0023a258` | `Human_SetRubberNeck` | `HuRubberNeck`: human flag 0x1000000 on or off. | confirmed (code) |
| `0x0023a2c0` | `Human_SetBlockLook` | `HuBlockLook`: human flag 0x1000 on or off. | confirmed (code) |
| `0x0023a328` | `Human_SetForceLook` | `HuForceLook`: human flag 0x10000 on or off. | confirmed (code) |
| `0x0023a390` | `Human_SetLookPos` | `HuSetLookPos`: a point for the head to look at. | confirmed (code) |
| `0x0023a3b8` | `Human_SetLookTarget` | `HuSetLookTarget`: an object for the head to look at. | confirmed (code) |
| `0x0023a460` | `Human_ClearLook_Stub` | `HuClearLook`: empty in this build. | confirmed (code) |
| `0x0023a468` | `Human_IsAimingAt` | `HuIsAimingAt`: a player's aim target `+0x634` is the given object. | confirmed (code) |
| `0x0023a4e0` | `SetDeathTimer` | `SetDeathTimer`: three death-timer values at 0x0051078c-0x00510794. | confirmed (code) |
| `0x0023a540` | `Cfg_SetCombo` | `CfgCombo`: sets 0x005102b0. | confirmed (code) |
| `0x0023a550` | `Cfg_SetSnap` | `CfgSnap`: sets 0x005102b4. | confirmed (code) |
| `0x0023a560` | `Cfg_SetButtonMash` | `CfgButtonMash`: the mash target, decay and gain ([Crimes](crimes.md#uncuffing)). | confirmed (code) |
| `0x0023a588` | `Cfg_SetStrafe` | `CfgStrafe`: sets 0x005104a8. | confirmed (code) |
| `0x0023a598` | `Cfg_SetAutoLock` | `CfgAutoLock`: sets 0x005104b4. | confirmed (code) |
| `0x0023a5a8` | `Cfg_SetAutoLockAndCombat` | `CfgAutoLockAndCombat`: sets 0x005104b8. | confirmed (code) |
| `0x0023a5b8` | `Cfg_SetAutoCombat` | `CfgAutoCombat`: sets 0x005104b0. | confirmed (code) |
| `0x0023a5c8` | `Cfg_SetLockOn` | `CfgLockOn`: sets 0x005104ac. | confirmed (code) |
| `0x0023a5d8` | `Cfg_SetCombatTimeout` | `CfgCombatTimeout`: sets 0x00510994. | confirmed (code) |
| `0x0023a5e8` | `Cfg_SetStrafeRStick` | `CfgStrafeRStick`: sets 0x005104d0. | confirmed (code) |
| `0x0023a5f8` | `Cfg_SetTurnRate` | `CfgTurnRate`: sets 0x00510304, 0x00510308, 0x0051030c, 0x00510310, 0x00510314, 0x00510318. | confirmed (code) |
| `0x0023a630` | `Cfg_SetCombatDistances` | `CfgDistances`: sets 0x005104bc, 0x005104c0, 0x005104c4. | confirmed (code) |
| `0x0023a658` | `Cfg_SetBurnRates` | `CfgBurnRates`: sets 0x005101e4, 0x005101e8, 0x005101ec, 0x005101f0, 0x005101f4. | confirmed (code) |
| `0x0023a7a0` | `Cfg_SetTurnRates` | `CfgSetTurnRates`: sets 0x005101b0, 0x005101b8, 0x005101c0, 0x005101c8, 0x005101d0, 0x005101d8. | confirmed (code) |
| `0x0023a970` | `Cfg_SetJumpIsAction` | `CfgJumpIsAction`: sets 0x0051099c. | confirmed (code) |
| `0x0023a980` | `Cfg_SetPickupIsAction` | `CfgPickupIsAction`: sets 0x005109a0. | confirmed (code) |
| `0x0023a990` | `Cfg_SetPickupIsGrab` | `CfgPickupIsGrab`: sets 0x005109a4. | confirmed (code) |
| `0x0023a9a0` | `Cfg_SetBurnTime` | `CfgBurnTime`: sets 0x00510220. | confirmed (code) |
| `0x0023ad88` | `Human_SetVoiceIndex` | `HuSetVoiceIndex`: the voice `+0x3b0`. | confirmed (code) |
| `0x0023adc8` | `Human_GetVoiceIndex` | `HuGetVoiceIndex`: the voice `+0x3b0`. | confirmed (code) |
| `0x0023ae00` | `Human_SetStateRespVoiceIndex` | `HuSetStateRespVoiceIndex`: the state-response voice `+0x3b4`. | confirmed (code) |
| `0x0023ae40` | `Human_LockPad` | `HuLockPad`: the player record's pad lock `+0x1e`. | confirmed (code) |
| `0x0023ae90` | `Human_LockPadMovement` | `HuLockPadMovement`: the player record's movement lock `+0x1f`. | confirmed (code) |
| `0x0023aee0` | `Human_StartButtonMiniGame` | `HuButtonMiniGame`: starts mode 5 of the mini-game record with its buttons, times and Lua callback. | confirmed (code) |
| `0x0023b0e0` | `Human_SetScale` | `HuSetScale`: the human's model scale. | confirmed (code) |
| `0x0023b128` | `CNS_SetMissionInfoEnabled` | `CNSEnableMissionInfo`: sets 0x005109ac. | confirmed (code) |
| `0x0023b138` | `Human_TeleportNear` | `HuTeleportNearHuman`: puts the human at a free point 3.6 m from another human, keeping his own facing ([below](#teleport-near)). | confirmed (code) |
| `0x0023b778` | `Human_SetSlowMo` | `HuSetSlowMo`: a player's slow motion (`+0x3bb`, the time factor 0x005102cc). | confirmed (code) |

## Data

The script functions write the human's fields listed on [the human object](characters/index.md#the-human-object) and the
record [the record](characters/index.md#the-record).

## Behaviour

The behaviour each function starts is on the topic page of its subject; the table under Map names the function and says
what it does. The sections below have no other page; `Human_Heal`'s is [The flash](combat-rage.md#flash).

### HuIsAlive {#hu-is-alive}

Read in Ghidra: confirmed (code) where no other level is given.

`Human_IsAlive` (`0x00235628`) answers true for a human that `Human_IsDownOrDead` (`0x00227e60`) does not count and
that `Human_IsUp` (`0x0029ce40`) does. Down or dead is any record state of the mask `0x180050000`: dead
(`0x100000000`), dying (`0x80000000`), knocked out (`0x40000`) and wounded or tackled (`0x10000`), or no record at
`+0xd4`. Not up is state `0x100000000`. Cuffed (`0x20000`) is in neither mask, so **an arrested human is alive** to the
scripts, and a wounded one is not. A handle that names no human answers nil. Confirmed (code) at the three addresses.
On player 1 in `level99`, `HuIsAlive` answered 1 while he was arrested and nil while wounded, and 1 again once the
wound ended; the wound left 25 % health (confirmed (runtime), run r-20261128-script-heal-states).

### HuRevive and Human_Heal {#hu-revive}

Read in Ghidra: confirmed (code) where no other level is given.

`Human_Revive` (`0x002377f8`) is `Human_Heal(human, 1)` and then state `0x20000000` (the anim state chosen again);
nothing else. It calls no part of the normal mode ([The reset at the join](scenes.md#join-reset)), so a grab, a stun,
rage or a knockdown the human is in stay as they are. Confirmed (code) at `0x002377f8`.

`Human_Heal` (`0x0022eb40`), the heal the revive, the flash, the uncuffing and the Armies game-over toggle share,
is described step by step under [The flash](combat-rage.md#flash).

### HuSetWounded {#hu-wounded}

Read in Ghidra: confirmed (code) where no other level is given.

`Human_SetWounded(human, on)` (`0x00237628`) starts the wound for `on` = 1 exactly and ends it for anything else, then
sets state `0x20000000`. Confirmed (code).

`Human_StartWounded` (`0x0022fa78`), for a human with a record: states `0x14800f` cleared (the stance bits, block
`0x8000`, knocked out `0x40000` and stunned `0x100000`). Only when not wounded already: everything let go
(`Human_ReleaseAll`: the pair broken, a mini-game ended, the held object dropped, rage ended, style `0x12`), the held
object dropped, every move style popped and the wounded style `0xd` pushed, record `+0xf0` stamped now + 14 s, health a
quarter of the maximum (rounded toward zero) and body flag `BLOCKHUMANS` off. Then always: state `0x10000`, push weight
(attribute 7, vtable `+0xe4` with 1e9 and 7) 1e9, and the brain's knocked-out event (`Brain_OnKnockedOut(brain, 0)`).
Confirmed (code).

`Human_EndWounded` (`0x0022fc00`): state `0x10000` cleared, push weight 1.0, style `0xd` popped when on top (else
removed), and body shape 2 enabled again (the body's vtable `+0x2c` with 2, then the shape's `+0x1c` with 1), as
`Human_WakeUp` does. It does not give `BLOCKHUMANS` back. Confirmed (code).

### `HuSetConscious` {#set-conscious}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Human_SetConscious` (`0x00237778`), confirmed (code) at that address: for a handle that names a human, `false` calls
`Human_KnockOut(h, 0)` (`0x00230328`) and then `Player_SetStanceTimer` (`0x002800b8`,
which writes record `+0xfc` only while the setting `0x00510994` is positive; it is −1, so nothing); `true` calls
`Human_WakeUp(h, 0)` (`0x00230598`). Both then set state flag `0x20000000`, which makes the next
`Human_ChooseAnimState` build the anim state again ([Clip selection](characters-animation.md#clip-selection)). There
is no test of the state before either call.

`Human_KnockOut(h, 0)`, confirmed (code) at `0x00230328`, does nothing for a human with no record or already knocked
out (`0x40000`). Otherwise it lets go of everything (`Human_ReleaseAll`), stops his speech, takes `BLOCKHUMANS` (body
flag `0x4`) away and sets body flag `0x80000000`, stamps the wake time (record `+0xf0`, now + 14 s), gives the push
weight 1e9, turns a player's slow motion off, and, unless he is dead or wounded (`0x100050000`), sets `0x40000` and
calls `Brain_OnKnockedOut` (`0x0028c3b0`; [the brain stops](ai/index.md)): never `Brain_SetDead`. A member of a
player's gang (the brain's gang `+0x20c` equal to a player's) whose brain is of type 3 adds a harmony statistic, and
outside Armies and Rumble levels shows the `dyn_cross` icon over him. It ends by setting `0x40000` whatever the state
and clearing `0x8000` ([Who sets the bit](combat-reactions.md#knocked-out-bit)).

`Human_WakeUp(h, 0)`, confirmed (code) at `0x00230598`: switches his body's shape 2 back on, removes the `dyn_cross`
icon when he has it, pops move style `0x12` (or removes it when it is not on top), clears `0x1c8007`, puts the push
weight back to 1 and resumes the brain (`Brain_EnableAndResume`). It does not give `BLOCKHUMANS` back.

**In Coney** (`ai::ScriptedHumans::setConscious`, `repo:src/ai/scripted_humans.cpp`): the knock-out does nothing for
a human knocked out already (`Human::knockedOut()`: a script's mark or a health-out knock-out, Coney's `0x40000`);
otherwise it sets the script's
knocked-out mark (whatever the state, as the original's last step), takes `BLOCKHUMANS` away, ends a player's slow
motion, breaks a pair, and for a human not down or dead stops his brain as `Brain_OnKnockedOut` does
(`Brain::knockOut()`) and sends him event 18 with the target he had, as `Brains::destroy()` does for the same call;
the wake clears the mark and resumes the brain (`Brain::wakeUp()`). `Brains::reportDowns()`, which wakes a brain whose
health is back, leaves a brain the script knocked out. **Stand-ins**: of `Human_ReleaseAll` only the pair is broken
(no mini-game, held object, rage or move-style reset); the speech is not stopped; the 14 s wake time is not kept; the
cross icon and the harmony statistic are not shown or counted; the push weight is derived from the states
(`Human::pushWeight()`), which do not include the knock-out; and state `0x20000000` has no counterpart, as Coney picks
the anim state every update.

### `HuTeleportNearHuman` {#teleport-near}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Human_TeleportNear(human, near)` (`0x0023b138`), confirmed (code) at that address and its disassembly:

1. Both handles must name humans and differ. Let *rA* and *rB* be their capsule radii (`Human_GetCapsuleRadius`,
   `0x00219860`, 0.35 m × scale) and *c* = *rB* + 2 *rA*.
2. When the squared distance between them **in plan** is below 12.96 (3.6 m), nothing happens.
3. The humans within *rA* + 3.6 m of `near` (in 3D) are listed by `Humans_FindAhead` (`0x002274a8`, at most 32,
   `near` left out, each with its squared 3D distance to `near` as its key) and sorted by key, largest first
   (`SortByKeyDescending`, `0x003868f8`). The 32 is the list buffer's size, filled in table order, so a human past
   the 32nd is never tested. Coney lifts it past 60 humans (rule: every human in reach is listed,
   [Crowd rules](../engine/crowd-rules.md#lists)); up to 60 it keeps 32, and so does the engine limits preset.
4. 32 headings are tried, 11.25° apart (π/16), starting at (game time & 31) and counting up modulo 32. Heading *k*
   gives the direction (sin, cos, 0) of *k* π/16 (`Vec_FromHeading`, `0x00335e40`) and the point `near` + 3.6 m along
   it.
5. When `near` has a nav polygon and the line from him to the point is not clear (`Nav_LineClearWithin`,
   `0x0024f718`, mask 0), its out value (the fraction along the line where the nearest nav edge crosses it) less *rA*
   is compared with *c*²: below it the heading is refused; otherwise the point is moved to `near` + that value along
   the direction. The fraction is at most 1, so for a human of scale 0.85 or more the heading is always refused
   (inferred from the comparison).
6. Each listed human *H* whose squared distance in plan to the point is below *rH* + *rA* (a squared distance against
   a plain sum, as written) refuses the heading when his key is below (*c* + *rH*)²; otherwise the point moves to
   `near` + (√key − *rH* − *rA*) along the direction, and the humans after him are tested against the new point.
7. The first heading not refused is used: when the world exists, a ray straight down from the point at `near`'s
   height + his capsule height (`0x00219890`, 2 m × scale) + 3 m, of that height + 6 m, drops the point to 0.01 m
   above what it hits (`WorldManager_RayCast`, direction `0x00511770`). The human's transform is read (vtable `+0xac`),
   its position replaced and set back (vtable `+0x6c`, `Human_SetTransform`, [a placement](teleports.md#placement)):
   he **keeps his own facing**. When every heading is refused, nothing happens. No brain type is left out, so a
   player is moved too.

**In Coney** (`ai::ScriptedHumans::teleportNear`): the steps above, with the humans every brain of the level
(`ai::Brains`) in its own order for the original's table order, the nav line through `ai::RoutePlanner::lineClear()`
and the ray through the level's collision mesh. **Stand-ins**: `lineClear()` gives no fraction, so a line not clear
refuses the heading whatever the scale (step 5); the ray meets the collision mesh only, not the world's objects.
**Limit**: the list of 32 (step 3) is `crowd::teleportListCap()`, marked `@coney-only limit:`: 32 up to 60 humans
and under the engine limits preset, every human in reach past 60.

## In Coney {#coneys-implementation}

- **The scripts' hold on a human** ([character bindings](../references/bindings/character.md)): the flag word
  `+0xe0` is `human::flag` (`src/human/human_flags.h`), kept by the fighter and across a respawn; combat reads the
  bits [Combat](combat/index.md#human-flags) lists, `0x2` gates the running climb and `0x4000000` keeps stamina full. A
  player starts with `0x2`, `0x4`, `0x400`, `0x2000000` and (**Coney choice**, for scenes with no level script)
  `0x20000000000`. The rest of what the scripts set (arrest, pad lock, disabled commands, money, carried item,
  callbacks, icon, held object, head-look, animation overrides) is `human::ScriptState` (`src/human/script_state.h`);
  the bindings are `src/scripting/human_bindings.*`, acting through `ai::ScriptedHumans`. **Stand-ins**: an arrested
  human only stands still; the icon, the object in the hand, the head-look and `HuUseAnim`'s clips other than the
  idle's (slot 0, which plays: [Rumble](rumble.md#coney)) are kept, not drawn or played; `HuSetPushable` is kept
  (bodies do not push each other); `HuChangePlayerGang` and `WCIssueCommand` only
  note the gang and command. `LoadBumAnims` requests or releases the bum animations' table, in order, as
  `SetDynamicAnimation` does each: the file listed twice is one entry, so twelve files are listed.
- **The heals, the normal mode, the wound and the arrest** (task 685, `src/human/human_script.cpp`,
  `src/human/fighter_script.cpp`): `HuIsAlive` is `Human::aliveAndUp()`, the down mask read from Coney's states
  (`Human::downOrDead()`), so a cuffed human answers alive and a wounded one not; the AI's and the HUD's own test,
  which also leaves out the cuffed, stays `Human::alive()`. `HuRevive` is `Human::heal(true)` and then, for a human
  who was knocked out or out of health, the stand into his idle; a stun, rage, a grab or a knockdown are left alone.
  A human a script knocked out also gets his brain back (`Brain::wakeUp()`, as `Human_WakeUp` resumes it), which his
  health back alone would not give him ([HuSetConscious](#set-conscious)).
  `Human::heal(bool)` ends a wound, wakes a knocked-out human into the fight stance (a player's: an AI human's stance
  is its fight goal's), fills the health or adds half the maximum, capped, and makes the body again; the flash keeps
  its own health and wound steps and calls only the body (`Human::heal()`). `HuSetNormalMode` ends the player's
  rage (`ai::ScriptedHumans`), cancels a throw's aim, breaks a pair in full, ends a wound, a grab, the ground and a
  stun, takes a player out of the stance, releases the carried props and ends the slow motion; in full a health at or
  below the pending damage becomes 1 and the human stands in its idle. An arrest is no longer ended by it. The wound
  weighs 1e9 (`Human::pushWeight()`) and the arrest no longer ends a grab or rage; the release lets go of the carried
  props. **Not ported**: the held object's drops (#711), the move styles of the wound and the arrest (#990), the
  heal's linked object (#1640), the ally's follow after a release (#677), and the heal's glow colour and the normal
  mode's overlay task, climb body and picked object (#1718).
- **The hub's hold on a human** (`level95`; `src/scripting/hub_bindings.*`, acting through `ai::ScriptedHub`): money,
  death, character class and voice set are read; the scale, name, cuffs, muggable, pedestrian reaction,
  unarrestable, combat mode, gear and tackle block are set. **Workouts** (`HuWorkout`): the enter clip into a loop
  blend of the three workout clips, the effort pumped with cross and decaying by `CfgWorkoutParams`' tables, a
  repetition each wrap of the loop, the end on triangle or when worn down, then the exit clip
  ([Workouts](workouts.md#coney), `human::WorkoutEffort`). **Stand-ins**: another human's equipment is reached within
  1.5 m; no dynamic clip plays; combat mode and the gear are kept, their context actions not built.

**The fifth and sixth missions' switches** (2026-10-06): `HuBlockJump` sets flag `0x10000000`, which makes
`Human::tryJump` refuse (a climb or a context action still takes triangle); `HuSetNoReact` sets `0x800`, the same bit
the AI's block goal sets (`Fighter::hitReactionsOff()` now reads the flag word); `HuSetAutoCombat` keeps
`0x200000000000` with no reader. `HuApplyDamageModifier` rewrites a human's attack damages from its class table × the
factor, rounded, then a player's Warrior class percentage (`Human::applyDamageModifier`; an entry that rounds to 0
keeps its damage). **Coney's stand-ins**: `HuEnableTagCheer`, `HuSetInterrogation` and `HuUseAnyAnim` are kept on the
human's `ScriptState` (no crew comment on a slip, no interrogation shake-down, no clip played; `HuUseAnyAnim` keeps
up to seven overrides apart from `HuUseAnim`'s four and answers whether the file was requested); `HuHasHat` answers
NilHandle, as Coney's humans wear no hat objects; `HuClearLook` and `KillHumans` do nothing, as in the original.
`CfgSetMaxThrowError` keeps its two errors in radians (`CharacterRules::maxThrowError`); Coney's AI throws do not
read them yet.

## Open questions

- **The workout's reach and timing** (answered): the scripts make the player his own target, so the reach always
  passes, and a repetition is each wrap of the loop blend's leading clip ([Workouts](workouts.md#repetitions)).
- Where god mode (`0x10`) is tested: Coney drops the damage and still plays the reaction.
- What the arrest (state `0x20000`) plays and allows. (Which states `HuIsAlive`'s mask `0x180050000` names is answered
  in [HuIsAlive](#hu-is-alive).)

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.
