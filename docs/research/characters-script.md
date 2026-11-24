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
| `0x0023b138` | `Human_TeleportNear` | `HuTeleportNearHuman`: puts the human at a free point near another human, facing it. | confirmed (code) |
| `0x0023b778` | `Human_SetSlowMo` | `HuSetSlowMo`: a player's slow motion (`+0x3bb`, the time factor 0x005102cc). | confirmed (code) |

## Data

The script functions write the human's fields listed on [the human object](characters/index.md#the-human-object) and the
record [the record](characters/index.md#the-record).

## Behaviour

The behaviour each function starts is on the topic page of its subject; the table under Map names the function and says
what it does.

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
  (bodies do not push each other); `HuDropWeapon` lets go of any object; `HuChangePlayerGang` and `WCIssueCommand` only
  note the gang and command. `LoadBumAnims` requests or releases the bum animations' table, in order, as
  `SetDynamicAnimation` does each: the file listed twice is one entry, so twelve files are listed.
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
- What the arrest (state `0x20000`) plays and allows, and which human states `HuIsAlive`'s mask `0x180050000` names.

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.
