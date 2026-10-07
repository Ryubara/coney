# Script types: the code of every type

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07); each row states its evidence level.

## Purpose

The game's world objects, particles, lights and glass panes are all tasks of a named **script type**
([Particles and script types](particles.md) describes the table, the spawning by name and the message scratch). This
page is the code index of those types: for each one what its initialiser, update and message handler do, the data
record it keeps, and the helpers they share. It covers the stretch of `TaskEngine/` from `0x003a8698` to `0x00408ac8`
([Source map](source-map.md#position)). The engine core below it (the task manager, the object, car, glass, light and
scene managers) is on [Tasks](tasks.md) and the pages it links.

## The type table {#type-table}

270 records of 20 bytes at `0x00512f28`: `{name, init, update, message, flags}`, confirmed (code) (read by
`ScriptType_Find` `0x003c55e8` and `ScriptType_Get` `0x003c56c8`). The flags give the kind and so the manager that
makes the task: `0x10` particle system, `0x08` object behaviour, `0x04` light, `0x400` glass pane
(`ScriptType_CreateByName` `0x003c56f0`, [below](#type-helpers)). Several names share one set of functions; the
sections below say which. The full list, in table order (names from the executable; flags in hex):

| # | Name | Kind | Init | Update | Message |
| ---: | --- | --- | --- | --- | --- |
| 0 | `blo_splat` | particle | `0x003adcc0` | `0x003ae0f0` | `0x003ae0e8` |
| 1 | `blood_drop` | particle | `0x003af280` | `0x003af518` | `0x003af510` |
| 2 | `blood_mist` | particle | `0x003ae328` | `0x003ae520` | `0x003ae518` |
| 3 | `blood_splat_ground` | particle | `0x003ae708` | `0x003ae880` | `0x003ae878` |
| 4 | `blood_splatter` | particle | `0x003ae990` | `0x003aeb78` | `0x003aeb70` |
| 5 | `blood_spray` | particle | `0x003af590` | `0x003af728` | `0x003af720` |
| 6 | `bloosh` | particle | `0x003ae180` | `0x003ae2b0` | `0x003ae2a8` |
| 7 | `coplights_glow` | particle | `0x003d5618` | `0x003d5860` | `0x003d5788` |
| 8 | `coplights_lens_flare` | particle | `0x003d53c0` | `0x003d5538` | `0x003d54e0` |
| 9 | `dyn_animwave` | object | `0x003f0758` | `0x003f08c0` | `0x003f0898` |
| 10 | `dyn_bar_lamp` | object | `0x003cc3d0` | `0x003cc650` | `0x003cc510` |
| 11 | `dyn_blaster` | object | `0x003ac708` | `0x003ad440` | `0x003ac7e8` |
| 12 | `dyn_blocker` | object | `0x003ef980` | `0x003efb60` | `0x003efa20` |
| 13 | `dyn_breakable_light` | object | `0x003ea350` | `0x003ea530` | `0x003ea488` |
| 14 | `dyn_cashreg` | object | `0x003c04e0` | `0x003c0d98` | `0x003c06b0` |
| 15 | `dyn_cashreg_b` | object | `0x003bfbf0` | `0x003bffd0` | `0x003bfcc8` |
| 16 | `dyn_cbradio` | object | `0x003ee338` | `0x003ee838` | `0x003ee4a8` |
| 17 | `dyn_chand` | object | `0x003c1450` | `0x003c1b58` | `0x003c1610` |
| 18 | `dyn_chicken` | object | `0x00406458` | `0x00407068` | `0x00406e20` |
| 19 | `dyn_colasign` | object | `0x003ea7b0` | `0x003eaa30` | `0x003ea9c0` |
| 20 | `dyn_ctrl_box` | object | `0x003ee9d8` | `0x003eee68` | `0x003eeaa8` |
| 21 | `dyn_disco_a` | object | `0x003eb160` | `0x003eb2e8` | `0x003eb240` |
| 22 | `dyn_disco_b` | object | `0x003eb4d0` | `0x003eb620` | `0x003eb5b8` |
| 23 | `dyn_door_bar_bani` | object | `0x003b3250` | `0x003b40d0` | `0x003b33e8` |
| 24 | `dyn_door_barricade` | object | `0x003f6018` | `0x003f61a8` | `0x003f60f8` |
| 25 | `dyn_door_bnstr` | object | `0x003b4128` | `0x003b5440` | `0x003b42a8` |
| 26 | `dyn_door_chain_s` | object | `0x003b5498` | `0x003b57c8` | `0x003b55e8` |
| 27 | `dyn_door_fence` | object | `0x003b2f40` | `0x003b3220` | `0x003b3158` |
| 28 | `dyn_door_fence_o` | object | `0x003b57d0` | `0x003b61d8` | `0x003b5950` |
| 29 | `dyn_door_parapet` | object | `0x003b6220` | `0x003b74e0` | `0x003b63a0` |
| 30 | `dyn_door_sliding` | object | `0x003f5d10` | `0x003f5ec0` | `0x003f5ef0` |
| 31 | `dyn_scaffold` | object | `0x003c1250` | `0x003c1420` | `0x003c1318` |
| 32 | `dyn_door_swinging` | object | `0x003fb5f8` | `0x003fbba0` | `0x003fb8d0` |
| 33 | `dyn_exploding_car` | object | `0x003cc8b8` | `0x003ccf68` | `0x003cca88` |
| 34 | `dyn_fluor_swing` | object | `0x003cd7c0` | `0x003cdb88` | `0x003cdae8` |
| 35 | `dyn_icon` | object | `0x003e8fa0` | `0x003e92e0` | `0x003e9470` |
| 36 | `dyn_lizzies` | object | `0x003cdb90` | `0x003d17f8` | `0x003cdca8` |
| 37 | `dyn_lock_a` | object | `0x003eda58` | `0x003ee170` | `0x003edb28` |
| 38 | `dyn_manqheadred` | object | `0x003d1830` | `0x003d1f38` | `0x003d1908` |
| 39 | `dyn_masks` | object | `0x003b7538` | `0x003bf548` | `0x003bf1b8` |
| 40 | `dyn_molotv` | object | `0x00404b38` | `0x00405600` | `0x00404c48` |
| 41 | `dyn_motelneon` | object | `0x003eb7e8` | `0x003ebb70` | `0x003ebae8` |
| 42 | `dyn_neon_broken` | object | `0x003d2320` | `0x003d2820` | `0x003d2768` |
| 43 | `dyn_neon_onesided` | object | `0x003d2b68` | `0x003d2ed0` | `0x003d2e48` |
| 44 | `dyn_o_shore` | object | `0x003efb90` | `0x003efda8` | `0x003efd00` |
| 45 | `dyn_objective` | object | `0x003e98e8` | `0x003e9b08` | `0x003e9be0` |
| 46 | `dyn_on_off` | object | `0x003d3968` | `0x003d3c20` | `0x003d3af0` |
| 47 | `dyn_oneon_glow` | object | `0x003d31a0` | `0x003d34e0` | `0x003d3458` |
| 48 | `dyn_outwave` | object | `0x003f1388` | `0x003f1678` | `0x003f1650` |
| 49 | `dyn_pile` | object | `0x003d3c28` | `0x003d4058` | `0x003d3d30` |
| 50 | `dyn_rat` | object | `0x003f36a0` | `0x003f3770` | `0x003f3f30` |
| 51 | `dyn_skullglow` | object | `0x003ed3d0` | `0x003ed610` | `0x003ed5a0` |
| 52 | `dyn_table` | object | `0x003cb0c8` | `0x003cc158` | `0x003cc098` |
| 53 | `dyn_walktalk` | object | `0x003d4100` | `0x003d48e8` | `0x003d41f8` |
| 54 | `dyn_wash_a` | object | `0x003f0988` | `0x003f0b10` | `0x003f0ae8` |
| 55 | `dyn_wash_b` | object | `0x003f0e80` | `0x003f1008` | `0x003f0fe0` |
| 56 | `dyn_wave` | object | `0x003f01d8` | `0x003f0388` | `0x003f0360` |
| 57 | `dyn_woodbridge` | object | `0x003c0df0` | `0x003c0ec0` | `0x003c1148` |
| 58 | `fade_object` | object | `0x003c60d8` | `0x003c61d8` | `0x003c6c48` |
| 59 | `fir` | particle | `0x003b1c48` | `0x003b1cf0` | `0x003b1ef0` |
| 60 | `fir_group` | particle | `0x003b1ef8` | `0x003b1ff8` | `0x003b2178` |
| 61 | `glass_script` | glass | `0x003e29e8` | `0x003e3058` | `0x003e2d90` |
| 62 | `glasstest` | particle | `0x003e44e0` | `0x003e4838` | `0x003e4830` |
| 63 | `gun_light_flash` | light | `0x003d89a8` | `0x003d8a70` | `0x003d8a68` |
| 64 | `hat_object` | object | `0x003e74a8` | `0x003e7b88` | `0x003e7f20` |
| 65 | `hud_radar_dot` | particle | `0x003e5bf8` | `0x003e6008` | `0x003e5f20` |
| 66 | `hud_text_widget` | particle | `0x003e61d8` | `0x003e65b0` | `0x003e6498` |
| 67 | `hud_widget` | particle | `0x003e66c8` | `0x003e6c40` | `0x003e6b10` |
| 68 | `hud_widget_angled` | particle | `0x003e6e90` | `0x003e7378` | `0x003e7248` |
| 69 | `melee_weapon` | object | `0x003fd420` | `0x003fee68` | `0x003fead0` |
| 70 | `miniglass` | particle | `0x003e3d40` | `0x003e4058` | `0x003e4030` |
| 71 | `molotov_smoke` | particle | `0x004058c8` | `0x00405a08` | `0x00405a00` |
| 72 | `multi_strobe` | light | `0x003eaa60` | `0x003eab70` | `0x003eab08` |
| 73 | `on_off_light` | light | `0x003d37b0` | `0x003d38e0` | `0x003d3870` |
| 74 | `overhead_weapon` | object | `0x003ff6c8` | `0x00402e70` | `0x00402bf8` |
| 75 | `paint_splat` | particle | `0x003ec158` | `0x003ec3a0` | `0x003ec398` |
| 76 | `part_blood_spray` | particle | `0x003d4f60` | `0x003d50d0` | `0x003d50c8` |
| 77 | `part_coke_machine` | particle | `0x003d5310` | `0x003d53b8` | `0x003d53b0` |
| 78 | `part_copcar_lights` | particle | `0x003d5af0` | `0x003d6480` | `0x003d5f68` |
| 79 | `part_explosion` | particle | `0x003c3448` | `0x003c3bb0` | `0x003c3ba8` |
| 80 | `part_explosion_screen_shake` | particle | `0x003d70e8` | `0x003d71e8` | `0x003d71e0` |
| 81 | `part_fear` | particle | `0x003d7230` | `0x003d72c8` | `0x003d72c0` |
| 82 | `part_fire` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 83 | `part_fire_large` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 84 | `part_fire_large_ns` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 85 | `part_fire_light_large` | particle | `0x003c8808` | `0x003c8b20` | `0x003c8970` |
| 86 | `part_fire_light_medium` | particle | `0x003c8808` | `0x003c8b20` | `0x003c8970` |
| 87 | `part_fire_ns` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 88 | `part_fire_plume` | particle | `0x003d7b38` | `0x003d7cd8` | `0x003d7c38` |
| 89 | `part_fire_tiki` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 90 | `part_firebarrel` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 91 | `part_firebarrel_ns` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 92 | `part_firetruck_lights` | particle | `0x003d7678` | `0x003d7af0` | `0x003d7920` |
| 93 | `part_fog` | particle | `0x003cadd8` | `0x003caed8` | `0x003caff0` |
| 94 | `part_garbage_flies` | particle | `0x003d7e30` | `0x003d7f10` | `0x003d7f08` |
| 95 | `part_garbage_flies_ns` | particle | `0x003d8180` | `0x003d8238` | `0x003d8230` |
| 96 | `part_gasmeter` | particle | `0x003d8240` | `0x003d82e8` | `0x003d82e0` |
| 97 | `part_generator_sparks` | particle | `0x003d82f0` | `0x003d83d8` | `0x003d83d0` |
| 98 | `part_ghost_light` | particle | `0x003d8818` | `0x003d89a0` | `0x003d88d8` |
| 99 | `part_gun_flash` | particle | `0x003d8bd8` | `0x003d8da0` | `0x003d8d98` |
| 100 | `part_large_ac` | particle | `0x003d9178` | `0x003d9220` | `0x003d9218` |
| 101 | `part_large_ac_two` | particle | `0x003d9228` | `0x003d92d0` | `0x003d92c8` |
| 102 | `part_lava_light` | particle | `0x003d9508` | `0x003d9690` | `0x003d95c8` |
| 103 | `part_level2_subway` | particle | `0x003d98b0` | `0x003d9a48` | `0x003d9978` |
| 104 | `part_light_bugs` | particle | `0x003d9ca0` | `0x003d9d50` | `0x003d9d48` |
| 105 | `part_motorbike_gang` | particle | `0x003d9fc8` | `0x003da158` | `0x003da078` |
| 106 | `part_multi_strobe` | particle | `0x003eac30` | `0x003eada0` | `0x003eacc8` |
| 107 | `part_narrowflame` | particle | `0x003c84f0` | `0x003c87a0` | `0x003c8698` |
| 108 | `part_ominous_smoke` | particle | `0x003f7c78` | `0x003f7eb8` | `0x003f7e08` |
| 109 | `part_open_air_vent` | particle | `0x003da278` | `0x003da320` | `0x003da318` |
| 110 | `part_orange_neon` | particle | `0x003da328` | `0x003da420` | `0x003da418` |
| 111 | `part_pelham_subway` | particle | `0x003da428` | `0x003da5c0` | `0x003da4f0` |
| 112 | `part_pink_neon` | particle | `0x003da818` | `0x003da910` | `0x003da908` |
| 113 | `part_plaster_drop` | particle | `0x003ecf78` | `0x003ed3c8` | `0x003ed008` |
| 114 | `part_plaster_drop_ns` | particle | `0x003ecf78` | `0x003ed3c8` | `0x003ed008` |
| 115 | `part_raindrops` | particle | `0x003f30d8` | `0x003f3178` | `0x003f35d8` |
| 116 | `part_ridecart_sound` | particle | `0x003da940` | `0x003daab0` | `0x003da9e0` |
| 117 | `part_rotate_vent` | particle | `0x003dab08` | `0x003dabb0` | `0x003daba8` |
| 118 | `part_s_fire` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 119 | `part_s_shack_dust_puff` | particle | `0x003dac68` | `0x003dadb8` | `0x003dacf0` |
| 120 | `part_s_subway_sparks` | particle | `0x003db298` | `0x003db4e0` | `0x003db368` |
| 121 | `part_small_ac` | particle | `0x003dabb8` | `0x003dac60` | `0x003dac58` |
| 122 | `part_small_light` | particle | `0x003e81e0` | `0x003e8400` | `0x003e8290` |
| 123 | `part_smaller_light` | particle | `0x003e8e10` | `0x003e8f98` | `0x003e8eb0` |
| 124 | `part_spray_tag` | particle | `0x003fc600` | `0x003fca68` | `0x003fc8d8` |
| 125 | `part_squareflame_lrg` | particle | `0x003c84f0` | `0x003c87a0` | `0x003c8698` |
| 126 | `part_squareflame_med` | particle | `0x003c84f0` | `0x003c87a0` | `0x003c8698` |
| 127 | `part_squareflame_sml` | particle | `0x003c84f0` | `0x003c87a0` | `0x003c8698` |
| 128 | `part_steam` | particle | `0x003f69b8` | `0x003f6bf0` | `0x003f6b40` |
| 129 | `part_steam_huge` | particle | `0x003f6d70` | `0x003f6fb8` | `0x003f6f08` |
| 130 | `part_steam_large` | particle | `0x003f7138` | `0x003f7378` | `0x003f72c8` |
| 131 | `part_strobe` | particle | `0x003ed718` | `0x003ed890` | `0x003ed7b8` |
| 132 | `part_strobe_red` | particle | `0x003e85b8` | `0x003e8780` | `0x003e8678` |
| 133 | `part_subway_light` | particle | `0x003e8bb8` | `0x003e8d48` | `0x003e8ce0` |
| 134 | `part_torch_flame` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 135 | `part_torch_flame_ns` | particle | `0x003ca050` | `0x003ca138` | `0x003ca220` |
| 136 | `part_truck_sound` | particle | `0x003fcbe0` | `0x003fce60` | `0x003fccd8` |
| 137 | `part_truck_humans` | particle | `0x003fd0c0` | `0x003fd2b8` | `0x003fd1c0` |
| 138 | `part_train_sound` | particle | `0x003db518` | `0x003db690` | `0x003db5c0` |
| 139 | `part_train_splat` | particle | `0x003dc658` | `0x003dcb10` | `0x003dcb08` |
| 140 | `part_trans_five` | particle | `0x003dcb68` | `0x003dcc10` | `0x003dcc08` |
| 141 | `part_trans_four` | particle | `0x003dcc18` | `0x003dccc0` | `0x003dccb8` |
| 142 | `part_trans_one` | particle | `0x003dccc8` | `0x003dcd70` | `0x003dcd68` |
| 143 | `part_trans_three` | particle | `0x003dcd78` | `0x003dce20` | `0x003dce18` |
| 144 | `part_trans_two` | particle | `0x003dce28` | `0x003dced0` | `0x003dcec8` |
| 145 | `part_tv` | particle | `0x003dced8` | `0x003dd068` | `0x003dd060` |
| 146 | `part_tv_light` | particle | `0x003dd538` | `0x003dd648` | `0x003dd640` |
| 147 | `part_urine_spray` | particle | `0x003dd808` | `0x003dd938` | `0x003dd8b8` |
| 148 | `part_urine_stain2` | particle | `0x003ddb88` | `0x003ddc70` | `0x003ddc68` |
| 149 | `part_water` | particle | `0x003c2ab8` | `0x003c2ba8` | `0x003c2ba0` |
| 150 | `pee_pee` | particle | `0x003aa3e0` | `0x003aa530` | `0x003aa528` |
| 151 | `pee_tracer` | particle | `0x003aa0b0` | `0x003aa200` | `0x003aa1f8` |
| 152 | `pickup_item` | object | `0x003f17e0` | `0x003f1950` | `0x003f2070` |
| 153 | `power_mist` | particle | `0x003ab110` | `0x003ab2c8` | `0x003ab2c0` |
| 154 | `powerup_item` | object | `0x003f2700` | `0x003f2870` | `0x003f2c40` |
| 155 | `float_item` | object | `0x003ca3e0` | `0x003ca500` | `0x003ca560` |
| 156 | `puk_splat` | particle | `0x003a9910` | `0x003a9b88` | `0x003a9b80` |
| 157 | `rotating_object` | object | `0x003ddd70` | `0x003de3d8` | `0x003de0b8` |
| 158 | `rubble` | particle | `0x003f3f58` | `0x003f4188` | `0x003f45c0` |
| 159 | `simple_object` | object | `0x003eeea0` | `0x003ef840` | `0x003ef188` |
| 160 | `small_light` | light | `0x003e8020` | `0x003e81a8` | `0x003e80d0` |
| 161 | `spark` | particle | `0x003e0f50` | `0x003e1250` | `0x003e1248` |
| 162 | `spark_light_flash` | light | `0x003e1430` | `0x003e14f8` | `0x003e14f0` |
| 163 | `spawn_delayed` | particle | `0x003c1e58` | `0x003c1ff0` | `0x003c1fe8` |
| 164 | `spray_mist` | particle | `0x003aad68` | `0x003aaf28` | `0x003aaf20` |
| 165 | `strobe` | light | `0x003ed8b0` | `0x003ed9c0` | `0x003ed958` |
| 166 | `strober` | light | `0x003e8408` | `0x003e8518` | `0x003e84b0` |
| 167 | `sub_anim_notes` | particle | `0x003ab980` | `0x003abd08` | `0x003abd00` |
| 168 | `sub_anim_spark` | particle | `0x003ac148` | `0x003ac2f8` | `0x003ac2d0` |
| 169 | `sub_barlamp_glow` | particle | `0x003cc680` | `0x003cc858` | `0x003cc7e0` |
| 170 | `sub_blight` | light | `0x003de6e0` | `0x003de8f0` | `0x003de858` |
| 171 | `sub_blight_glow` | particle | `0x003de638` | `0x003de6d8` | `0x003de6d0` |
| 172 | `sub_blo` | particle | `0x003a9090` | `0x003a9270` | `0x003a9268` |
| 173 | `sub_blood_effect` | particle | `0x003af7a0` | `0x003afa08` | `0x003afa00` |
| 174 | `sub_blood_gout` | particle | `0x003dc018` | `0x003dc170` | `0x003dc168` |
| 175 | `sub_blood_gush` | particle | `0x003b00c8` | `0x003b01e8` | `0x003b01e0` |
| 176 | `sub_blood_spray` | particle | `0x003d4c10` | `0x003d4dd0` | `0x003d4dc8` |
| 177 | `sub_burn` | particle | `0x003c40a0` | `0x003c4280` | `0x003c4210` |
| 178 | `sub_car_damage` | particle | `0x003df698` | `0x003dfcb8` | `0x003df730` |
| 179 | `sub_car_rubble` | particle | `0x003ded88` | `0x003deef8` | `0x003deef0` |
| 180 | `sub_car_spark_emitter` | particle | `0x003df228` | `0x003df690` | `0x003df688` |
| 181 | `sub_car_sparks` | particle | `0x003df038` | `0x003df1b0` | `0x003df1a8` |
| 182 | `sub_car_steam` | particle | `0x003f7780` | `0x003f7838` | `0x003f7830` |
| 183 | `sub_car_steam_emitter` | particle | `0x003debd0` | `0x003dec88` | `0x003dec80` |
| 184 | `sub_coloured_glass` | particle | `0x003e30e0` | `0x003e33d0` | `0x003e33c8` |
| 185 | `sub_coloured_shards` | particle | `0x003e36c0` | `0x003e39f8` | `0x003e39f0` |
| 186 | `sub_debris` | particle | `0x003c21a8` | `0x003c24e8` | `0x003c2a48` |
| 187 | `sub_detergent` | particle | `0x003ff150` | `0x003ff5e8` | `0x003ff5c0` |
| 188 | `sub_dus` | particle | `0x003a9c78` | `0x003a9d98` | `0x003a9d90` |
| 189 | `sub_embers` | particle | `0x003c72d8` | `0x003c73c8` | `0x003c7550` |
| 190 | `sub_explode` | particle | `0x003c5350` | `0x003c54b8` | `0x003c5490` |
| 191 | `sub_explosion_embers` | particle | `0x003c3c08` | `0x003c3da8` | `0x003c3da0` |
| 192 | `sub_explosion_light` | light | `0x003c51b0` | `0x003c5270` | `0x003c5248` |
| 193 | `sub_explosion_group` | particle | `0x003d64d8` | `0x003d70e0` | `0x003d70d8` |
| 194 | `sub_fade_flame` | particle | `0x003c7de0` | `0x003c80d8` | `0x003c8090` |
| 195 | `sub_fire` | particle | `0x003c7840` | `0x003c7930` | `0x003c7d30` |
| 196 | `sub_fire_light` | light | `0x003dfcc0` | `0x003dfee0` | `0x003dfe18` |
| 197 | `sub_fire_smoke` | particle | `0x003dffc0` | `0x003e00a8` | `0x003e00a0` |
| 198 | `sub_fire2` | particle | `0x003c7840` | `0x003c7930` | `0x003c7d30` |
| 199 | `sub_fire2_light` | light | `0x003c6f58` | `0x003c71a8` | `0x003c70b0` |
| 200 | `sub_fire2_smoke` | particle | `0x003c7578` | `0x003c7640` | `0x003c7818` |
| 201 | `sub_fireball` | particle | `0x003c4a58` | `0x003c4c38` | `0x003c4c30` |
| 202 | `sub_fireball_emitter` | particle | `0x003c4580` | `0x003c4a50` | `0x003c4a48` |
| 203 | `sub_firetruck_light` | light | `0x003d72d0` | `0x003d7568` | `0x003d74a0` |
| 204 | `sub_flame_reflect` | particle | `0x003bfa08` | `0x003bfb80` | `0x003bfb58` |
| 205 | `sub_flames` | particle | `0x003c4db0` | `0x003c4fb8` | `0x003c4fb0` |
| 206 | `sub_flaming_debris` | particle | `0x003de8f8` | `0x003deb88` | `0x003dea50` |
| 207 | `sub_flashing_light` | particle | `0x003eca88` | `0x003ecdc8` | `0x003eccd8` |
| 208 | `sub_fog` | particle | `0x003ca658` | `0x003ca9d8` | `0x003cac40` |
| 209 | `sub_ghost_light` | light | `0x003d85f0` | `0x003d8730` | `0x003d8698` |
| 210 | `sub_glass` | particle | `0x003e4be0` | `0x003e4cb8` | `0x003e4cb0` |
| 211 | `sub_glint` | particle | `0x003e9560` | `0x003e96a0` | `0x003e9780` |
| 212 | `sub_glt` | particle | `0x003a8e58` | `0x003a8f50` | `0x003a8f48` |
| 213 | `sub_gun` | particle | `0x003a8c60` | `0x003a8db8` | `0x003a8db0` |
| 214 | `sub_hood_smoke` | particle | `0x003f74f8` | `0x003f7580` | `0x003f7578` |
| 215 | `sub_lava_light` | light | `0x003d92d8` | `0x003d9418` | `0x003d9380` |
| 216 | `sub_molotv_flame` | particle | `0x00405e78` | `0x004061c0` | `0x00405f78` |
| 217 | `sub_molotv_light` | light | `0x00405bd8` | `0x00405d90` | `0x00405c98` |
| 218 | `sub_moving_light` | light | `0x003cd6c8` | `0x003cd7b8` | `0x003cd7b0` |
| 219 | `sub_muzzle_flash` | particle | `0x003d8ab8` | `0x003d8b90` | `0x003d8b88` |
| 220 | `sub_neon_light` | light | `0x003e02a8` | `0x003e03d8` | `0x003e03b0` |
| 221 | `sub_neon_light2` | light | `0x003e0538` | `0x003e0708` | `0x003e0648` |
| 222 | `sub_objective_column` | object | `0x003e9d60` | `0x003e9fd8` | `0x003e9ea0` |
| 223 | `sub_objective_glow` | particle | `0x003ea0b0` | `0x003ea260` | `0x003ea198` |
| 224 | `sub_oil_reflect` | object | `0x003bf698` | `0x003bf9d8` | `0x003bf900` |
| 225 | `sub_paint_splat` | particle | `0x003ebed8` | `0x003ebff0` | `0x003ebfe8` |
| 226 | `sub_pch` | particle | `0x003b17f8` | `0x003b1a20` | `0x003b1a18` |
| 227 | `sub_pee` | particle | `0x003a9da0` | `0x003a9f28` | `0x003a9f20` |
| 228 | `sub_pla` | particle | `0x003aa6a0` | `0x003aa8f8` | `0x003aa8f0` |
| 229 | `sub_plaster_drop` | particle | `0x003ec500` | `0x003ec8f0` | `0x003ec8e8` |
| 230 | `sub_polar_bugs` | particle | `0x003e07a0` | `0x003e0980` | `0x003e0938` |
| 231 | `sub_police_light` | light | `0x003e0b28` | `0x003e0e40` | `0x003e0d00` |
| 232 | `sub_powerup_glow` | particle | `0x003f2270` | `0x003f2340` | `0x003f2378` |
| 233 | `sub_puk` | particle | `0x003a9410` | `0x003a9630` | `0x003a9628` |
| 234 | `sub_punch_flash` | particle | `0x003b1460` | `0x003b17f0` | `0x003b17e8` |
| 235 | `sub_ripple` | particle | `0x003f2d70` | `0x003f2ea0` | `0x003f2f18` |
| 236 | `sub_rubble` | particle | `0x003f45e8` | `0x003f46e8` | `0x003f46c0` |
| 237 | `sub_shack_puff` | particle | `0x00408860` | `0x00408a28` | `0x00408a00` |
| 238 | `sub_shack_puff_aligned` | particle | `0x00408610` | `0x004087c0` | `0x00408798` |
| 239 | `sub_shk` | particle | `0x003a8698` | `0x003a87d8` | `0x003a8790` |
| 240 | `sub_sliding_door` | object | `0x003f58f8` | `0x003f5a68` | `0x003f5c38` |
| 241 | `sub_smk` | particle | `0x003ab3e0` | `0x003ab5b0` | `0x003ab5a8` |
| 242 | `sub_smoke` | particle | `0x003f61d8` | `0x003f6460` | `0x003f66f8` |
| 243 | `sub_spark_effect` | particle | `0x003e1578` | `0x003e1648` | `0x003e1640` |
| 244 | `sub_spk` | particle | `0x003abd10` | `0x003abe18` | `0x003abe10` |
| 245 | `sub_splash` | particle | `0x003f2f40` | `0x003f3038` | `0x003f30b0` |
| 246 | `sub_spr` | particle | `0x003aa900` | `0x003aaa40` | `0x003aaa38` |
| 247 | `sub_stained_glass` | particle | `0x003e5090` | `0x003e5168` | `0x003e5160` |
| 248 | `sub_sweat_effect` | particle | `0x003b0f68` | `0x003b10f8` | `0x003b10f0` |
| 249 | `sub_swinging_door` | object | `0x003fb330` | `0x003fb5a0` | `0x003fb430` |
| 250 | `sub_swinging_obj` | object | `0x003e1938` | `0x003e1c28` | `0x003e1a08` |
| 251 | `sub_thrown_dust_puff` | particle | `0x003e1ee0` | `0x003e20d8` | `0x003e20d0` |
| 252 | `sub_train_splat` | particle | `0x003dbb48` | `0x003dbcf0` | `0x003dbce8` |
| 253 | `sub_train_splat_mist` | particle | `0x003dc370` | `0x003dc4e8` | `0x003dc4e0` |
| 254 | `sub_triglint` | particle | `0x003eaf98` | `0x003eb0d0` | `0x003eb060` |
| 255 | `sub_tv_light` | light | `0x003dd190` | `0x003dd378` | `0x003dd2c0` |
| 256 | `sub_water` | particle | `0x003c3078` | `0x003c31f0` | `0x003c31e8` |
| 257 | `sub_wood_splinter` | particle | `0x00407d78` | `0x00407ee8` | `0x00407ec0` |
| 258 | `subway_lensflare` | particle | `0x003e8890` | `0x003e8a48` | `0x003e8a40` |
| 259 | `subway_light` | light | `0x003e8788` | `0x003e8888` | `0x003e8820` |
| 260 | `subway_loop` | particle | `0x003d9698` | `0x003d97a0` | `0x003d9710` |
| 261 | `subway_spark` | particle | `0x003dadc0` | `0x003daf38` | `0x003daf30` |
| 262 | `subway_spark_light_flash` | light | `0x003db148` | `0x003db210` | `0x003db208` |
| 263 | `sweat_drop` | particle | `0x003b0cd0` | `0x003b0ef0` | `0x003b0ee8` |
| 264 | `sweat_mist` | particle | `0x003b0990` | `0x003b0b50` | `0x003b0b48` |
| 265 | `sweat_spray` | particle | `0x003b07f0` | `0x003b0900` | `0x003b08f8` |
| 266 | `thrown_weapon` | object | `0x00403090` | `0x00404660` | `0x004041a8` |
| 267 | `urine_spray` | particle | `0x003dd650` | `0x003dd748` | `0x003dd740` |
| 268 | `Wind_Manager` | particle | `0x004071b8` | `0x00407318` | `0x00407730` |
| 269 | `wood_splinter_bit` | particle | `0x004077b8` | `0x00407ae0` | `0x00407d50` |

## The types, by address {#by-address}

In address order, which is the order of the source files. Each table row is one function.

### How the effect types are built {#effect-pattern}

Most particle types in `0x003a8698`-`0x003b2180` follow one pattern, confirmed (code) in every initialiser below. An
**emitter** (`sub_*`) pops, last-pushed first, an int, a rotation, a position, an attach point (task `+0x6d`), its
parent object (task `+0x58`) and another int ([Spawning](particles.md#spawning)); it sets its update interval
(usually 2 ticks), flags `+0x54` = `0x15` (`0x10` is "attached": `0x003a18c0` returns the parent only with that bit) and
spawns the visible pieces by name through `0x003c56f0`, which makes a task of the type's kind (glass, particle,
object or light, from the type flags). A **piece** (a drop, puff or decal) sets its sprite word (`+0xc4`), colours
(`+0xb0`, `+0xb4`), sizes (`+0xbc`, `+0xc0`) and velocity, and its update counts steps in data `+0x00`, fading the
alpha byte of `+0xb4`; it returns 1 (done) at the last step. Two tests gate nearly every spawn:

- `0x003a5a50`: the particle pool has more than 512 free (inferred from the count it compares);
- `0x003a7d58(15, 5, pos)`: some camera is within 15 m (`Cameras_MinDistanceSq`) and the point is in some player view
  with a 5 m margin (`0x003a51f8`: outside means more than the margin beyond one of the view's six frustum planes,
  `0x003a50e0`). Some types use 25 m and 10 instead; the glass shatter uses 15 and 10
  ([Objects](objects.md#shatter)). Confirmed (code) at `0x003a50e0`.

Blood is skipped while game state `+0x454` (a short, read through `0x003a3e78(6)`) is not 0; game state flag word 0
bit `0x40` swaps the reds for `0x55c9ff` (speculative: a blood colour option or cheat).

### Shake and gun flash {#sub-shk}

`sub_shk` is an explosion's shake. Data: `+0x00` state (0 waiting, 1 fired, 2 done), `+0x04` ticks. The first update
any camera is within 23 m, each view's camera gets a shake of (60 − distance) × 0.00424 with a 0.015 step, and
each player's pad a rumble of 75 + 150 × (1 − d² / 529), at most 255 (player record `+0x1c`, pad record `+0x41`);
the next update turns both off. It ends after 240 ticks or on message `0x13`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003a8790` | `SubShk_OnMessage` | `sub_shk` message | message `0x13` sets state 2 (done) | confirmed (code) |
| `0x003a87d8` | `SubShk_Update` | `sub_shk` update | camera shake and pad rumble by distance, once; 240-tick life | confirmed (code) |
| `0x003a8c60` | `SubGun_Init` | `sub_gun` init | emitter attached to its parent (update every tick) | confirmed (code) |
| `0x003a8db8` | `SubGun_Update` | `sub_gun` update | one `part_gun_flash` at its position, then done | confirmed (code) |
| `0x003a8e58` | `SubGlt_Init` | `sub_glt` init | emitter attached to its parent | confirmed (code) |
| `0x003a8f50` | `SubGlt_Update` | `sub_glt` update | on its first visible update a `sub_glint` (size 0.5-0.7); at 15 updates, or when the pool is busy, removes the glint and ends | confirmed (code) |

### Blood {#blood}

The blood types: `sub_blo` and `sub_pch` / `sub_punch_flash` (hit effects) make `sub_blood_effect`, which makes the
pieces. Colours are `0x7a0015` (`0x9a1616` for sprays) unless the colour swap above is on.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003a9090` | `SubBlo_Init` | `sub_blo` init | attached emitter; at once a `sub_blood_effect` unless blood is off | confirmed (code) |
| `0x003a9270` | `SubBlo_Update` | `sub_blo` update | one `blo_splat` (with its parent) when near a camera and blood is on; done | confirmed (code) |
| `0x003adcc0` | `BloSplat_Init` | `blo_splat` init | a decal on the surface a ray finds (1 m along its rotation, else 2 m along the parent's; `0x003a36e0`), turned to its normal; own sprite batch (`PTank_New(10, 0x10005)`); hidden when no surface | confirmed (code) |
| `0x003ae0f0` | `BloSplat_Update` | `blo_splat` update | 6 steps of 60 ticks (fades early when the pool is busy), then frees its batch (`0x003a4f78`) | confirmed (code) |
| `0x003ae180` | `Bloosh_Init` | `bloosh` init | a puff with velocity, sprite `0x1002e`, update every 4 | confirmed (code) |
| `0x003ae2b0` | `Bloosh_Update` | `bloosh` update | sprites `0x2e`-`0x31`, one per update, then done | confirmed (code) |
| `0x003ae328` | `BloodMist_Init` | `blood_mist` init | a mist at a bone of the human it came from (`0x003a3f30`), placed with the player camera | confirmed (code); camera role inferred |
| `0x003ae520` | `BloodMist_Update` | `blood_mist` update | follows bone 6, grows to 0.6, 5 alpha less a step; done when faded | confirmed (code) |
| `0x003ae708` | `BloodSplatGround_Init` | `blood_splat_ground` init | a small drop on the ground, sprite `0x2`-`0x4`, update every 10 | confirmed (code) |
| `0x003ae880` | `BloodSplatGround_Update` | `blood_splat_ground` update | step 3: 180-tick interval, size 0.225; step 4: faded, 0.45; done at 5 | confirmed (code) |
| `0x003ae990` | `BloodSplatter_Init` | `blood_splatter` init | a falling blob, velocity × 1-3, flags `0x14000005` | confirmed (code) |
| `0x003aeb78` | `BloodSplatter_Update` | `blood_splatter` update | on landing (flag `0x20000000`): sound `vags/character/blood_02`, lies on the floor, and 10 drops in a line along its travel (5 each way) or 17 around it on a slope; step 179 waits 180 ticks, done at 180 | confirmed (code) |
| `0x003af280` | `BloodDrop_Init` | `blood_drop` init | a drop jittered ±0.25 m, sprite `0x10034`, colour by kind (1, 2) | confirmed (code) |
| `0x003af518` | `BloodDrop_Update` | `blood_drop` update | 4 alpha less a step for 20 steps | confirmed (code) |
| `0x003af590` | `BloodSpray_Init` | `blood_spray` init | a streak with velocity × 1-2, sprite `0x10034` | confirmed (code) |
| `0x003af728` | `BloodSpray_Update` | `blood_spray` update | as `blood_drop` | confirmed (code) |
| `0x003af7a0` | `SubBloodEffect_Init` | `sub_blood_effect` init | attached to a human's bone; for bones 5-15 a `blood_mist` at once; keeps the start point | confirmed (code) |
| `0x003afa08` | `SubBloodEffect_Update` | `sub_blood_effect` update | 8 steps within 25 m and on screen: step 1 a `blood_splatter`, a `bloosh` and 8 `blood_spray`; later steps one `blood_drop` along the bone's movement | confirmed (code) |
| `0x003b00c8` | `SubBloodGush_Init` | `sub_blood_gush` init | attached emitter, keeps the start point | confirmed (code) |
| `0x003b01e8` | `SubBloodGush_Update` | `sub_blood_gush` update | 16 steps: step 1 a `blood_splatter` (1 in 4), a `bloosh`, 8 sprays; then two `blood_drop` a step | confirmed (code) |
| `0x003b1460` | `SubPunchFlash_Init` | `sub_punch_flash` init | within 25 m and on screen, blood on: with a count above 1 a `blood_mist` and count + 1 `sub_blood_effect` at random angles; else one | confirmed (code) |
| `0x003b17f8` | `SubPch_Init` | `sub_pch` init | a hit's effect: kind 0 a `sub_sweat_effect`, else a `sub_blood_effect` (blood on) | confirmed (code) |

### Sweat {#sweat}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003b07f0` | `SweatSpray_Init` | `sweat_spray` init | a white puff with velocity, sprite `0x1002e`, update every 4 | confirmed (code) |
| `0x003b0900` | `SweatSpray_Update` | `sweat_spray` update | sprites `0x2e`-`0x31`, 6 alpha less each, then done | confirmed (code) |
| `0x003b0990` | `SweatMist_Init` | `sweat_mist` init | as `blood_mist`, white, sprite rectangle `0x35` | confirmed (code) |
| `0x003b0b50` | `SweatMist_Update` | `sweat_mist` update | follows bone 6, grows 0.02, 10 alpha less a step | confirmed (code) |
| `0x003b0cd0` | `SweatDrop_Init` | `sweat_drop` init | a drop with velocity × 1-3, jittered ±0.25 | confirmed (code) |
| `0x003b0ef0` | `SweatDrop_Update` | `sweat_drop` update | 4 alpha less a step for 20 steps | confirmed (code) |
| `0x003b0f68` | `SubSweatEffect_Init` | `sub_sweat_effect` init | attached to a human (attach point 6); a `sweat_mist` | confirmed (code) |
| `0x003b10f8` | `SubSweatEffect_Update` | `sub_sweat_effect` update | on step 1, 5-8 `sweat_drop` and, with a parent, as many `sweat_spray`; done | confirmed (code) |

### Vomit, urine and other emitters {#emitters}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003a9410` | `SubPuk_Init` | `sub_puk` init | attached emitter; a `puk_splat` below it when near a camera | confirmed (code) |
| `0x003a9630` | `SubPuk_Update` | `sub_puk` update | for 30 updates grey (`0x808080`) `sub_debris` drops; then done | confirmed (code) |
| `0x003a9910` | `PukSplat_Init` | `puk_splat` init | a ground decal found by a 2 m ray, random turn, colour from a table at `0x00512cbc`; hidden with no ground | confirmed (code) |
| `0x003a9b88` | `PukSplat_Update` | `puk_splat` update | size 0.081 × step for 6 steps (every 60 ticks), faded early when the pool is busy | confirmed (code) |
| `0x003a9c78` | `SubDus_Init` | `sub_dus` init | attached emitter; dust at its position (`Particles_Dust`, 0.6 m) | confirmed (code) |
| `0x003a9da0` | `SubPee_Init` | `sub_pee` init | attached emitter; keeps the parent's rotation and a sprite word | confirmed (code) |
| `0x003a9f28` | `SubPee_Update` | `sub_pee` update | every other update one drop: a `pee_tracer` 1 time in 91, else `pee_pee` | confirmed (code) |
| `0x003aa0b0` | `PeeTracer_Init` | `pee_tracer` init | a falling drop (flag `0x10000000`), sprite `0x36`-`0x38`, update every 15 | confirmed (code) |
| `0x003aa200` | `PeeTracer_Update` | `pee_tracer` update | on landing on a floor (normal z above 0.9) a `part_urine_stain2` there; done | confirmed (code) |
| `0x003aa3e0` | `PeePee_Init` | `pee_pee` init | as `pee_tracer` | confirmed (code) |
| `0x003aa530` | `PeePee_Update` | `pee_pee` update | 3 updates while near a camera, then done | confirmed (code) |
| `0x003aa6a0` | `SubPla_Init` | `sub_pla` init | sends message `0x12` to every `part_plaster_drop`, plays sound `0x813c858a`, spawns a `sub_plaster_drop` | confirmed (code) |
| `0x003aa900` | `SubSpr_Init` | `sub_spr` init | attached spray emitter; first argument −1 marks the power-mist form (colour alpha `0x20`) | confirmed (code) |
| `0x003aaa40` | `SubSpr_Update` | `sub_spr` update | two puffs an update for 3 updates: `spray_mist`, or `power_mist` in the −1 form | confirmed (code) |
| `0x003aad68` | `SprayMist_Init` | `spray_mist` init | a puff with velocity, random spin ±90°, sprite `0x4002a`-`0x4002c`; with its flag it starts at step 4 at size 0.45 | confirmed (code) |
| `0x003aaf28` | `SprayMist_Update` | `spray_mist` update | grows 0.015-0.03 a step; at step 5 fades and slows to 60 ticks; done at 6 | confirmed (code) |
| `0x003ab110` | `PowerMist_Init` | `power_mist` init | as `spray_mist`; colour `0xffefef` marks the slow form | confirmed (code) |
| `0x003ab2c8` | `PowerMist_Update` | `power_mist` update | 8 alpha less a step, grows 0.14 (0.05 slow) for 3 steps, faded at 6, done at 7 | confirmed (code) |
| `0x003ab3e0` | `SubSmk_Init` | `sub_smk` init | attached smoke emitter on attach point 6, with a puff size from its argument | confirmed (code) |
| `0x003ab5b0` | `SubSmk_Update` | `sub_smk` update | for 15 updates, near a camera, one or two `sub_smoke` puffs from the parent | confirmed (code) |
| `0x003ab980` | `SubAnimNotes_Init` | `sub_anim_notes` init | one effect at once by kind: 0 a sound (`0x958d9437`), 1 two splinter bursts, 2 `0x003a5ad8`, 3 a burst of 10-15 (`0x003c5ef0`) | confirmed (code); that the kinds are animation notes inferred from the name |
| `0x003abd10` | `SubSpk_Init` | `sub_spk` init | attached spark emitter for 3 updates | confirmed (code) |
| `0x003abe18` | `SubSpk_Update` | `sub_spk` update | within 25 m, 15 `sub_anim_spark` an update | confirmed (code) |
| `0x003ac148` | `SubAnimSpark_Init` | `sub_anim_spark` init | a spark (sprite `0x10032`) with velocity 6-8, life 10-60 updates | confirmed (code) |
| `0x003ac2d0` | `SubAnimSpark_OnMessage` | `sub_anim_spark` message | does nothing | confirmed (code) |
| `0x003ac2f8` | `SubAnimSpark_Update` | `sub_anim_spark` update | moves; slows on landing; after its life 12 alpha less a step | confirmed (code) |

### Burning humans: `fir` and `fir_group` {#fir}

`fir_group` is attached to a burning human and makes the flames, `fir` is one flame. Group data: `+0x00` offset,
`+0x10` the human's handle, `+0x1c` flames made (short), `+0x1e` the spawn period, `+0x1f` afterburn updates (1-15
at start). Flame data: `+0x00` its group's handle, `+0x04` step, `+0x08` fade step, `+0x0c` restart flag.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003b1ef8` | `FirGroup_Init` | `fir_group` init | stores the offset, the human, the period and a random 1-15 afterburn | confirmed (code) |
| `0x003b1ff8` | `FirGroup_Update` | `fir_group` update | done when the human is gone or the afterburn runs out after his fire ends (`Human_IsOnFire`); re-aims the offset every period; makes up to 10 `fir` | confirmed (code) |
| `0x003b1c48` | `Fir_Init` | `fir` init | takes its group's handle, flags `0xc00000`, and starts the flame | confirmed (code) |
| `0x003b1a28` | `Fir_Respawn` | helper of `fir` | places the flame on the human; sprite 8-11, size × 3.6, colour, fade step; while the group is in its last 30 afterburn updates often skips | confirmed (code) |
| `0x003b1cf0` | `Fir_Update` | `fir` update | moves with the human's velocity, 3-5 steps fading, then restarts (done when its group is gone) | confirmed (code) |

### Barrier classes {#barrier-classes}

The barriers beside `dyn_door_fence` ([Breakable barriers](objects.md#barriers)) share its set-up: the init pops the
links number and the two triangle sets, sets the model, makes the triangles two-sided, gives them type bits `0x400`
and the type's material (property 3), retags the number's links (`0x003a4b30`), and sets 10 hitpoints (data `+0x0c`,
object `+0x128`) and a 60-tick update. A break clears both triangle sets (`0x003a46c8`), opens the links
(`0x003a4b00`), makes dust and splinters (unless game state word 0 bit 2), throws debris objects with message `0x30`
(unless word 3 bit 2), plays the material pair (unless word 3 bit 4) and marks data `+0x00` = −5; the update then
reports done, which removes the object (message 2). Messages 10 and `0x19` set hittable and hitpoints.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003b3250` | `DynDoorBarBani_Init` | `dyn_door_bar_bani` init | set-up as above, model `0x0a845744` | confirmed (code) |
| `0x003b33e8` | `DynDoorBarBani_OnMessage` | `dyn_door_bar_bani` message | hit takes 2 + 8 × kind; break hides it and throws two `dyn_dre_bar_bani_a` and two `_b` | confirmed (code) |
| `0x003b40d0` | `DynDoorBarBani_Update` | `dyn_door_bar_bani` update | done once data `+0x00` is −5 | confirmed (code) |
| `0x003b4128` | `DynDoorBnstr_Init` | `dyn_door_bnstr` init | set-up, model `0xfe414c9d`, rotation kept at `+0x20` | confirmed (code) |
| `0x003b42a8` | `DynDoorBnstr_OnMessage` | `dyn_door_bnstr` message | as `bar_bani`; throws `dyn_dre_bnstr_a` to `_f` with random spin | confirmed (code) |
| `0x003b5440` | `DynDoorBnstr_Update` | `dyn_door_bnstr` update | done once broken | confirmed (code) |
| `0x003b5498` | `DynDoorChainS_Init` | `dyn_door_chain_s` init | a chained gate: model `0xd3328d52`, triangles, links | confirmed (code) |
| `0x003b55e8` | `DynDoorChainS_OnMessage` | `dyn_door_chain_s` message | `0x0b` (a human opens it; a hit is passed on as `0x0b`): triangles off, links open, model `0xfd96c6c0`; `0x22` command 5 or 2 opens (2 with the model), 6 closes | confirmed (code) |
| `0x003b57d0` | `DynDoorFenceO_Init` | `dyn_door_fence_o` init | set-up, model `0xc82d7530` | confirmed (code) |
| `0x003b5950` | `DynDoorFenceO_OnMessage` | `dyn_door_fence_o` message | a hit takes its damage argument; at 0: links open, broken model `0x9fc8637e`, `dyn_dre_fence_ob` and `_oc` thrown; it is not hidden | confirmed (code) |
| `0x003b61d8` | `DynDoorFenceO_Update` | `dyn_door_fence_o` update | never done: the wreck stays | confirmed (code) |
| `0x003b6220` | `DynDoorParapet_Init` | `dyn_door_parapet` init | set-up, model `0xed8a5ad4` | confirmed (code) |
| `0x003b63a0` | `DynDoorParapet_OnMessage` | `dyn_door_parapet` message | as `bar_bani`, more dust; throws `dyn_dre_parapet_a` to `_d` | confirmed (code) |
| `0x003b74e0` | `DynDoorParapet_Update` | `dyn_door_parapet` update | done once broken | confirmed (code) |

### `dyn_masks` and `dyn_blaster` {#dyn-masks-blaster}

`dyn_masks` is a breakable prop class. Its init reads the type's properties: 0 the hitpoints (data `+0x0c`, object
`+0x10d`), 1 a second value (data `+0x10`, object `+0x128` = 1), 5 above 0 or game state word 3 bit 1 sets data
`+0x04` and leaves the path polygon's flag 8 off, else sets it. By model hash it adds a child: `part_torch_flame`,
`part_firebarrel` (two models), `dyn_oil_reflect` or `part_fire_tiki`; one model converts the jump link at it into a
door link and disables it. Its update and messages are at `0x003bf548` and `0x003bf1b8`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003b7538` | `DynMasks_Init` | `dyn_masks` init | as above; update every 20 ticks | confirmed (code) |
| `0x003ac708` | `Radio_Init` | `dyn_blaster` init | no model, update every 20, record `+0x20` = 20, `+0x24` a global (`0x00598690`), the rest cleared; the radio itself is not traced here | confirmed (code) |

### `dyn_masks`: breakable props {#dyn-masks}

`dyn_masks` (type 39, flags `0x08`) is the class of most breakable props: benches, stalls, barrels, cabinets, trash
and the like ([Object types](objects.md#object-types) give each prop its class). Every type function first asks the
task for its type data (task vtable `+0x18c`). Its record, confirmed (code) at `0x003bf1b8` and `0x003b7a88`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | broken (1) |
| `+0x08` | update interval to use (2 after a knock or a removal) |
| `+0x0c` | hit points; −1 none; −10 together with `+0x10` = −10: ignores hits (set after an explosive prop goes off) |
| `+0x10` | a second count, lowered by one per hit of strength 3 when the type's flag `0x4` is set (message `0x22` sets it) |
| `+0x14` | a child task (removed with message `0x15` on message `0x20`) |

**The hit** (`0x003b7a88`, message 1; 30 KB). It pops the hit point and direction, a kind, a strength and two
handles. Unless both counts are −10, the hit points drop by **4 + 6 × strength**; at 1 or below the prop is broken
(`+0x00` = 1) and object `+0x10d` mirrors the hit points left. One prop (model hash `0xf21e6a91`) breaks at once on a
kind of −1. Then, with game-state bit `0x4` (flag set 3) clear, a material sound plays at the hit point, and the
rest is a long chain on the prop's **model hash** (object `+0xc4`, the CRC-32 of its name,
[Object types](objects.md#object-types))
that picks the effects: dust (radii 3.75 and 2.75 m, unless game-state flag 1 forbids it), splinters, coloured debris
pairs, spawned pieces (`dyn_wooddmg_a`, `dyn_wooddmg_b`, `dyn_trashbit_a`/`_b`/`_d`, `dyn_money`, `dyn_beerbottle`,
`dyn_parkbench_aa`, `dyn_jewelcase_aa`, `dyn_fruitstand_aa`, `dyn_easel_c`, `dyn_barrel_ac`, `dyn_cabinet_ab`,
`dyn_barbeque_wreck`, `dyn_oil_fire_b`), effects by name (`sub_debris`, `sub_rubble`, `sub_blo`, `sub_paint_splat`,
`glasstest`, `part_water`, `part_steam`, `sub_spark_effect`, `sub_car_spark_emitter`, `sub_fireball`,
`sub_explosion_embers`, `spawn_delayed`), blowing litter (`Garbage_Throw`, [Particles](particles.md#garbage)), and for
explosive props `World_BreakObjectsAround` at 0.3-0.75 m with both counts set to −10. A broken prop takes its broken
model and drops its child. Confirmed (code) for the hit point arithmetic and the calls; which hash is which prop is not
traced (the hashes are of names we have not matched).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003b7a88` | `DynMasks_OnHit` | `dyn_masks` message 1 (called by `0x003bf1b8`) | hit points, break, effects by model hash, as above | confirmed (code) |
| `0x003bf1b8` | `DynMasks_OnMessage` | `dyn_masks` message | 1 the hit; 4 pops a knock (interval 2); `0x15` flag `0x40` (removed), interval 2; `0x19` sets or clears flag `0x8000`; `0x20` removes the child; `0x22` sets `+0x10` and clears `+0x0c`; `0x30` knocked: spin 4-6 rad/s, flags `0x4200000`; `0x3c` reloads the model by hash (four props lose their body; one picks one of two models at random) | confirmed (code) |
| `0x003bf548` | `DynMasks_Update` | `dyn_masks` update | done (1) once broken; once removed, its interval grows by one each update | confirmed (code) |

### `sub_oil_reflect` {#sub-oil-reflect}

A static model (`0x1c21bdc3`) laid flat under something, flags `0x200401`, updated every 20 ticks (inferred: the shine
of an oil pool). Its record holds two task pointers (`+0x00`, `+0x04`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003bf698` | `SubOilReflect_Init` | `sub_oil_reflect` init | pops its parent, sets the model, flags and interval, turns it to lie flat | confirmed (code) |
| `0x003bf900` | `SubOilReflect_OnMessage` | `sub_oil_reflect` message | `0x15` deletes it; `0x20` removes the first task (message `0x15`) and deletes the second | confirmed (code) |
| `0x003bf9d8` | `SubOilReflect_Update` | `sub_oil_reflect` update | nothing; returns 0 | confirmed (code) |

### `sub_flame_reflect` {#sub-flame-reflect}

A flat sprite on batch 7 (`lighting`, [Sprite words](particles.md#sprite-words)) that steps through 36 rectangles,
colours `0xffffff00` / `0xffffff80`, size 2.5: a flicker of firelight on the ground (inferred from the name).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003bfa08` | `SubFlameReflect_Init` | `sub_flame_reflect` init | interval 2, flags `0x480000`, a random first frame 0-35, sprite `0x70000 \| frame` | confirmed (code) |
| `0x003bfb58` | `SubFlameReflect_OnMessage` | `sub_flame_reflect` message | handles nothing | confirmed (code) |
| `0x003bfb80` | `SubFlameReflect_Update` | `sub_flame_reflect` update | next frame, wrapping at 36 | confirmed (code) |

### Cash registers (`dyn_cashreg`, `dyn_cashreg_b`) {#dyn-cashreg}

`dyn_cashreg` (type 14) is the register a player can rob or smash; `dyn_cashreg_b` (type 15) is its drawer, a separate
task (model `0x2e055fc5`) kept at the register's `+0x1c`. Register record: `+0x10` broken, `+0x11` the drawer flew out,
`+0x14` update interval, `+0x18` hit points (type property 1), `+0x1c` the drawer. Drawer record: `+0x10` interval,
`+0x14` a step count, `+0x18` open, `+0x1c` a value it passes on. Confirmed (code).

- **Use** (message 0, the triangle): if not broken and not busy (`0x100000`), the register marks itself busy and sends
  message `0x14` to the human (inferred: the rob action). Any drawer is removed.
- **Hit** (message 1): the hit points drop by **2 + 8 × strength** (a kind of −1 empties them, −2 costs nothing).
  At 0: broken model `0x59026f53`, interval 240, flag `0x800000`, and the drawer gets message `0x12` (it opens); with no
  drawer the register itself is knocked (message `0x30`, a random spread ±0.25) and rescheduled.
- The drawer's update, one step after opening, calls `0x003a4cb0` with a random 25-50 (inferred: the money that spills;
  not traced), then sleeps 240 ticks.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003bfbf0` | `DynCashregB_Init` | `dyn_cashreg_b` init | model `0x2e055fc5`, flags `0x201`, interval 60, record cleared | confirmed (code) |
| `0x003bfcc8` | `DynCashregB_OnMessage` | `dyn_cashreg_b` message | `0x12` shows it and slides it open (`+0x18` = 1); `0x15` deletes; `0x30` knocked (spin 4-6); `0x3c` re-poses it | confirmed (code) |
| `0x003bffd0` | `DynCashregB_Update` | `dyn_cashreg_b` update | when open, one step later `0x003a4cb0(value, 25-50)` and interval 240 | confirmed (code); the meaning inferred |
| `0x003c0108` | `DynCashreg_OnPickUp` | helper (message `0x1b`, from `0x003c06b0`) | attached to the human's hand at a bone, flags `0x10`; messages 3 and `0x17` to the human; the drawer removed | confirmed (code) |
| `0x003c0330` | `DynCashreg_OnDrop` | helper (message `0x1c`, from `0x003c06b0`) | detached when held, flag `0x4000000` (airborne), a random drop speed | confirmed (code) |
| `0x003c04e0` | `DynCashreg_Init` | `dyn_cashreg` init | interval 20, flags `0x228001`, no own model, hit points from type property 1 | confirmed (code) |
| `0x003c06b0` | `DynCashreg_OnMessage` | `dyn_cashreg` message | 0 use, 1 hit, 4 a knock, 10 hittable, `0x1b` / `0x1c` hand, `0x20` the drawer removed, `0x30` knocked, `0x3c` broken model and the drawer told | confirmed (code) |
| `0x003c0d98` | `DynCashreg_Update` | `dyn_cashreg` update | done once the drawer flew out (`+0x11`); else keeps its interval | confirmed (code) |

### `dyn_woodbridge` {#dyn-woodbridge}

A plank bridge (model `0x6a493d24`, interval 90). Its record holds one child (`+0x00`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c0df0` | `DynWoodbridge_Init` | `dyn_woodbridge` init | flags 1, model, interval 90 | confirmed (code) |
| `0x003c0ec0` | `DynWoodbridge_Update` | `dyn_woodbridge` update | nothing; returns 0 | confirmed (code) |
| `0x003c0ef0` | `DynWoodbridge_Break` | helper (from `0x003c1148`) | five pieces at random 2.0-3.5, flag `0x800000`, broken model `0xefcfc554`, interval 180, a break sound unless game-state bit `0x4`, the child deleted | confirmed (code) |
| `0x003c1148` | `DynWoodbridge_OnMessage` | `dyn_woodbridge` message | 8 detach; 1 (a hit) or `0x15` break | confirmed (code) |

### `dyn_scaffold` {#dyn-scaffold}

A scaffold that counts hits (record `+0x00` done, `+0x04` hits left from type property 0; flags `0x200081`, interval
20). Nothing in the type sets `+0x00`; who does is not traced.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c1240` | `DynScaffold_OnHit` | helper (message 1, from `0x003c1318`) | one hit less | confirmed (code) |
| `0x003c1250` | `DynScaffold_Init` | `dyn_scaffold` init | flags, interval 20, no own model, hits from type property 0 | confirmed (code) |
| `0x003c1318` | `DynScaffold_OnMessage` | `dyn_scaffold` message | 1 hit; 8 detach; `0x1b`, `0x1c` only pop their arguments | confirmed (code) |
| `0x003c1420` | `DynScaffold_Update` | `dyn_scaffold` update | done once `+0x00` is set | confirmed (code) |

### `dyn_chand`: the chandelier {#dyn-chand}

A hanging light (flags `0x201001`, interval 10) with a light child (`+0x2c`, given colour `0xd3a745ff` by message
`0x19`) and swing amplitudes of 0.098 rad on three axes (`+0x20`-`+0x28`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c1450` | `DynChand_Init` | `dyn_chand` init | the fields above; `+0x18` 5, `+0x14` 15 (flicker steps) | confirmed (code) |
| `0x003c1610` | `DynChand_OnMessage` | `dyn_chand` message | `0x12` flicker on (`+0x1c` = 1); `0x13` and `0x30` it falls: spin, flags `0x4a00000`, model `0x0b3d94d2`, 15 glass shards; `0x20` the light deleted | confirmed (code) |
| `0x003c1b58` | `DynChand_Update` | `dyn_chand` update | while flickering, a random warm colour every few ticks to the light, back to `0xd3a745ff` after `+0x14` steps; when landed (`0x20000000`) the light goes and it is done | confirmed (code) |

### `spawn_delayed` {#spawn-delayed}

Waits a number of updates, then creates a weapon (record `+0x00` count, `+0x04` delay, `+0x08` kind). Kinds: 0-1 none,
2 `dyn_molotv`, 3 `dyn_beerbottle`, 4 `dyn_bat`, 5 `dyn_woodboard`, 6 `dyn_pipe_a`, 7 `dyn_hunter`, 8 `dyn_tknife`,
9 `dyn_machet`. The broken props use it to drop a weapon ([`dyn_masks`](#dyn-masks)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c1e58` | `SpawnDelayed_Init` | `spawn_delayed` init | pops delay and kind, flags 4, interval 2; a random spin when the kind is above 1 | confirmed (code) |
| `0x003c1ff0` | `SpawnDelayed_Update` | `spawn_delayed` update | at the delay creates the kind's weapon (airborne, `0x4000000`) and is done | confirmed (code) |

### `sub_debris` {#sub-debris}

A falling piece (sprite or model) that bounces and fades. Record: `+0x10` sound material, `+0x14` interval, `+0x18`
step, `+0x1c` steps, `+0x20` size × 1.1, `+0x24` kind (0 sprite, 1 small model piece, 2 set by message `0x27`).
Made by `Spawn_SubDebris` ([Spawn helpers](#spawn-helpers)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c21a8` | `SubDebris_Init` | `sub_debris` init | pops material, sprite, colour, size, life and speed; a negative size means a model piece; speed 5-9 along its direction | confirmed (code) |
| `0x003c24e8` | `SubDebris_Update` | `sub_debris` update | shrinks a model piece over its life; airborne until it lands (`0x20000000`), then a material sound (`Sound_PlayMaterialPair`), lies flat; at the end of its life fades and is done | confirmed (code) |
| `0x003c2a48` | `SubDebris_OnMessage` | `sub_debris` message | `0x27` sets the material and kind 2 | confirmed (code) |

### `part_water` and `sub_water` {#part-water}

A water burst (a broken hydrant or pipe, inferred): `part_water` lives for a count of updates and each update makes
four `sub_water` drops and two mist puffs within the particle budget (`0x003a5a50`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c2ab8` | `PartWater_Init` | `part_water` init | pops count, colour and spread; flags 4, interval 2 | confirmed (code) |
| `0x003c2ba8` | `PartWater_Update` | `part_water` update | done at the count or when the budget refuses; else the drops (±0.25 × spread) and puffs (size 0.9-1.1) | confirmed (code) |
| `0x003c3078` | `SubWater_Init` | `sub_water` init | flags `0x800000`, batch 1 rectangle `0x2e` + 0-3, size 0.65 × spread, 0.09 growth; a flag 1 adds `0x10000000` | confirmed (code) |
| `0x003c31f0` | `SubWater_Update` | `sub_water` update | done after its life; falls 0.33 a step; on landing a small splash at ±0.75 × spread | confirmed (code) |

### Explosions {#part-explosion}

`part_explosion` (interval 30, flags `0xc04`) is done after 24 updates (freeing its batch, `+0x08`). Its init
(`0x003c3448`) makes, at its position `p` and rotation, confirmed (code):

- a sprite batch of its own (`PTank_New(10.0, 0x10006, ...)`: sheet record 1, `part_page1`, rectangle 6; capacity
  6) kept at data `+0x08`;
- **six `sub_explosion_embers`** (table below), each given a random vector (x and z ±0.05, y −3.5 to 0.05, turned by
  the rotation), a random 0.6-1.5, the fire sprite word `0xc0000` (batch 12, `part_fire`) and the explosion's batch;
  which popped value is the speed and which the size is not traced;
- **one `sub_fireball_emitter`** (`0x003c4580`), which makes **six `sub_fireball`s** at scale 2.0, one along each of
  ±x, ±y, ±z of the rotation, moving outward;
- **one `sub_explosion_light`** at `p` + (0, 0, 0.5);
- while the particle budget allows, **twenty `sub_debris`** at `p` + (0, 0, 0.5) with directions x, y ±0.5 and z
  0.25-1, life 30, size 0.25-0.35, white, `part_page1` rectangles 8-11, each then sent message `0x27` with
  `0xc0000`; and **eight smaller `sub_debris`** at `p` + (0, 0, 0.55), directions x, y ±0.25 and z 0-0.75, life 60,
  size 0.05-0.1, the same sprites.

**`sub_explode`** (the molotov's flash; `0x003c5350`, `0x003c54b8`): `part_page1` rectangle 17 (batch 4), a random
roll, flags `0x80000`, colour from white at alpha `0xf0` toward alpha `0xbf`. Stages (interval, size): 1 tick to 0.2,
then 8 ticks to 1.0 (at this stage it makes the `part_explosion` at its position and rotation), then 40 ticks to 1.5
while the colour fades to alpha 0, then done: about 49 ticks. A particle's drawn sprite is 2 × size wide
(`ParticleTask_Draw`, [GUI](gui.md#radar-icons)), so the flash grows to 3 m.

**`sub_fireball`** (`0x003c4a58`, `0x003c4c38`): `part_page1` rectangle 42 (batch 4), flags `0x80000`, a random roll
and flip, velocity = the direction × scale × 1.25, size table `0x00512ea0` (0.8) × scale (1.6 for the emitter's 2.0),
colour from transparent black to the stage colours. Seven stages, each lasting the table's interval plus up to as
much again (8, 10, 10, 8, 8, 10, 10 ticks) and setting the size to 0.8-0.88 × scale; the stage colours (`0x00512ec0`,
`0xRRGGBBAA`): `1010ce24` (a faint blue), `fb780c9f`, `c336097f` (oranges), `590e0024`, `33080030`, `33080020`,
`34210010` (dark, fading). Done at stage 7: 64-128 ticks. The parts step through
small tables of interval, colour and size: `sub_fireball` 7 stages (`0x00512e80` intervals, `0x00512ec0` colours,
`0x00512ea0` start size), `sub_flames` 7 (`0x00512ee0`, `0x00512f00`), `sub_explode` 3 (`0x00512e58`, `0x00512e68`).
`sub_explosion_light` is a light: colour `0xdacaffff`, then `0xf45c1cff`, `0xf45c1a90`, `0xf4ba1f20`, off at the
fourth step, done at the fifth.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c3448` | `PartExplosion_Init` | `part_explosion` init | as above | confirmed (code) |
| `0x003c3bb0` | `PartExplosion_Update` | `part_explosion` update | done after 24 updates; frees its batch | confirmed (code) |
| `0x003c3c08` | `SubExplosionEmbers_Init` | `sub_explosion_embers` init | pops colours, sprite, speed; flags `0x10000004` (bit `0x10000000` cleared by game-state flag 1), speed 6-8, life 25-40 | confirmed (code) |
| `0x003c3da8` | `SubExplosionEmbers_Update` | `sub_explosion_embers` update | on landing a burst of up to 30 sparks within the budget; done at the end of its life; else interval 1-3 | confirmed (code) |
| `0x003c4580` | `SubFireballEmitter_Init` | `sub_fireball_emitter` init | flags `0x404`; six fireballs at fixed directions about its rotation | confirmed (code) |
| `0x003c4a58` | `SubFireball_Init` | `sub_fireball` init | sprite `0x4002a` (batch 4, rectangle 42), flags `0x80000`, size from the table × the given scale, a random flip | confirmed (code) |
| `0x003c4c38` | `SubFireball_Update` | `sub_fireball` update | next stage: interval, colour, 80-88 % size; done at stage 7 | confirmed (code) |
| `0x003c4db0` | `SubFlames_Init` | `sub_flames` init | as `sub_fireball` with the flame tables | confirmed (code) |
| `0x003c4fb8` | `SubFlames_Update` | `sub_flames` update | next stage, size × 1.05-1.2, slows its velocity; done at stage 7 or when the budget refuses | confirmed (code) |
| `0x003c51b0` | `SubExplosionLight_Init` | `sub_explosion_light` init | flags `0x88`, interval 25, colour `0xdacaffff`, radius 10 (`+0x8c`, `+0x90`) | confirmed (code) |
| `0x003c5248` | `SubExplosionLight_OnMessage` | `sub_explosion_light` message | handles nothing | confirmed (code) |
| `0x003c5270` | `SubExplosionLight_Update` | `sub_explosion_light` update | the colour steps above (interval 60, then 30); done at step 5 | confirmed (code) |
| `0x003c5350` | `SubExplode_Init` | `sub_explode` init | sprite `0x40011`, colours, a random roll about z, flags `0x80000` | confirmed (code) |
| `0x003c5490` | `SubExplode_OnMessage` | `sub_explode` message | handles nothing | confirmed (code) |
| `0x003c54b8` | `SubExplode_Update` | `sub_explode` update | three stages of interval and size; done at 3 | confirmed (code) |

### `sub_burn` {#sub-burn}

The burning patch a fire leaves (`0x003c40a0` init, [AI](ai.md) no-go spheres): it holds an AI no-go sphere (record
`+0x14`, the sphere's index; below 65 means one is held) and grows in four steps.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c4210` | `SubBurn_OnMessage` | `sub_burn` message | `0x13` releases the sphere (`0x003a5ab8`) | confirmed (code) |
| `0x003c4280` | `SubBurn_Update` | `sub_burn` update | four growth steps (size 0.75-1.5 × scale; 0.4 × on steep ground); at the end releases the sphere and is done | confirmed (code) |

### `fade_object` {#fade-object}

A loose piece of a broken prop (flags `0xa00001`, interval 20) that is thrown, lands and later fades. Record `+0x30`
age, `+0x34` a value from message `0x27`. Confirmed (code).

- On landing it lies flat and, by its type name, makes a mess: `dyn_paintcan_brk` a `paint_splat` (larger with
  game-state bit `0x20`), `dyn_icecream_a_fade`, `dyn_hotdog_c_fade`, `dyn_oilcan_brk` and `dyn_steak_fade` their own
  splats or pieces.
- Past an age of 3550 it fades by 5 each update; message `0x15` jumps the age to 3400 (3530 for three models) so it
  fades soon. A piece of the broken register (`0x59026f53`) is treated apart.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c60d8` | `FadeObject_Init` | `fade_object` init | pops its parent, interval 20, flags, no own model, counters cleared | confirmed (code) |
| `0x003c61d8` | `FadeObject_Update` | `fade_object` update | the landing, the messes and the fade above; also calls `0x003a4cb0` with 25-50 | confirmed (code) |
| `0x003c6ae0` | `FadeObject_Launch` | helper (message `0x30`, from `0x003c6c48`) | not busy; a speed of 2-5 along the knock; interval 2; flags `0x4200000` | confirmed (code) |
| `0x003c6c48` | `FadeObject_OnMessage` | `fade_object` message | 1 pops a hit; 8 drop; 10 hittable; `0x15` fade soon; `0x19` alpha (`+0xcc`); `0x27` `+0x34`; `0x30` launch | confirmed (code) |

### Script type helpers {#type-helpers}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c55c8` | `ScriptType_CompareName` | helper (passed by `ScriptType_Find` `0x003c55e8`) | compares a name with a record's name (`0x00430c4c`) | confirmed (code) |
| `0x003c56a0` | `ScriptType_FindRecord` | helper (4 callers) | the type record for a name | confirmed (code) |
| `0x003c56f0` | `ScriptType_CreateByName` | helper (118 callers) | creates a task of a named type in the manager its flags pick: `0x400` the glass manager, `0x10` the particle pool, `0x08` the object manager, `0x04` the light manager; null when none | confirmed (code) |

### Spawn helpers {#spawn-helpers}

Each opens the [message scratch](tasks.md#messages) (`0x003a2d20`), pushes its arguments, creates the named type
(`Task_CreateParticleByName`) and closes the scratch (`0x003a2d40`). Callers are the props' hit handlers
([`dyn_masks`](#dyn-masks), `MeleeWeapon_Break` and others).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c5860` | `Spawn_SubDebris` | helper (8 callers) | position, velocity, a handle, an int, a handle, three ints → `sub_debris` | confirmed (code) |
| `0x003c5960` | `Spawn_SubRubble` | helper (9 callers) | position, velocity, an int, a handle, an int → `sub_rubble` | confirmed (code) |
| `0x003c5a18` | `Spawn_WoodSplinterBit` | helper (2 callers) | three vec4s, an int, a handle, two ints → `wood_splinter_bit` | confirmed (code) |
| `0x003c5c18` | `Spawn_SubColouredGlass` | helper (2 callers) | position, velocity, a handle, an int → `sub_coloured_glass` | confirmed (code) |
| `0x003c5cb8` | `Spawn_Rubble` | helper (1 caller) | position, velocity, an int, a handle, an int → `rubble` | confirmed (code) |
| `0x003c5d70` | `Spawn_SubDetergent` | helper (3 callers) | a position and an int → `sub_detergent` | confirmed (code) |
| `0x003c5de0` | `Spawn_SubSparkEffect` | helper (5 callers) | a position and a direction → `sub_spark_effect` | confirmed (code) |
| `0x003c5e50` | `Spawn_SubPaintSplat` | helper (2 callers) | position, velocity, a handle, an int → `sub_paint_splat` | confirmed (code) |
| `0x003c5ef0` | `Spawn_Glasstest` | helper (1 caller) | position, velocity, a handle, two ints → `glasstest` | confirmed (code) |
| `0x003c5fa8` | `Spawn_SubBlight` | helper (1 caller) | a position, an int, a handle → `sub_blight`; returns the task | confirmed (code) |
| `0x003c6ee0` | `Spawn_ByNameWithParent` | helper (from `0x003c7930`) | a handle → the type named by the caller | confirmed (code) |

### `sub_fire` and its parts {#sub-fire}

`sub_fire` and `sub_fire2` share their functions: a flame sprite (record `+0x00` the batch, `+0x08` frame, `+0x0c` size
5.5 × scale, `+0x10` scale) stepping through 36 frames every 2-3 ticks with a ±10 % size wobble kept within
0.8-1.2 ×,
and, within the particle budget, `sub_embers` and `sub_fire2_smoke` (`Spawn_ByNameWithParent`). Message `0x15` sets
`+0x04` to −1, after which the colour fades by 2 a step until it is done.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c7840` | `SubFire_Init` | `sub_fire` / `sub_fire2` init | pops batch and scale; random first frame; flags `0x400000`; interval 2 | confirmed (code) |
| `0x003c7930` | `SubFire_Update` | `sub_fire` / `sub_fire2` update | as above; done when faded below 17 | confirmed (code) |
| `0x003c7cf8` | `SubFire_OnMessage40` | helper (message `0x40`, from `0x003c7d30`) | does nothing | confirmed (code) |
| `0x003c7d30` | `SubFire_OnMessage` | `sub_fire` / `sub_fire2` message | 8 detach; `0x15` put out; `0x40` | confirmed (code) |
| `0x003c72d8` | `SubEmbers_Init` | `sub_embers` init | pops a size; interval 10; sprite `0x10014` | confirmed (code) |
| `0x003c73c8` | `SubEmbers_Update` | `sub_embers` update | seven steps; from step 2 a small size, sprite `0x10036` + 0-2, colour from `0x00514488`; done at 7 or when the budget refuses | confirmed (code) |
| `0x003c7550` | `SubEmbers_OnMessage` | `sub_embers` message | handles nothing | confirmed (code) |
| `0x003c7578` | `SubFire2Smoke_Init` | `sub_fire2_smoke` init | sprite `0x1002a` + 0-2, interval 30, size from the scale, colours `0x50100090` / `0xb0100030` | confirmed (code) |
| `0x003c7640` | `SubFire2Smoke_Update` | `sub_fire2_smoke` update | four steps growing 1.15-1.35 × and fading by 30; done at 4 | confirmed (code) |
| `0x003c7818` | `SubFire2Smoke_OnMessage` | `sub_fire2_smoke` message | handles nothing | confirmed (code) |

### `sub_fade_flame` {#sub-fade-flame}

A flame that burns down with a looping sound (record `+0x18` the sound, `+0x14` a sprite batch, `+0x20` a parent).
Frames as `sub_fire`; once message `0x15` arrives or its frame count runs out, its colour drops by 2 a step and the
sound level follows it (`SoundTask_SetFadeLevel`, alpha / 128); below 17 the sound stops and it is done.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c7de0` | `SubFadeFlame_Init` | `sub_fade_flame` init | pops parent, flag, sprite, sound and size; flags `0x400`; frame count from the size (at most 4 extra) | confirmed (code) |
| `0x003c8090` | `SubFadeFlame_OnMessage` | `sub_fade_flame` message | `0x15` starts the fade | confirmed (code) |
| `0x003c80d8` | `SubFadeFlame_Update` | `sub_fade_flame` update | as above; at frame 15 with game-state bit `0x10` it lets go of its parent | confirmed (code) |

### The narrow and square flames {#flames}

`part_narrowflame` and `part_squareflame_lrg`/`_med`/`_sml` share functions; their init (`Flame_Init`, `0x003c84f0`)
tells them apart by name ([Particles](particles.md#type-record)). They share one sprite batch (`0x0051447c`), freed
when one is deleted outside level index 93 (`0x00514480` is the request).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c6e18` | `Flame_ReleaseSharedBatch` | helper (from `0x003c84f0`, `0x003c8698`) | keeps the shared batch while its instance exists; on request frees it | confirmed (code) |
| `0x003c8698` | `Flame_OnMessage` | flames message | `0x12` shows, `0x13` hides (flag `0x4`), `0x15` deletes (asking for the batch to be freed) | confirmed (code) |
| `0x003c87a0` | `Flame_Update` | flames update | next of 36 frames from its base rectangle | confirmed (code) |

### Fire lights {#fire-lights}

`part_fire_light_large` and `part_fire_light_medium` each own one `sub_fire2_light` (record `+0x00`; `+0x04` is 1 for
the large one). `sub_fire2_light` is a light (flags `0x488`) of radius 7.5 × size (at most 15) that flickers through
four colours (`0x005144a8`) at 90-100 % of its radius every 5-15 ticks; size 0 makes it idle (interval 180).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c8808` | `FireLight_Init` | `part_fire_light_*` init | interval 100, flags 4; creates the `sub_fire2_light` | confirmed (code) |
| `0x003c8970` | `FireLight_OnMessage` | `part_fire_light_*` message | `0x12` on (the light made again if gone), `0x13` off (light deleted), `0x15` deletes both | confirmed (code) |
| `0x003c6f58` | `SubFire2Light_Init` | `sub_fire2_light` init | pops parent, size, kind; radius and colour `0xf45c1cff` | confirmed (code) |
| `0x003c70b0` | `SubFire2Light_OnMessage` | `sub_fire2_light` message | 10 on/off (off: flag `0x4`); `0x15` deletes; `0x22` sets the radius | confirmed (code) |
| `0x003c71a8` | `SubFire2Light_Update` | `sub_fire2_light` update | the flicker; idle when the radius is 0 | confirmed (code) |

### `part_fire` and the other fires {#part-fire}

The ten fire types (`part_fire`, `part_fire_large`, `part_torch_flame`, `part_firebarrel` ...
[Particles](particles.md#type-record))
share `Fire_Init` (`0x003ca050`) and the two below. A fire **streams with the cameras**: while it is on (`+0x04`) and
not put out (`+0x0c`), each update it is lit (`FireParticle_SetUpByName`, `0x003c8b28`) when a view camera is near
(`0x003a51f8`) and taken down (`Fire_Stop`) when none is (`+0x08` lit). Taking down removes the child (`+0x00`), the
AI no-go sphere (`+0x18`) and a sound (`+0x1c`), and deletes the task when `+0x0c` is 1.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003c9f78` | `Fire_Stop` | helper (from `0x003ca138`, `0x003ca220`) | as above | confirmed (code) |
| `0x003ca138` | `Fire_Update` | `part_fire*` update | the camera streaming above | confirmed (code) |
| `0x003ca220` | `Fire_OnMessage` | `part_fire*` message | 8 detached; 10 on/off; `0x12` on; `0x13` off; `0x15` stopped, its sound emitter disabled, deleted; `0x1b`, `0x1c` pop | confirmed (code) |

### `float_item` {#float-item}

An item model that rises and fades over a human (flags `0x800001`, interval 10): `FloatItem_Spawn`, called from human
code (`0x002334d0`), creates `dyn_float_item` with the CRC-32 of a name as its model key (inferred: the item a player
just took, shown over his head).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ca3e0` | `FloatItem_Init` | `float_item` init | pops parent and model; `+0x00` 6.0; a velocity of (0, 0, 0.1) | confirmed (code) |
| `0x003ca500` | `FloatItem_Update` | `float_item` update | alpha (`+0xc8`) down by `0x20` a step; done below `0xffffff01` | confirmed (code) |
| `0x003ca560` | `FloatItem_OnMessage` | `float_item` message | handles nothing | confirmed (code) |
| `0x003ca588` | `FloatItem_Spawn` | helper (from `0x002334d0`) | pushes the hash and the human, creates `dyn_float_item`, starts it | confirmed (code); the use inferred |

### `sub_fog`: a fog wisp {#fog-wisp}

A wisp of `part_fog` ([Particles](particles.md#fog); init `0x003ca658`). Record `+0x00` age, `+0x08` target colour,
`+0x0c` fade-in steps, `+0x10` the camera it belongs to, `+0x14` fade step. It fades in toward its colour, is shown at
full colour every tick when its camera is beyond 20 m (or within 10 m when that camera says so), every 30 ticks within
4 m, and is done (lowering the wisp count `0x005144cc`) when its camera is not the player view any more.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ca9d8` | `SubFog_Update` | `sub_fog` update | as above | confirmed (code); the distance roles inferred |
| `0x003cac40` | `SubFog_OnMessage` | `sub_fog` message | handles nothing | confirmed (code) |

### `part_fog`: the fog emitter {#part-fog}

`part_fog` (init `0x003cadd8`, [Drifting fog](particles.md#fog)) keeps the screen's fog wisps topped up. Its record:
`+0x00` the sprite batch, `+0x04` a value set by message `0x19`, `+0x08` the wisps wanted per view (message `0x22`),
`+0x0c` the views counted, `+0x18` / `+0x1c` two values set by messages `0x41` (both) and `0x27` (the second).
Confirmed (code) at `0x003caff0`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003caed8` | `PartFog_Update` | `part_fog` update | while the live wisp count (`0x005144cc`) is below `+0x08` × the view count, spawns a `sub_fog` wisp, at most 10 per update | confirmed (code) |
| `0x003cac68` | `PartFog_PickWispPoint` | helper (called by `0x003caed8`) | a random point near the player camera: ±20 m across, 0.5-2 m above the camera's height | confirmed (code) for the calls; the axes inferred |
| `0x003caff0` | `PartFog_OnMessage` | `part_fog` message | `0x15` frees the batch and removes the task; `0x19`, `0x22`, `0x27`, `0x41` store the fields above | confirmed (code) |

### `dyn_table`: tables that break in one hit {#dyn-table}

A `dyn_table` (type 52) breaks on its first hit. Record: `+0x00` hit (set by game-state flag set 3 bit 1 at
start), `+0x04` broken, `+0x08` knocks counted, `+0x0c` the update interval. Confirmed (code) at `0x003cb0c8` and
`0x003cb220`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003cb0c8` | `DynTable_Init` | `dyn_table` init | model, flags 1, update every 60 ticks; with flag set 3 bit 8 its collision body loses bit 4 | confirmed (code) |
| `0x003cb220` | `DynTable_OnHit` | `dyn_table` message 1 | pops the hit (point, direction, two handles); unless broken: flag `0x200000`, dust, 24 debris pieces (`0x003a5530`), splinters, nearby objects broken (`World_BreakObjectsAround`), path polygon flag 8 at the spot, the material sound unless game-state flag 4; four model hashes break outright; interval 2, broken | confirmed (code) |
| `0x003cc098` | `DynTable_OnMessage` | `dyn_table` message | 1 the hit; 10 hittable or not (`Object_SetHittable`); `0x15` flag `0x40`, interval 2 | confirmed (code) |
| `0x003cc158` | `DynTable_Update` | `dyn_table` update | done (1) once broken; after removal counts to 4; on each knock (`0x20000000`) counts, and at the fourth clears flags `0x6000000` and goes back to every 60 ticks | confirmed (code) |

### `dyn_bar_lamp` and `sub_barlamp_glow` {#bar-lamp}

A hanging bar lamp (`dyn_bar_lamp`; its init `0x003cc3d0` is not a function in the database) with two glow sprites
(`sub_barlamp_glow`) as children (`+0x00`, `+0x04`). A glow's record: `+0x00` its sprite batch, `+0x04` removed.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003cc510` | `DynBarLamp_OnMessage` | `dyn_bar_lamp` message | message 1 (after popping the hit) and `0x15` remove the lamp; `0x20` sends `0x15` to both glows | confirmed (code) |
| `0x003cc650` | `DynBarLamp_Update` | `dyn_bar_lamp` update | does nothing; returns 0 | confirmed (code) |
| `0x003cc680` | `SubBarlampGlow_Init` | `sub_barlamp_glow` init | every 40 ticks, white, scale 0.3; with argument 1 turned 90° (a quaternion of 0.707), scale 0.45 and a tinted colour; sprite rectangle 3 of its batch | confirmed (code) |
| `0x003cc7e0` | `SubBarlampGlow_OnMessage` | `sub_barlamp_glow` message | `0x15`: frees the batch, colour alpha 0, removed | confirmed (code) |
| `0x003cc858` | `SubBarlampGlow_Update` | `sub_barlamp_glow` update | done once removed | confirmed (code) |

### `dyn_exploding_car` {#exploding-car}

A parked wreck that blows up (type 33, flags `0x208001`). Record: `+0x00` state (20 idle, 11 shaking, 10 exploding,
−5 removed), `+0x04` the explosion step, `+0x08` a shake counter, `+0x0c` the update interval, `+0x10` from flag set
3 bit 1, `+0x14` hits left before it explodes (3 for model `0xc4cb2a87`, which also updates every 60 ticks), `+0x18` a
child task (its lights). Confirmed (code) at `0x003cc8b8`, `0x003cca88`, `0x003ccf68`.

- **A hit** (message 1, unless already exploding, `0x800000`): one hit fewer; while hits are left and the kind is 0
  it **shakes** (state 11, 20-30 ticks; the last one sets `+0x08` to −8); otherwise, when idle, it starts
  **exploding**: state 10, every 2 ticks, flag `0x800000`.
- **Exploding** (update, one step per update): step 2 drops the child and (except model `0xc4cb2a87`) throws it with a
  random spin; step 22 breaks the glass within 10 m (`0x003a5810` → `World_BreakGlassInRadius`), plays one of three
  sounds and takes the wrecked model; step 45 goes back to idle, every 180 ticks.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003cc8b8` | `DynExplodingCar_Init` | `dyn_exploding_car` init | the record above; world object `+0x124` = 0 (`0x003a4cb0`) | confirmed (code) |
| `0x003cca88` | `DynExplodingCar_OnMessage` | `dyn_exploding_car` message | 1 the hit; `0x12` / `0x13` set world object `+0x124` to 1 / 0 and pass the message to the child; `0x15` state −5; `0x19` flag `0x8000`; `0x20` removes the child; `0x30` knocked: spin 2-5 rad/s, flags `0x4200000`; `0x3c` removes the child and reloads the model | confirmed (code) |
| `0x003ccf68` | `DynExplodingCar_Update` | `dyn_exploding_car` update | the shake and explosion steps above; done (1) at state −5; a knock clears the velocity | confirmed (code) |

### `dyn_fluor_swing` and `sub_moving_light` {#fluor-swing}

A swinging fluorescent tube: its init makes a swinging part (`dyn_swinging_obj`) and a `sub_moving_light` attached to
it (inferred from the names it creates) and sends the light three `0x19` colour words and two `0x35` messages.
Children at `+0x00`, `+0x04`, `+0x08`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003cd7c0` | `DynFluorSwing_Init` | `dyn_fluor_swing` init | flags `0x401`, every 30 ticks; the children above | confirmed (code); the roles inferred |
| `0x003cdae8` | `DynFluorSwing_OnMessage` | `dyn_fluor_swing` message | `0x15` or `0x20`: removes its three children (`0x003a57d8`) | confirmed (code) |
| `0x003cd6c8` | `SubMovingLight_Init` | `sub_moving_light` init | a light (flags `0x98`, range 6 m at task `+0x8c`) attached to the popped parent (`0x003a25b8`, `0x003a25b0`), every 30 ticks | confirmed (code) |

### `dyn_lizzies`: the props of the Lizzies' bar {#lizzies}

One class for the jukebox and the wall pieces of the Lizzies' bar (`dyn_jukebox`, `dyn_lizzywall_a`,
`dyn_lizzywall_in...`; the class tests the object's type name, `Obj_IsTypeName` `0x003a5998`). Record: `+0x00`
state (−5 done), `+0x04` from flag set 3 bit 1, `+0x08` hit points (the type's `CfgObj` property 0).

**The hit** (`0x003cdca8`, message 1; 15 KB). It pops the hit and sets flag `0x800000`. A `dyn_jukebox` gets a dust
puff (scale 0.2) in two colours. A wall piece goes through damage stages by its **model hash**: each stage swaps the
model for the next damaged one (about a dozen hash pairs), with a dust burst, debris pieces at set offsets and, unless
game-state flag 4, the type's material sound (`0x003a37b8` property 3); one model spins two pieces off (message `0x30`
to them) and takes a final model; three models of `dyn_lizzywall_a` reload their model by hash, one clears body flag
`0x40` (`0x003a4810`) and one loses its body. Message `0x15` pushes a hit and sends itself message 1. Confirmed (code)
for the calls; which stage each hash is was not traced (the decompiler drops several of the conditions).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003cdb90` | `DynLizzies_Init` | `dyn_lizzies` init | model, flags `0x401`, every 20 ticks, the record above | confirmed (code) |
| `0x003cdca8` | `DynLizzies_OnMessage` | `dyn_lizzies` message | 1 the hit above; `0x15` a hit sent to itself | confirmed (code); the stage order inferred |
| `0x003d17f8` | `DynLizzies_Update` | `dyn_lizzies` update | done once its state is −5 | confirmed (code) |

### `dyn_manqheadred`: the mannequin head {#manqheadred}

A pickable, throwable mannequin head (flags `0x228081`, every 2 ticks) that is **armed by a hit** and blows up.
Record `+0x20` the holder, `+0x24` state (−5 armed), `+0x28` armed time in ticks, `+0x2c` the colour stage
(−1, −2, 1, 2). Confirmed (code) at `0x003d1830`, `0x003d1908`, `0x003d1f38`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d1830` | `DynManqheadred_Init` | `dyn_manqheadred` init | model; `+0x20`-`+0x28` cleared, `+0x2c` = −1 | confirmed (code) |
| `0x003d1908` | `DynManqheadred_OnMessage` | `dyn_manqheadred` message | 0 offered to a taker: flag `0x100000`, message `0x14` back to him; 1 a hit: colour `0xff00ff`, armed unless the kind is −2; 8 clears `0x10`; 10 hittable; `0x19` flag `0x8000`; `0x1b` put in a hand (attached, velocity cleared, messages 3 and `0x17` with weight 1.0 and its property 3 to the holder); `0x1c` thrown (flag `0x4000000`, a downward spin); `0x30` knocked | confirmed (code) |
| `0x003d1f38` | `DynManqheadred_Update` | `dyn_manqheadred` update | once armed, counts ticks: at 100 a colour change, at 200 another and a random spin each update; at 300 an explosion (`0x003a4b50` → `Explosion_DamageHumansInRadius`, 2 m, its property 0) and done; a landing (`0x20000000`) arms it; done if it falls below −500 m | confirmed (code) |

### Neon signs: `dyn_neon_broken`, `dyn_neon_onesided`, `dyn_oneon_glow` {#neon}

Three neon classes that flicker their `sub_neon_light2` children. `dyn_neon_onesided` and `dyn_oneon_glow` are the
same code at two addresses. The flicker record, confirmed (code) at `0x003d2320`, `0x003d2b68`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | steady spell length (8, or 15 for the one-sided; then 5 + 0-10 or 0-15) |
| `+0x04` | the counter |
| `+0x08` | flicker phase (0, 1 fast, 2 slow) |
| `+0x0c` | flickering |
| `+0x10` | colour `0xeac9acff` |
| `+0x18` (broken) / `+0x20` (one-sided) | "changed": tell the lights |
| `+0x18` / `+0x1c` (one-sided) | pulse direction and step |
| `+0x1c`-`+0x28` (broken, four) / `+0x24`, `+0x28` (one-sided, two) | the child lights |

The update: a steady spell (the one-sided ones pulse their glow by ±10 per update, reversing every 4), then a burst
that alternates every 9 and 3 ticks for a random number of steps, then back to steady (every 30 ticks); on each change
the sign's colour word and its lights are set on or off.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d2320` | `DynNeonBroken_Init` | `dyn_neon_broken` init | model, flags `0x401`, every 60 ticks, four lights, the record | confirmed (code) |
| `0x003d2768` | `DynNeonBroken_OnMessage` | `dyn_neon_broken` message | `0x15` / `0x20`: removes the four lights | confirmed (code) |
| `0x003d2820` | `DynNeonBroken_Update` | `dyn_neon_broken` update | the flicker above | confirmed (code) |
| `0x003d2b68` | `DynNeonOnesided_Init` | `dyn_neon_onesided` init | model, every 30 ticks, two lights, the record | confirmed (code) |
| `0x003d2e48` | `DynNeonOnesided_OnMessage` | `dyn_neon_onesided` message | `0x15` / `0x20`: removes the two lights | confirmed (code) |
| `0x003d2ed0` | `DynNeonOnesided_Update` | `dyn_neon_onesided` update | the pulse and flicker above | confirmed (code) |
| `0x003d31a0` | `DynOneonGlow_Init` | `dyn_oneon_glow` init | as `0x003d2b68` | confirmed (code) |
| `0x003d3458` | `DynOneonGlow_OnMessage` | `dyn_oneon_glow` message | as `0x003d2e48` | confirmed (code) |
| `0x003d34e0` | `DynOneonGlow_Update` | `dyn_oneon_glow` update | as `0x003d2ed0` | confirmed (code) |

### `dyn_on_off` and `on_off_light` {#on-off}

A switchable lamp: `dyn_on_off` holds one `on_off_light` (`+0x00`) and sets its colour by message. The light's record:
`+0x00` blink phase, `+0x04` its colour, `+0x08` on.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d3968` | `DynOnOff_Init` | `dyn_on_off` init | model, flags `0x401`, every 20 ticks, the `on_off_light` child | confirmed (code) |
| `0x003d3af0` | `DynOnOff_OnMessage` | `dyn_on_off` message | 8 clears `0x10`; 10 hittable; `0x12` sends the light colour `0xff0000ff`, `0x13` `0x00ff00ff` (message `0x19`); `0x15` / `0x20` remove it | confirmed (code) |
| `0x003d37b0` | `OnOffLight_Init` | `on_off_light` init | a light (flags `0x488`, `0xff00ff`, range 0.2), every 20 ticks | confirmed (code) |
| `0x003d3870` | `OnOffLight_OnMessage` | `on_off_light` message | `0x19`: its colour | confirmed (code) |
| `0x003d38e0` | `OnOffLight_Update` | `on_off_light` update | done when off; otherwise its colour on step 1 and black on step 2 (a blink) | confirmed (code) |

### `dyn_pile`: piles you take from {#dyn-pile}

A pickable pile (flags `0x228481`). `dyn_donut_spawner` has no model. Record `+0x14` takes allowed (5; message
`0x22`), `+0x18` takes so far, `+0x1c` −5 when empty.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d3c28` | `DynPile_Init` | `dyn_pile` init | every 20 ticks; model unless `dyn_donut_spawner`; the record | confirmed (code) |
| `0x003d3d30` | `DynPile_OnMessage` | `dyn_pile` message | 0 taken: message `0x14` to the taker; for model `0x69b9d1a0` one more take, and at the limit flag `0x8000` toggled, every 240 ticks, empty; 1 pops a hit only; `0x19` flag `0x8000`; `0x1b` message 3 to the holder; `0x1c` released; `0x22` the limit; `0x30` knocked | confirmed (code) |
| `0x003d4058` | `DynPile_Update` | `dyn_pile` update | done once empty and its colour word `+0xc8` is 0; done below −500 m when thrown | confirmed (code) |

### `dyn_walktalk`: the walkie-talkie {#walktalk}

A pickable radio (flags `0x228001`, every 20 ticks) that plays chatter. Record: `+0x20` state (−5 broken, 1 thrown),
`+0x24` interval, `+0x28` / `+0x2c` time and the next chatter ((4 + 0-5) × 60 ticks), `+0x30` the clip index (0-29),
`+0x34` the playing sound.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d4100` | `DynWalktalk_Init` | `dyn_walktalk` init | model, the record | confirmed (code) |
| `0x003d41f8` | `DynWalktalk_OnMessage` | `dyn_walktalk` message | 0 offered to a taker; 1 a hit: dust (1.75), broken unless the kind is −2; 4 a knock (interval 2); 8; 10 hittable; `0x12` / `0x19` a random clip (0-29) when none plays; `0x1b` in a hand (as the mannequin head's); `0x1c` thrown; `0x30` knocked | confirmed (code) |
| `0x003d48e8` | `DynWalktalk_Update` | `dyn_walktalk` update | broken: stops its sound, done; else at each chatter time stops the last clip and plays the next of 30 in turn; a thrown radio lands (flags `0x4000000`, `0x10`) | confirmed (code) |

### Blood spray {#blood-spray}

`part_blood_spray` sticks to a human at one of seven attach points (table `0x00514558`) and sprays `sub_blood_spray`
sprites. Related types are on [Blood](#blood).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d4f60` | `PartBloodSpray_Init` | `part_blood_spray` init | every 2 ticks, flags `0x14`, attached to the popped parent at a random point of the seven | confirmed (code) |
| `0x003d50d0` | `PartBloodSpray_Update` | `part_blood_spray` update | eight steps; on steps 1-5 a `sub_blood_spray` while game state `+0x454` is set (not traced) | confirmed (code) |
| `0x003d4c10` | `SubBloodSpray_Init` | `sub_blood_spray` init | dark red `0x7a0015ff`, scale 0.3, every 2 ticks, rectangle `0x10006` | confirmed (code) |
| `0x003d4dd0` | `SubBloodSpray_Update` | `sub_blood_spray` update | grows 0.04 and loses 5 of alpha per update; done when faded | confirmed (code) |

### Small emitters {#small-emitters}

Types whose init only places them (flags 4) and, for some, adds a looping sound emitter (`Sound_AddScriptEmitter`
`0x001172c8`); their update and message are empty stubs.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d5310` | `PartCokeMachine_Init` | `part_coke_machine` init | flags 4, every 2 ticks, position | confirmed (code) |
| `0x003d7230` | `PartFear_Init` | `part_fear` init | flags 4, every 240 ticks, position | confirmed (code) |
| `0x003d8240` | `PartGasmeter_Init` | `part_gasmeter` init | a sound emitter, every 2 ticks | confirmed (code) |
| `0x003d9178` | `PartLargeAc_Init` | `part_large_ac` init | a sound emitter, every 2 ticks | confirmed (code) |
| `0x003d9228` | `PartLargeAcTwo_Init` | `part_large_ac_two` init | a sound emitter, every 2 ticks | confirmed (code) |

### Cop car lights {#cop-lights}

`part_copcar_lights` (every 240 ticks) owns three `coplights_glow` children (`+0x0c`-`+0x14`) and a sprite batch
(`+0x08`); `+0x00` is −1 while off. A `coplights_glow` record: `+0x10` its interval, `+0x14` the flash phase.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d5af0` | `PartCopcarLights_Init` | `part_copcar_lights` init | the three glows at set offsets, the batch, off | confirmed (code) |
| `0x003d5f68` | `PartCopcarLights_OnMessage` | `part_copcar_lights` message | `0x12` (when off) sends 10 with 1 to each glow and clears flag 4; `0x13` sends 10 with 0 and sets flag 4; `0x15` removes the glows; `0x40` passes on | confirmed (code) |
| `0x003d6480` | `PartCopcarLights_Update` | `part_copcar_lights` update | once removed (`0x800000`) frees the batch and ends | confirmed (code) |
| `0x003d5618` | `CoplightsGlow_Init` | `coplights_glow` init | pops rectangle, interval, a start phase and a colour; attached; flags `0x220`; starts bright (scale 1.43, alpha `0x6f`) or dim | confirmed (code) |
| `0x003d5788` | `CoplightsGlow_OnMessage` | `coplights_glow` message | 10 hittable; `0x15` removed | confirmed (code) |
| `0x003d5860` | `CoplightsGlow_Update` | `coplights_glow` update | while on (flag 4 clear) alternates bright and dim; removed: frees its batch, done | confirmed (code) |
| `0x003d53c0` | `CoplightsLensFlare_Init` | `coplights_lens_flare` init | an attached sprite: rectangle, interval, colour; rectangle `0x80002` | confirmed (code) |
| `0x003d54e0` | `CoplightsLensFlare_OnMessage` | `coplights_lens_flare` message | `0x15`: removed | confirmed (code) |
| `0x003d5538` | `CoplightsLensFlare_Update` | `coplights_lens_flare` update | the first update dims it (alpha 0, scale 0.75 / 0.25); the next ends it | confirmed (code) |

### Explosion group and screen shake {#explosion-group}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d64d8` | `SubExplosionGroup_Init` | `sub_explosion_group` init | flags `0x404`; when the play-phase particle manager has room (`0x003a5a50`), twelve `sub_fade_flame` at random offsets round the popped point | confirmed (code) |
| `0x003d70e8` | `PartExplosionScreenShake_Init` | `part_explosion_screen_shake` init | for every view, the camera's shake (camera vtable `+0x1d4` with 0.95, `+0x1dc` with 0.55; `0x003a4608`, `0x003a4668`) | confirmed (code); the two values' roles inferred |
| `0x003d71e8` | `PartExplosionScreenShake_Update` | `part_explosion_screen_shake` update | done on its first update | confirmed (code) |

### Fire truck lights {#firetruck}

`part_firetruck_lights` makes rotating red lights (`sub_firetruck_light`) and a `coplights_glow`, children at `+0x0c`
and `+0x10`, and starts off (it sends itself `0x13`). A `sub_firetruck_light` record: `+0x00` phase (3 off), `+0x04`
its pattern.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d7678` | `PartFiretruckLights_Init` | `part_firetruck_lights` init | every 240 ticks, the children, off | confirmed (code) |
| `0x003d7920` | `PartFiretruckLights_OnMessage` | `part_firetruck_lights` message | `0x12` on and `0x13` off (message 10 to both children, flag 4); `0x15` removes them | confirmed (code) |
| `0x003d7af0` | `PartFiretruckLights_Update` | `part_firetruck_lights` update | done once removed | confirmed (code) |
| `0x003d72d0` | `SubFiretruckLight_Init` | `sub_firetruck_light` init | a red light (`0xff0000ff`, range 15 m), attached, turned by half the popped angle, interval and pattern popped | confirmed (code) |
| `0x003d74a0` | `SubFiretruckLight_OnMessage` | `sub_firetruck_light` message | 10 on or off (phase 3, flag 4); `0x15` removed | confirmed (code) |
| `0x003d7568` | `SubFiretruckLight_Update` | `sub_firetruck_light` update | flips its colour between red and off by phase and pattern | confirmed (code) |

### `part_fire_plume` {#fire-plume}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d7b38` | `PartFirePlume_Init` | `part_fire_plume` init | flags `0x400400`, a sprite batch, scale 12, colour `0xffffff80`; pause of 30-45 | confirmed (code) |
| `0x003d7c38` | `PartFirePlume_OnMessage` | `part_fire_plume` message | 8 clears `0x10` | confirmed (code) |
| `0x003d7cd8` | `PartFirePlume_Update` | `part_fire_plume` update | every 4-5 ticks the next of 19 frames of batch 9; then a pause of (pause × 35) updates with a sound | confirmed (code) |

### Flies and bugs {#flies}

Both swarm types keep three `sub_polar_bugs` children (`+0x14`, `+0x18`, `+0x1c`) while a view is within range of
them (`0x003a51f8`), and drop them when none is. `part_garbage_flies` also has a looping sound.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d7e30` | `PartGarbageFlies_Init` | `part_garbage_flies` init | flags 4, every 60 ticks, a sound emitter | confirmed (code) |
| `0x003d7f10` | `PartGarbageFlies_Update` | `part_garbage_flies` update | the three swarms (every 60 ticks), or none (every 120) | confirmed (code) |
| `0x003d8180` | `PartGarbageFliesNs_Init` | `part_garbage_flies_ns` init | spawns one `part_garbage_flies` at its position | confirmed (code) |
| `0x003d9ca0` | `PartLightBugs_Init` | `part_light_bugs` init | flags 4, every 60 ticks, no swarms yet | confirmed (code) |
| `0x003d9d50` | `PartLightBugs_Update` | `part_light_bugs` update | as the garbage flies, without the sound | confirmed (code) |

### `part_generator_sparks` {#generator-sparks}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d82f0` | `PartGeneratorSparks_Init` | `part_generator_sparks` init | flags `0x0c`, every 180 ticks, a flash sprite (scale 12), a burst of 10-15 | confirmed (code) |
| `0x003d83d8` | `PartGeneratorSparks_Update` | `part_generator_sparks` update | while a camera is near (`0x003a5280`), every 10-60 ticks a flash (scale 8-12 on odd types), a sound and a `sub_spark_effect`; after the burst a new count; back to every 180 ticks when no camera is near | confirmed (code) |

### Flickering lights: ghost and lava {#flicker-lights}

`part_ghost_light` and `part_lava_light` each own one light child (`+0x00`) and switch it with `0x12` / `0x13`
(message 10 with 1 or 0). The lights (range 4 m, every 5 ticks) toggle between two colours.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d8818` | `PartGhostLight_Init` | `part_ghost_light` init | flags 4, every 180 ticks, a `sub_ghost_light` | confirmed (code) |
| `0x003d88d8` | `PartGhostLight_OnMessage` | `part_ghost_light` message | `0x12` / `0x13`: the light on or off | confirmed (code) |
| `0x003d85f0` | `SubGhostLight_Init` | `sub_ghost_light` init | colour `0x27c584e4`, range 4 m | confirmed (code) |
| `0x003d8698` | `SubGhostLight_OnMessage` | `sub_ghost_light` message | 10: on or off (phase 3, flag 4) | confirmed (code) |
| `0x003d8730` | `SubGhostLight_Update` | `sub_ghost_light` update | every 20-35 ticks toggles between `0x27c584e4` and `0xe4` | confirmed (code) |
| `0x003d9508` | `PartLavaLight_Init` | `part_lava_light` init | flags 4, every 180 ticks, a `sub_lava_light` | confirmed (code) |
| `0x003d95c8` | `PartLavaLight_OnMessage` | `part_lava_light` message | `0x12` / `0x13`: the light on or off | confirmed (code) |
| `0x003d92d8` | `SubLavaLight_Init` | `sub_lava_light` init | colour `0xff00e4`, range 4 m | confirmed (code) |
| `0x003d9380` | `SubLavaLight_OnMessage` | `sub_lava_light` message | 10: on or off | confirmed (code) |
| `0x003d9418` | `SubLavaLight_Update` | `sub_lava_light` update | every 10-15 ticks toggles between two warm colours | confirmed (code) |

### Gun flash {#gun-flash}

`part_gun_flash` is one shot's flash; it spawns the two short-lived parts below. See also [Shake and gun
flash](#sub-shk).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d8bd8` | `PartGunFlash_Init` | `part_gun_flash` init | flags 1, every 2 ticks, a batch on sheet record 8 (`lighting`), rectangle 2, colour `0xfafec9ff`; a dust puff of 0.5-0.75 | confirmed (code) |
| `0x003d8da0` | `PartGunFlash_Update` | `part_gun_flash` update | step 0 spawns `sub_muzzle_flash` and `gun_light_flash` at a random roll; step 1 every 10 ticks, scale 0.35; step 2 frees the batch and ends | confirmed (code) |
| `0x003d8ab8` | `SubMuzzleFlash_Init` | `sub_muzzle_flash` init | flags `0x400001`, every 4 ticks, scale 18, rectangle `0x10023` | confirmed (code) |
| `0x003d8b90` | `SubMuzzleFlash_Update` | `sub_muzzle_flash` update | done on its second update | confirmed (code) |
| `0x003d89a8` | `GunLightFlash_Init` | `gun_light_flash` init | a light (`0xff9600ff`, range 10 m), every 5 ticks | confirmed (code) |
| `0x003d8a70` | `GunLightFlash_Update` | `gun_light_flash` update | done on its second update | confirmed (code) |

### The level 2 subway {#subway}

`part_level2_subway` moves through ten key positions (`0x006f2e00`, 16 bytes each) with per-step intervals
(`0x00514588`): a passing train's light and noise. Record: `+0x00` the key, `+0x04` the pass step, `+0x08` running,
`+0x0c` its `subway_loop` sound child. A `subway_loop`: `+0x00` the sound, `+0x04` stopping, `+0x08` the parent,
`+0x0c` which sound.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d98b0` | `PartLevel2Subway_Init` | `part_level2_subway` init | flags `0x200`, at the first key, idle | confirmed (code) |
| `0x003d9978` | `PartLevel2Subway_OnMessage` | `part_level2_subway` message | `0x12` starts a pass from the first key; `0x13` sends `0x13` to the sound and stops | confirmed (code) |
| `0x003d9a48` | `PartLevel2Subway_Update` | `part_level2_subway` update | while running steps through the keys; on the first step spawns the `subway_loop`; within 50 m of a camera every view shakes (0.02, 4.0) and `sub_spark_effect` spawns | confirmed (code) |
| `0x003d9698` | `SubwayLoop_Init` | `subway_loop` init | the parent, every 2 ticks, no sound yet | confirmed (code) |
| `0x003d9710` | `SubwayLoop_OnMessage` | `subway_loop` message | `0x13` stop; `0x27` which sound | confirmed (code) |
| `0x003d97a0` | `SubwayLoop_Update` | `subway_loop` update | keeps a 3D sound (`0x326071de`, or `0x26ac304b` when `+0x0c` is 0) playing at the parent's place; on stop, stops it and ends | confirmed (code) |

### Helpers {#chunk-c-helpers}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003cc888` | `Vec3_Dot` | helper (11 callers among the object types) | the dot product of two vectors' x, y, z | confirmed (code) |

### Ambient sound emitters and the shack dust {#ambient-sound-emitters}

Hidden particle types that place a sound in the world. Each initialiser pops its parent and position like any
emitter ([How the effect types are built](#effect-pattern)), hides itself (flags `+0x54` = 4) and, for the plain
hums, adds a looping script sound emitter at its position (`Sound_AddScriptEmitter`); their update and message slots
are empty stubs outside this list. The others react to the common switch messages of the effect types: **`0x12`**
switches on and **`0x13`** switches off. `part_motorbike_gang` also shakes the player cameras through
`0x003a4608` (amount) and `0x003a4668` (rate), the camera vtable's `+0x1d4` and `+0x1dc`, the same pair `sub_shk`
uses ([Shake and gun flash](#sub-shk)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003d9fc8` | `PartMotorbikeGang_Init` | `part_motorbike_gang` init | pops parent and position, size 0, counter −1 (idle), update every 225 ticks | confirmed (code) |
| `0x003da078` | `PartMotorbikeGang_OnMessage` | `part_motorbike_gang` message | `0x12`: plays its sound, counter 0, update every 2; `0x13`: idle, shake off | confirmed (code) |
| `0x003da158` | `PartMotorbikeGang_Update` | `part_motorbike_gang` update | counts to 280 updates; from 250 the cameras shake (0.15, rate 1.5, update every 30); then shake off and idle | confirmed (code); that it is a passing motorbike gang is inferred from the name |
| `0x003da278` | `PartOpenAirVent_Init` | `part_open_air_vent` init | hidden, update every 2, adds a script sound emitter | confirmed (code) |
| `0x003da940` | `PartRidecartSound_Init` | `part_ridecart_sound` init | hidden, update every 2, no sound yet (data `+0x10` = 0) | confirmed (code) |
| `0x003da9e0` | `PartRidecartSound_OnMessage` | `part_ridecart_sound` message | `0x12`: starts its sound once, at the camera's position; 8: clears flag `0x10` (detached) | confirmed (code) |
| `0x003daab0` | `PartRidecartSound_Update` | `part_ridecart_sound` update | moves the playing sound to the task's position | confirmed (code) |
| `0x003dab08` | `PartRotateVent_Init` | `part_rotate_vent` init | hidden, update every 2, adds a script sound emitter | confirmed (code) |
| `0x003dabb8` | `PartSmallAc_Init` | `part_small_ac` init | hidden, update every 2, adds a script sound emitter | confirmed (code) |
| `0x003dcb68` | `PartTransFive_Init` | `part_trans_five` init | hidden, update every 2, adds a script sound emitter | confirmed (code); a transformer hum is inferred from the name |
| `0x003dcc18` | `PartTransFour_Init` | `part_trans_four` init | the same | confirmed (code) |
| `0x003dccc8` | `PartTransOne_Init` | `part_trans_one` init | the same | confirmed (code) |
| `0x003dcd78` | `PartTransThree_Init` | `part_trans_three` init | the same | confirmed (code) |
| `0x003dce28` | `PartTransTwo_Init` | `part_trans_two` init | the same | confirmed (code) |
| `0x003dac68` | `PartSShackDustPuff_Init` | `part_s_shack_dust_puff` init | flags `0x404`, update every 180, attached to its parent | confirmed (code) |
| `0x003dacf0` | `PartSShackDustPuff_OnMessage` | `part_s_shack_dust_puff` message | `0x12`: a dust puff of radius 4 (`Particles_Dust`); `0x19`: a dust puff of the popped radius | confirmed (code) |

### Neon signs {#neon-signs}

`part_orange_neon` and `part_pink_neon` only place a `sub_neon_light` and end. The lights (kind `0x04`) keep two
colours in their light record (`+0x80` and `+0x84`, the colour drawn) and a radius pair (`+0x8c`, `+0x90`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003da328` | `PartOrangeNeon_Init` | `part_orange_neon` init | flags `0x404`, update every 60, spawns a `sub_neon_light` at its position (`ScriptType_CreateByName`) | confirmed (code) |
| `0x003da818` | `PartPinkNeon_Init` | `part_pink_neon` init | the same | confirmed (code) |
| `0x003da910` | `PartPinkNeon_Update` | `part_pink_neon` update | returns 1: the sign task ends after its first update | confirmed (code) |
| `0x003e03b0` | `SubNeonLight_OnMessage` | `sub_neon_light` message | does nothing | confirmed (code) |
| `0x003e03d8` | `SubNeonLight_Update` | `sub_neon_light` update | random flicker: every few updates swaps the drawn colour between its two (data `+0x08`, `+0x0c`), with a random off spell | confirmed (code) |
| `0x003e0538` | `SubNeonLight2_Init` | `sub_neon_light2` init | pops parent, radius and colour; with a parent it is attached at (0, −1.5, −1); update every 15 | confirmed (code) |
| `0x003e0648` | `SubNeonLight2_OnMessage` | `sub_neon_light2` message | `0x19`: a new colour, applied on the next update; `0x22`: the update interval | confirmed (code) |
| `0x003e0708` | `SubNeonLight2_Update` | `sub_neon_light2` update | applies a pending colour | confirmed (code) |

### The subway and the train {#subway-train}

`part_pelham_subway` runs a train's passing effects along a fixed path: a static table (`TaskTypes_InitConstantTables`,
run once at start-up) holds 13 points at `0x006f2ea0` (16 bytes each; x 54 to 84, y 250 down to −80, z 4), and
`0x005145b0` the update interval for each point. Each run starts at point 8 and steps along the points; at each step
it may spawn a `subway_loop` sound and a `sub_spark_effect` and, within 30 m of a camera, shakes it (0.02, rate 4).
The same constructor fills the spark direction table at `0x006f2f30` (16 unit vectors) used by
`sub_car_spark_emitter`. `part_train_sound` and `part_train_splat` are the train's sound and a body hit by it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003da428` | `PartPelhamSubway_Init` | `part_pelham_subway` init | flags `0x200`, size 1, position = path point 0, data: point 8, not running | confirmed (code) |
| `0x003da4f0` | `PartPelhamSubway_OnMessage` | `part_pelham_subway` message | `0x12`: a run from point 8; `0x13`: forwards `0x13` to its sound child and stops | confirmed (code) |
| `0x003da5c0` | `PartPelhamSubway_Update` | `part_pelham_subway` update | steps the path point (rotation from the point table), spawns `subway_loop` and `sub_spark_effect`, camera shake within 30 m | confirmed (code); the path's meaning (one train line) inferred |
| `0x003e21c8` | `TaskTypes_InitConstantTables` | static data | writes the subway points, the spark directions and further unit vectors and quaternions (`0x006f2e00`-`0x006f315c`) | confirmed (code) |
| `0x003e29c8` | `TaskTypes_StaticInit` | static constructor | calls the above with (1, `0xffff`) | confirmed (code) |
| `0x003dadc0` | `SubwaySpark_Init` | `subway_spark` init | pops sprite and speed (÷ 60), size 0.02, colour `0xffff88ff`, one of three rectangles (`0x10036`+) | confirmed (code) |
| `0x003daf38` | `SubwaySpark_Update` | `subway_spark` update | a random direction at birth; fades from step 90; slows to every 60 ticks at 179; ends at step 180 | confirmed (code) |
| `0x003db148` | `SubwaySparkLightFlash_Init` | `subway_spark_light_flash` init | light (flags 8), white, radius 12, attached to its parent, update every 5 | confirmed (code) |
| `0x003db210` | `SubwaySparkLightFlash_Update` | `subway_spark_light_flash` update | step 1 dims it (update every 80); ends at step 2 | confirmed (code) |
| `0x003db298` | `PartSSubwaySparks_Init` | `part_s_subway_sparks` init | update every 240, size 0, its own sprite batch (`PTank_New`) | confirmed (code) |
| `0x003db368` | `PartSSubwaySparks_OnMessage` | `part_s_subway_sparks` message | `0x12`: a sound and a burst of 30-35 `subway_spark` pieces and a `subway_spark_light_flash` | confirmed (code) |
| `0x003db4e0` | `PartSSubwaySparks_Update` | `part_s_subway_sparks` update | done once its burst counter is −1 | confirmed (code) |
| `0x003db518` | `PartTrainSound_Init` | `part_train_sound` init | hidden, update every 2, sound and timers cleared | confirmed (code) |
| `0x003db5c0` | `PartTrainSound_OnMessage` | `part_train_sound` message | `0x12`: starts its sound once; 8: clears flag `0x10` | confirmed (code) |
| `0x003db690` | `PartTrainSound_Update` | `part_train_sound` update | moves the sound; picks one of four sounds from the table `0x005145f0` (levels whose id is `0x1f` or `0x57`) or `0x00514608`; each view's camera shakes by (60 − distance) × 0.00424 (0.25 near), rate 0.95; 1 in 100 a `sub_spark_effect` | confirmed (code); which levels the two ids are not traced |
| `0x003dbb48` | `SubTrainSplat_Init` | `sub_train_splat` init | a blood drop: size 0.45, colours `0x20050500` / `0x20050580`, random rectangle, life 180 ticks | confirmed (code) |
| `0x003dbcf0` | `SubTrainSplat_Update` | `sub_train_splat` update | random growth at birth; when its direction meets a surface (an absolute dot product of 0.9 or more with up or forward) its size drops to 0.4 ×; ends after step 3 or when the pool is short | confirmed (code); "landing" inferred |
| `0x003dc018` | `SubBloodGout_Init` | `sub_blood_gout` init | pops sprite and direction, size 0.25, 25-40 drops to throw | confirmed (code) |
| `0x003dc170` | `SubBloodGout_Update` | `sub_blood_gout` update | one `sub_train_splat` per update (every 4-6 ticks) until the count is reached | confirmed (code) |
| `0x003dc370` | `SubTrainSplatMist_Init` | `sub_train_splat_mist` init | pops a size, a mist puff of 1.5 × it, update every 4 | confirmed (code) |
| `0x003dc4e8` | `SubTrainSplatMist_Update` | `sub_train_splat_mist` update | grows the puff for 7 steps while the pool allows | confirmed (code) |
| `0x003dc658` | `PartTrainSplat_Init` | `part_train_splat` init | a sprite batch, 10 `sub_blood_gout` fanned 15° apart and 12 `sub_train_splat_mist`, only while blood is on (game state `+0x454` = 0) | confirmed (code) |
| `0x003dcb10` | `PartTrainSplat_Update` | `part_train_splat` update | after 25 updates destroys its batch (`0x003a4f78`) and ends | confirmed (code) |

### TV sets {#tv-sets}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003dced8` | `PartTv_Init` | `part_tv` init | a screen quad (size 0.28, tilted −0.36 rad), its own batch on the TV sprite page, one of four frames (`0x70000`+) | confirmed (code) |
| `0x003dd068` | `PartTv_Update` | `part_tv` update | next of the four frames; off screen it hides (update every 120), on screen every 7-11 ticks | confirmed (code) |
| `0x003dd190` | `SubTvLight_Init` | `sub_tv_light` init | a light with two colours and a radius (at most 15), update every 5 | confirmed (code) |
| `0x003dd2c0` | `SubTvLight_OnMessage` | `sub_tv_light` message | 10: on or off (off = state 3, hidden); `0x15`: state 4 (end) | confirmed (code) |
| `0x003dd378` | `SubTvLight_Update` | `sub_tv_light` update | flickers between its two colours every 5-15 ticks (1-4 when its update interval is 14 or more); ends in state 4 | confirmed (code) |
| `0x003dd538` | `PartTvLight_Init` | `part_tv_light` init | flags `0x404`, update every 240, spawns a `sub_tv_light` (radius 5.6) | confirmed (code) |

### Urine {#urine}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003dd650` | `UrineSpray_Init` | `urine_spray` init | a drop: size 0.02, colour `0x66660048`, one of two rectangles | confirmed (code) |
| `0x003dd748` | `UrineSpray_Update` | `urine_spray` update | lives 5 steps while the pool allows | confirmed (code) |
| `0x003dd808` | `PartUrineSpray_Init` | `part_urine_spray` init | hidden emitter attached to its parent (the human), off | confirmed (code) |
| `0x003dd8b8` | `PartUrineSpray_OnMessage` | `part_urine_spray` message | `0x12`: on; `0x13`: off and its sound stopped | confirmed (code) |
| `0x003dd938` | `PartUrineSpray_Update` | `part_urine_spray` update | while on: a sound, `urine_spray` drops and a `part_urine_stain2`; the sound stops after 360 steps | confirmed (code) |
| `0x003ddb88` | `PartUrineStain2_Init` | `part_urine_stain2` init | a flat decal (90° about x), size 0, life 240 ticks | confirmed (code) |
| `0x003ddc70` | `PartUrineStain2_Update` | `part_urine_stain2` update | grows to 0.75, then 1.5, and fades; ends at step 3 | confirmed (code) |

### `rotating_object` {#rotating-object}

An object behaviour (type 157) for props that spin: fans, signs and the like. The initialiser sets the model and a
spin rate by model hash (three models turn at 3.0-3.3, one at 0.45-0.55 reversed, one at −4 to −5, the rest at
0.45-0.55); four models also reset their offset. Data: `+0x00` ramp state, `+0x04` ramp step (60 = done), `+0x08`
the looping sound.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ddd70` | `RotatingObject_Init` | `rotating_object` init | model, spin rate by model, flags `0x200001`, update every tick | confirmed (code) |
| `0x003de0b8` | `RotatingObject_OnMessage` | `rotating_object` message | 1: pops a hit; 8: detached; 10: hittable; `0x12` / `0x13`: start or stop the ramp for six models; `0x19`: flag `0x8000`; `0x1b` / `0x1c`: pops; `0x20`: stops its sound | confirmed (code) |
| `0x003de3d8` | `RotatingObject_Update` | `rotating_object` update | ramps the word at `+0xcc` by ±4 per update for 60 updates; plays a per-model looping sound (radius 25, or 20 for three models) while a camera is near | confirmed (code); what `+0xcc` drives is not traced |

### `sub_blight` and its glow {#blight}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003de638` | `SubBlightGlow_Init` | `sub_blight_glow` init | a glow sprite (rectangle `0x80003`), white, update every 60 | confirmed (code) |
| `0x003de6e0` | `SubBlight_Init` | `sub_blight` init | a light: pops colour and radius, spawns its `sub_blight_glow` | confirmed (code) |
| `0x003de858` | `SubBlight_OnMessage` | `sub_blight` message | `0x15`: radius × 3 and colour off, removes its glow and itself | confirmed (code) |

### `sub_flaming_debris` {#flaming-debris}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003de8f8` | `SubFlamingDebris_Init` | `sub_flaming_debris` init | its own batch and a `sub_fire2` child; flags `0x804` | confirmed (code) |
| `0x003dea50` | `SubFlamingDebris_OnMessage` | `sub_flaming_debris` message | `0x15`: flag `0x800000` and the message passed to both children; `0x40` passed on too | confirmed (code) |
| `0x003deb88` | `SubFlamingDebris_Update` | `sub_flaming_debris` update | done once flag `0x800000` is set | confirmed (code) |

### Car damage effects {#car-damage-effects}

`sub_car_damage` is one hidden particle shared by every car (the car manager makes it with the first car); the
car's hit code sends it message **`0x3f`** with a kind, a position and a direction, and it spawns the matching effect
there ([Cars: hit effects](cars.md#hit-effects)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003df698` | `SubCarDamage_Init` | `sub_car_damage` init | hidden, update every 2; made with no parent | confirmed (code) |
| `0x003df730` | `SubCarDamage_OnMessage` | `sub_car_damage` message | `0x3f` kind: 0 a `sub_car_spark_emitter` and dust; 1 about 4-10 `miniglass`; 2 the same as `sub_coloured_glass`, dark red; 3 `sub_car_steam`; 4 a small `sub_glass` shatter; 5 `sub_hood_smoke`; 6 a 2 × 1 `sub_glass` shatter ([Cars](cars.md#hit-effects)) | confirmed (code) |
| `0x003df228` | `SubCarSparkEmitter_Init` | `sub_car_spark_emitter` init | pops a strength; along each of the 16 table directions, 50 %: a `sub_car_sparks`; with strength ≥ 1 also 25 %: a `sub_car_rubble` | confirmed (code) |
| `0x003df038` | `SubCarSparks_Init` | `sub_car_sparks` init | a spark streak, grey `0xa4a4a4ff`, size 2.5-5 | confirmed (code) |
| `0x003df1b0` | `SubCarSparks_Update` | `sub_car_sparks` update | lives 15 steps while the pool allows | confirmed (code) |
| `0x003ded88` | `SubCarRubble_Init` | `sub_car_rubble` init | a debris piece, random spin, grey, size up to 1.5, one of five rectangles | confirmed (code) |
| `0x003deef8` | `SubCarRubble_Update` | `sub_car_rubble` update | falls until it lands; fades after 14 steps | confirmed (code) |
| `0x003debd0` | `SubCarSteamEmitter_Init` | `sub_car_steam_emitter` init | hidden, update every 2-4, 2-6 puffs | confirmed (code) |
| `0x003dec88` | `SubCarSteamEmitter_Update` | `sub_car_steam_emitter` update | one `sub_car_steam` per update (every 10-25 ticks) until four | confirmed (code) |

### Fire light and smoke {#fire-light-smoke}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003dfcc0` | `SubFireLight_Init` | `sub_fire_light` init | an orange light (`0xff00e4`), radius 7.5 × strength (at most 15), update every 5 | confirmed (code) |
| `0x003dfe18` | `SubFireLight_OnMessage` | `sub_fire_light` message | 10: on or off; `0x19`: the strength | confirmed (code) |
| `0x003dfee0` | `SubFireLight_Update` | `sub_fire_light` update | flickers between two oranges every 10-15 ticks | confirmed (code) |
| `0x003dffc0` | `SubFireSmoke_Init` | `sub_fire_smoke` init | pops a size, a smoke puff, update every 30 | confirmed (code) |
| `0x003e00a8` | `SubFireSmoke_Update` | `sub_fire_smoke` update | rises and grows over four steps, fades, ends | confirmed (code) |

### `sub_polar_bugs` and `sub_police_light` {#polar-bugs}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e07a0` | `SubPolarBugs_Init` | `sub_polar_bugs` init | a bug sprite circling a point (radius 6), random phases, update every 20 | confirmed (code) |
| `0x003e0938` | `SubPolarBugs_OnMessage` | `sub_polar_bugs` message | `0x15`: marks it done | confirmed (code) |
| `0x003e0980` | `SubPolarBugs_Update` | `sub_polar_bugs` update | moves on two angles (0.63 and 1.26 rad per update); ends when marked | confirmed (code) |
| `0x003e0b28` | `SubPoliceLight_Init` | `sub_police_light` init | a light attached to its parent, radius 5-10, flash interval popped | confirmed (code) |
| `0x003e0d00` | `SubPoliceLight_OnMessage` | `sub_police_light` message | `0x15`: removed; 10: on or off | confirmed (code) |
| `0x003e0e40` | `SubPoliceLight_Update` | `sub_police_light` update | alternates red and blue; dark in state 3 | confirmed (code) |

### Sparks {#spark-types}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e0f50` | `Spark_Init` | `spark` init | a streak, random speed (±3.5 across), size 0.01-0.03, white or yellow by size | confirmed (code) |
| `0x003e1250` | `Spark_Update` | `spark` update | bounces on contact five times, then a grounded glow for 60 ticks; at most 90 steps | confirmed (code) |
| `0x003e1430` | `SparkLightFlash_Init` | `spark_light_flash` init | a white light, radius 8-10, attached, update every 5 | confirmed (code) |
| `0x003e14f8` | `SparkLightFlash_Update` | `spark_light_flash` update | ends on its second update | confirmed (code) |
| `0x003e1578` | `SubSparkEffect_Init` | `sub_spark_effect` init | an emitter at a point, 1-2 bursts | confirmed (code) |
| `0x003e1648` | `SubSparkEffect_Update` | `sub_spark_effect` update | per burst, while on screen: a sound, 10-20 `spark` pieces and a `spark_light_flash` | confirmed (code) |

### `sub_swinging_obj` and `dyn_colasign` {#swinging-obj}

`dyn_colasign` (type 19) hangs a `dyn_swinging_obj` from itself and configures it with messages: `0x19` three times
(model, period 120, amplitude 1) and `0x35` twice. The swinging object's data: `+0x00` / `+0x10` the two rotations,
`+0x20` state (11-14 set-up, 2 and 3 the two swing directions), `+0x24` mode, `+0x28` period, `+0x2c` the step.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e1938` | `SubSwingingObj_Init` | `sub_swinging_obj` init | keeps the rest rotation as both ends, state 11, update every 30 | confirmed (code) |
| `0x003e1a08` | `SubSwingingObj_OnMessage` | `sub_swinging_obj` message | `0x19` set-up steps: model, period, amplitude; `0x35`: the swing parameters | confirmed (code) |
| `0x003e1c28` | `SubSwingingObj_Update` | `sub_swinging_obj` update | swings between its two rotations with a sine ease (every 5 ticks), or flips between them at the period; mode 2 adds a little sway | confirmed (code) |
| `0x003ea7b0` | `DynColasign_Init` | `dyn_colasign` init | spawns the `dyn_swinging_obj` child and configures it | confirmed (code) |
| `0x003ea9c0` | `DynColasign_OnMessage` | `dyn_colasign` message | `0x15` / `0x20`: removes the child | confirmed (code) |
| `0x003eaa30` | `DynColasign_Update` | `dyn_colasign` update | done once the child is gone | confirmed (code) |

### `sub_thrown_dust_puff` {#thrown-dust-puff}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e1ee0` | `SubThrownDustPuff_Init` | `sub_thrown_dust_puff` init | a dust puff with a small random drift; colour, interval and size from six-step tables (`0x00514638`, `0x00514658`, `0x00514678`), scale 3 | confirmed (code) |
| `0x003e20d8` | `SubThrownDustPuff_Update` | `sub_thrown_dust_puff` update | next table step; ends after six | confirmed (code) |

### Glass pieces {#glass-pieces}

The pieces of a broken pane ([Glass panes](objects.md#glass)): shards fall under gravity (−9.8) and bounce off what
they touch (task flag `0x20000000`, a contact), losing speed. Data: `+0x00` and `+0x10` saved velocity, `+0x20` age,
`+0x24` contacts.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e5090` | `SubStainedGlass_Init` | `sub_stained_glass` init | pops the pane size and colour; a hidden emitter | confirmed (code) |
| `0x003e5168` | `SubStainedGlass_Update` | `sub_stained_glass` update | the break sound (`Sound_PlayMaterialPairAt`), then rings of `sub_coloured_shards` over the pane, the count from its area (10 for a small pane, size 0.06) | confirmed (code) |
| `0x003e30e0` | `SubColouredGlass_Init` | `sub_coloured_glass` init | a coloured shard, random rectangle, size 0.95-1.05 × the popped one | confirmed (code) |
| `0x003e33d0` | `SubColouredGlass_Update` | `sub_coloured_glass` update | bounces off contacts (1 in 2 keeps its speed); ends after 3 contacts or 120 ticks | confirmed (code) |
| `0x003e36c0` | `SubColouredShards_Init` | `sub_coloured_shards` init | as `sub_coloured_glass` | confirmed (code) |
| `0x003e39f8` | `SubColouredShards_Update` | `sub_coloured_shards` update | bounces; on a contact may break into `sub_coloured_glass`; ends after 3 contacts or 120 ticks | confirmed (code) |
| `0x003e3d40` | `Miniglass_Init` | `miniglass` init | a small shard, random spin, falling, pale colour | confirmed (code) |
| `0x003e4030` | `Miniglass_OnMessage` | `miniglass` message | does nothing | confirmed (code) |
| `0x003e4058` | `Miniglass_Update` | `miniglass` update | tumbles and bounces, settles after contacts, fades from 60 ticks, ends at 120 | confirmed (code) |

### HUD widgets {#hud-widgets}

Screen sprites made as particle tasks (flags `0x400304`): `hud_text_widget`, `hud_widget` and `hud_widget_angled`.
Each pops its colour, size and sprite (page and rectangle) and starts hidden. Data: `+0x00` size, `+0x04` colour,
`+0x08` state (0 fade in, 3 apply a move, 10 hide, 20 show at half alpha), `+0x0c` interval (the angled one: its
tilt), `+0x10` a pending position. The messages are shared:

| Message | Meaning |
| --- | --- |
| 10 | show (non-zero) or hide |
| `0x11` | sprite rectangle (`0x0039bb18`) |
| `0x15` | remove |
| `0x19` | fade: 0 half alpha, 1 full colour (2 faster for `hud_widget`; 12 sets flag `0x40`) |
| `0x29` / `0x2a` | show / hide |
| `0x2f` | move to a position (and interval) |
| `0x31` | interval; mode 4 hides, 5 resets |
| `0x33` | rectangle and size scale (`hud_widget`, `hud_widget_angled`) |

Confirmed (code) at the handlers below.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e5eb0` | `RadarDot_SetAlpha` | helper (`hud_radar_dot` message) | sets both colours from the popped value and reschedules the dot | confirmed (code) |
| `0x003e61d8` | `HudTextWidget_Init` | `hud_text_widget` init | colour, size, sprite on page 3; hidden | confirmed (code) |
| `0x003e62c8` | `HudTextWidget_Fade` | helper (`0x19`) | half alpha or full colour | confirmed (code) |
| `0x003e6358` | `HudTextWidget_SetMode` | helper (`0x31`) | interval; mode 4 hides, 5 resets | confirmed (code) |
| `0x003e6400` | `HudTextWidget_Move` | helper (`0x2f`) | new interval and position | confirmed (code) |
| `0x003e6498` | `HudTextWidget_OnMessage` | `hud_text_widget` message | the shared messages | confirmed (code) |
| `0x003e65b0` | `HudTextWidget_Update` | `hud_text_widget` update | fade in, apply a move, hide in state 10 | confirmed (code) |
| `0x003e66c8` | `HudWidget_Init` | `hud_widget` init | sprite page and rectangle, colour, size; hidden | confirmed (code) |
| `0x003e67e8` | `HudWidget_Fade` | helper (`0x19`) | half, full or fast; 12 sets flag `0x40` | confirmed (code) |
| `0x003e68d0` | `HudWidget_SetMode` | helper (`0x31`) | interval byte; mode 4 hides, 5 resets | confirmed (code) |
| `0x003e69a0` | `HudWidget_Move` | helper (`0x2f`) | new position and interval | confirmed (code) |
| `0x003e6a60` | `HudWidget_SetRectScale` | helper (`0x33`) | rectangle and size scale | confirmed (code) |
| `0x003e6b10` | `HudWidget_OnMessage` | `hud_widget` message | the shared messages | confirmed (code) |
| `0x003e6c40` | `HudWidget_Update` | `hud_widget` update | fade in, apply a move, hide in state 10 | confirmed (code) |
| `0x003e6db8` | `HudWidgetAngled_ApplyAngle` | helper | rotation about z from its tilt | confirmed (code) |
| `0x003e6e90` | `HudWidgetAngled_Init` | `hud_widget_angled` init | as `hud_widget`, with a random tilt of ±0.15 rad | confirmed (code) |
| `0x003e6fd8` | `HudWidgetAngled_Fade` | helper (`0x19`) | half alpha or full | confirmed (code) |
| `0x003e7060` | `HudWidgetAngled_SetMode` | helper (`0x31`) | mode 4 hides, 5 resets | confirmed (code) |
| `0x003e7100` | `HudWidgetAngled_Move` | helper (`0x2f`) | new position | confirmed (code) |
| `0x003e71a0` | `HudWidgetAngled_SetRectScale` | helper (`0x33`) | rectangle and size scale | confirmed (code) |
| `0x003e7248` | `HudWidgetAngled_OnMessage` | `hud_widget_angled` message | the shared messages | confirmed (code) |
| `0x003e7378` | `HudWidgetAngled_Update` | `hud_widget_angled` update | applies its tilt; fade in, move, hide; state 20 shows it at half alpha (update every 10) | confirmed (code) |

### `hat_object` {#hat-object}

A hat as a world object (type 64, flags `0x08`), worn or loose. Data: `+0x00` its rest offset, `+0x20` a saved
pose, `+0x30` state (0 loose, 2 dropped, 3 taken, 4 falling, −5 to remove), `+0x34` update interval, `+0x38` a
counter, `+0x3c` "check for duplicates" (set for model `0xbbbef927`, the Warriors' hat; inferred).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e74a8` | `HatObject_Init` | `hat_object` init | model, update every 240, flags `0x208001` | confirmed (code) |
| `0x003e7f20` | `HatObject_OnMessage` | `hat_object` message | 0 pick up, 8 detach, 10 hittable, `0x19` flag `0x8000`, `0x1b` hold, `0x1c` drop, `0x32` wear | confirmed (code) |
| `0x003e75d0` | `HatObject_OnPickUp` | helper (message 0) | unless hidden: state 3, hidden, and event `0x14` to the human taking it | confirmed (code) |
| `0x003e76a8` | `HatObject_AttachToHand` | helper (`0x1b`) | attached to the popped bone of the human | confirmed (code) |
| `0x003e77c0` | `HatObject_Wear` | helper (`0x32`) | attached to the head, placed (`Human_PlaceHat`) and scaled to the human; message 3 to him | confirmed (code) |
| `0x003e7958` | `HatObject_Drop` | helper (`0x1c`) | detached at its world pose and falls; three models get flag `0x80` | confirmed (code) |
| `0x003e7b38` | `HatObject_SetFlag8000` | helper (`0x19`) | sets or clears flag `0x8000` | confirmed (code) |
| `0x003e7b88` | `HatObject_Update` | `hat_object` update | once, for a Warriors' hat: every other loose `dyn_warr_cb` object is detached and removed, or this one goes when a player holds one; lands after a fall; ends when its wearer is down or gone | confirmed (code); `dyn_warr_cb` being the Warriors' hat inferred |

### Small lights and strobes {#small-lights-strobes}

Emitters (`part_*`) that switch a light child on with `0x12` and off with `0x13`, and the lights themselves.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e8020` | `SmallLight_Init` | `small_light` init | a light of the popped colour, radius 5, update every 60 | confirmed (code) |
| `0x003e80d0` | `SmallLight_OnMessage` | `small_light` message | `0x12` white; `0x13` removed; `0x19` colour | confirmed (code) |
| `0x003e81a8` | `SmallLight_Update` | `small_light` update | done once removed | confirmed (code) |
| `0x003e81e0` | `PartSmallLight_Init` | `part_small_light` init | an emitter, flags `0x404`, no light yet | confirmed (code) |
| `0x003e8290` | `PartSmallLight_OnMessage` | `part_small_light` message | `0x12` spawns a `small_light`; `0x13` removes it; `0x19` stores a colour | confirmed (code) |
| `0x003e8e10` | `PartSmallerLight_Init` | `part_smaller_light` init | an emitter, flags `0x404`, update every 60 | confirmed (code) |
| `0x003e8eb0` | `PartSmallerLight_OnMessage` | `part_smaller_light` message | `0x12` spawns a `small_light`; `0x13` removes it | confirmed (code) |
| `0x003e8408` | `Strober_Init` | `strober` init | a red light, radius 10, update every 10, on | confirmed (code) |
| `0x003e84b0` | `Strober_OnMessage` | `strober` message | `0x12` on, `0x13` off | confirmed (code) |
| `0x003e8518` | `Strober_Update` | `strober` update | red and dark in a four-step cycle; done when off | confirmed (code) |
| `0x003e85b8` | `PartStrobeRed_Init` | `part_strobe_red` init | adds a disabled script sound emitter | confirmed (code) |
| `0x003e8678` | `PartStrobeRed_OnMessage` | `part_strobe_red` message | `0x12` spawns a `strober` and enables the sound; `0x13` stops both | confirmed (code) |
| `0x003eaa60` | `MultiStrobe_Init` | `multi_strobe` init | a red light, radius 15, update every 10, on | confirmed (code) |
| `0x003eab08` | `MultiStrobe_OnMessage` | `multi_strobe` message | `0x12` on, `0x13` off | confirmed (code) |
| `0x003eab70` | `MultiStrobe_Update` | `multi_strobe` update | red and dark in a cycle; done when off | confirmed (code) |
| `0x003eac30` | `PartMultiStrobe_Init` | `part_multi_strobe` init | a hidden emitter attached to its parent | confirmed (code) |
| `0x003eacc8` | `PartMultiStrobe_OnMessage` | `part_multi_strobe` message | `0x12` spawns a `multi_strobe`; `0x13` removes it | confirmed (code) |
| `0x003eada0` | `PartMultiStrobe_Update` | `part_multi_strobe` update | idles (update every 60) | confirmed (code) |
| `0x003e8788` | `SubwayLight_Init` | `subway_light` init | a light, radius 7.5, colour `0xe0e0ffe4`, attached to its parent | confirmed (code) |
| `0x003e8820` | `SubwayLight_OnMessage` | `subway_light` message | `0x19`: colour | confirmed (code) |
| `0x003e8890` | `SubwayLensflare_Init` | `subway_lensflare` init | its own batch; a flare sprite turned to its heading; spawns a `subway_light` | confirmed (code) |
| `0x003e8a48` | `SubwayLensflare_Update` | `subway_lensflare` update | blinks between two colours; after 90 updates removes the light and the batch | confirmed (code) |
| `0x003e8bb8` | `PartSubwayLight_Init` | `part_subway_light` init | an emitter, flags `0x404`, update every 180 | confirmed (code) |
| `0x003e8ce0` | `PartSubwayLight_OnMessage` | `part_subway_light` message | `0x12` on, `0x13` off | confirmed (code) |
| `0x003e8d48` | `PartSubwayLight_Update` | `part_subway_light` update | while on, spawns a `subway_lensflare` | confirmed (code) |

### `dyn_icon` and `sub_glint` {#glint-icon}

`dyn_icon` is the icon over a human (the dealer's, [AI: the dealer's icon](ai.md#dealer-icon)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e8fa0` | `DynIcon_Init` | `dyn_icon` init | model, attached to the human, spin and height ([AI](ai.md#dealer-icon)) | confirmed (code) |
| `0x003e92e0` | `DynIcon_Update` | `dyn_icon` update | the colour fade and the hidden state ([AI](ai.md#dealer-icon)) | confirmed (code) |
| `0x003e9470` | `DynIcon_OnMessage` | `dyn_icon` message | 8, 10, `0x15`, `0x20`, `0x34` ([AI](ai.md#dealer-icon)) | confirmed (code) |
| `0x003e93d0` | `DynIcon_SetColourFade` | helper (`0x34`) | a target colour and a step count; a per-update colour step (none: the colour at once) | confirmed (code) |
| `0x003e9560` | `SubGlint_Init` | `sub_glint` init | a glint sprite (rectangle `0x40029`), size popped × 0.15, attached when given a parent | confirmed (code) |
| `0x003e96a0` | `SubGlint_Update` | `sub_glint` update | blinks on and off every 20-30 ticks | confirmed (code) |
| `0x003e9780` | `SubGlint_OnMessage` | `sub_glint` message | 10 show or hide; `0x13` detached and removed | confirmed (code) |

### `sub_triglint` {#triglint}

Three glints at fixed offsets (`0x006f3160`, sizes at `0x005146c0`), shown only while on screen.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003eade8` | `SubTriglint_SpawnGlints` | helper | spawns the missing glints of its three | confirmed (code) |
| `0x003eaf38` | `SubTriglint_RemoveGlints` | helper | removes its three glints | confirmed (code) |
| `0x003eaf98` | `SubTriglint_Init` | `sub_triglint` init | hidden, update every 60, spawns the glints | confirmed (code) |
| `0x003eb060` | `SubTriglint_OnMessage` | `sub_triglint` message | `0x15`: removes the glints and itself | confirmed (code) |
| `0x003eb0d0` | `SubTriglint_Update` | `sub_triglint` update | glints removed off screen and spawned again on screen | confirmed (code) |

### Objective marker pieces {#objective-pieces}

The marker itself is on [Objective markers](objects.md#objective-markers); these rows add the handlers it does not
list.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003e9e60` | `ObjectiveColumn_SetColour` | helper (`sub_objective_column`) | the column's colour (`+0xcc`) and its faded copy (`+0xc8`) | confirmed (code) |
| `0x003ea0b0` | `ObjectiveGlow_Init` | `sub_objective_glow` init | pops its batch, colour, size 0.75, update every 2 | confirmed (code) |
| `0x003ea198` | `ObjectiveGlow_OnMessage` | `sub_objective_glow` message | 10 show or hide; `0x15` and `0x20` destroy the batch and remove it | confirmed (code) |
| `0x003ea260` | `ObjectiveGlow_UpdatePull` | `sub_objective_glow` update | pulls the glow toward the player camera | confirmed (code) |

### `dyn_breakable_light` {#breakable-light}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ea350` | `DynBreakableLight_Init` | `dyn_breakable_light` init | model, attached; a `sub_blight` light child (`Spawn_SubBlight`) | confirmed (code) |
| `0x003ea488` | `DynBreakableLight_OnMessage` | `dyn_breakable_light` message | `0x20`: removes its two children | confirmed (code) |
| `0x003ea530` | `DynBreakableLight_Update` | `dyn_breakable_light` update | when broken (state 1): children removed, broken model `0xc727685b`, body removed, a spark effect, dust, rubble and the material sound; then idles | confirmed (code) |

### `dyn_disco_a` {#disco-a}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003eb160` | `DynDiscoA_Init` | `dyn_disco_a` init | the disco-ball model `0x80da26fd`, flags `0x800001`, its spin state (data `+0x00` 10, `+0x04` 10, `+0x14` 2) | confirmed (code) |

### Disco lights (`dyn_disco_a`, `dyn_disco_b`) {#dyn-disco}

A disco light turns in random steps of 45° about its up axis and blinks. Data: `+0x00` / `+0x04` how many updates
each of the two models shows, `+0x08` every how many updates it turns, `+0x0c` on, `+0x10` the update count, `+0x14`
the base interval (a random 0-2 ticks is added), `+0x18` which model shows. Off, its colour word `+0xcc` is
`0xffffff00` (alpha 0); on, `0xffffffff`. `dyn_disco_a` swaps between two models (hashes `0x80da26fd` and
`0x19d37747`); `dyn_disco_b` (init values 15, 10, 5, 2) only turns.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003eb240` | `DynDiscoA_OnMessage` | `dyn_disco_a` message | 0x12 on, 0x13 off, 0x20 clears flag 0x800000. | confirmed (code) |
| `0x003eb2e8` | `DynDiscoA_Update` | `dyn_disco_a` update | Random interval, colour on/off, every n-th update a random 45 deg turn, swaps two models on a timer. | confirmed (code) |
| `0x003eb4d0` | `DynDiscoB_Init` | `dyn_disco_b` init | Pops parent and pose, counts 15/10/5, interval 2, starts on. | confirmed (code) |
| `0x003eb5b8` | `DynDiscoB_OnMessage` | `dyn_disco_b` message | 0x12 on, 0x13 off. | confirmed (code) |
| `0x003eb620` | `DynDiscoB_Update` | `dyn_disco_b` update | As dyn_disco_a without the model swap; marks its pose changed. | confirmed (code) |

### Neon signs: `dyn_motelneon` {#dyn-motelneon}

One class drives several neon signs, told apart by their `CfgObj` name (`0x003a5998`): `dyn_motelneon_a`,
`dyn_neon_cross_a`, `dyn_tacks_e` and `dyn_neon_lotus`. Its init starts a buzz emitter (hash `0x7a887f`) and spawns a
`sub_neon_light2` light (range 9) placed by sign. Its update walks a flicker cycle from eight timing bytes in data
`+0x00` / `+0x04` (on and off lengths, a random spread, how many flickers), and on each change sets the sign's model
and the light's colour (message `0x19`) by name; coming on plays hash `0xf164dfe0` once. `dyn_neon_lotus` hides
(flag 4) instead of swapping models. The roles of the single timing bytes are inferred.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003eb7e8` | `DynMotelNeon_Init` | `dyn_motelneon` init | Timing bytes, buzz sound emitter, a sub_neon_light2 light (range 9) placed by sign name. | confirmed (code) |
| `0x003ebae8` | `DynMotelNeon_OnMessage` | `dyn_motelneon` message | Stops the buzz emitter and removes its light. | confirmed (code) |
| `0x003ebb70` | `DynMotelNeon_Update` | `dyn_motelneon` update | Flicker phases from the timing bytes; model and light colour per sign name; a zap sound when it comes on. | inferred |

### Paint splats {#paint-splat}

`sub_paint_splat` (a thrown paint can's hit, inferred from the name) waits until it touches something (flag
`0x20000000`), then spawns one `paint_splat` decal at the contact, offset 0.04 m along its normal. A `paint_splat`
decal takes sprite `0x24` or `0x25`, a random spin, size 0.55 × its size argument and the given colour; it updates
every 60 ticks and fades out after 90 updates (400 when the game mode record's `+0x04` is 3), or at once when no
camera is within 35 m (50 m in mode 3).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ebed8` | `SubPaintSplat_Init` | `sub_paint_splat` init | Colour, size, pose; update every tick. | confirmed (code) |
| `0x003ebff0` | `SubPaintSplat_Update` | `sub_paint_splat` update | Once it touches a surface (flag 0x20000000) spawns one paint_splat decal there; 15-update life. | confirmed (code) |
| `0x003ec158` | `PaintSplat_Init` | `paint_splat` init | Decal sprite 0x24/0x25, random spin, size 0.55 x arg, colour; life 90 (400 in game mode 3). | confirmed (code) |
| `0x003ec3a0` | `PaintSplat_Update` | `paint_splat` update | Every 60 ticks; fades when its life is over or no camera is near. | confirmed (code) |

### Plaster drops {#plaster-drop}

A ceiling's plaster fall: two `sub_rubble` emitters (sizes 0.03 and 0.02) and 20 `sub_debris` chips of colour
`0xffe2a7ff`, sprite 24-28, thrown at random. `sub_plaster_drop` makes it at once and rumbles both players' pads
(`0x003a4608` 0.95, `0x003a4668` 0.55; inferred: rumble strengths); `part_plaster_drop` and `part_plaster_drop_ns`
wait for message `0x12`, and only the `_ns` type rumbles.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ec500` | `SubPlasterDrop_Init` | `sub_plaster_drop` init | Pad rumble to both players, two sub_rubble and 20 sub_debris plaster chips. | confirmed (code) |
| `0x003ecf78` | `PartPlasterDrop_Init` | `part_plaster_drop` / `part_plaster_drop_ns` init | Pose, update every 40 ticks, hidden. | confirmed (code) |
| `0x003ed008` | `PartPlasterDrop_OnMessage` | `part_plaster_drop` / `part_plaster_drop_ns` message | Makes the drop (two sub_rubble, 20 sub_debris; rumble for _ns). | confirmed (code) |

### `sub_flashing_light` {#flashing-light}

A light (type flags `0x400`) that blinks between a colour and a dimmed copy of it (each channel × a percent). Data:
`+0x00` on, `+0x04` which colour shows, `+0x08` / `+0x0c` the two lengths in ms, `+0x10` / `+0x14` the colours,
`+0x18` the last game time, `+0x1c` the time left. Its update runs the timer on game time, not ticks. Unless game
flag word 3 has `0x10`, it also switches itself and every `sub_flashing_light` within 5 m off when the player camera
is out of its range + 10 m, and back on when inside (`FlashingLight_RelayNearby`, messages `0x13` / `0x12`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ec8f8` | `FlashingLight_RelayNearby` | helper (called by `sub_flashing_light` update) | Sends 0x12 or 0x13 to every sub_flashing_light within 5 m of a point. | confirmed (code) |
| `0x003eca88` | `SubFlashingLight_Init` | `sub_flashing_light` init | Colour, dimmed colour (percent), on and off times (ms), range, start state. | confirmed (code) |
| `0x003eccd8` | `SubFlashingLight_OnMessage` | `sub_flashing_light` message | 0x12 on (every 3 ticks), 0x13 off and hidden (every 30). | confirmed (code) |
| `0x003ecdc8` | `SubFlashingLight_Update` | `sub_flashing_light` update | Blinks between its two colours by game time; turns lights within 5 m on or off with the player camera's range. | confirmed (code) |

### `dyn_skullglow` {#dyn-skullglow}

A glowing skull prop with a `sub_neon_light2` light (range 2) 0.5 m below it. Every 60 ticks it alternates the light
between red (`0xaa2b2bff`) and a flicker with a random alpha of 50-100. Message `0x20` removes the light.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ed3d0` | `DynSkullGlow_Init` | `dyn_skullglow` init | A sub_neon_light2 light of range 2 below it; update every 60 ticks. | confirmed (code) |
| `0x003ed5a0` | `DynSkullGlow_OnMessage` | `dyn_skullglow` message | Removes its light. | confirmed (code) |
| `0x003ed610` | `DynSkullGlow_Update` | `dyn_skullglow` update | Alternates red light and a random-alpha flicker. | confirmed (code) |

### Strobes (`part_strobe`, `strobe`) {#strobe}

`part_strobe` is a switch: message `0x12` spawns a `strobe` light at its pose (once), `0x13` turns that light off. The
`strobe` light (range 15) toggles its hidden flag (4) every 4 ticks while on and ends when turned off.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ed718` | `PartStrobe_Init` | `part_strobe` init | Pose, hidden, no strobe yet. | confirmed (code) |
| `0x003ed7b8` | `PartStrobe_OnMessage` | `part_strobe` message | 0x12 spawns a strobe light, 0x13 turns it off. | confirmed (code) |
| `0x003ed890` | `PartStrobe_Update` | `part_strobe` update | Only re-arms every 60 ticks. | confirmed (code) |
| `0x003ed8b0` | `Strobe_Init` | `strobe` init | Strobe (light) init: range 15, on, update every 2 ticks. | confirmed (code) |
| `0x003ed958` | `Strobe_OnMessage` | `strobe` message | 0x12 on, 0x13 off. | confirmed (code) |
| `0x003ed9c0` | `Strobe_Update` | `strobe` update | Toggles its hidden flag every 4 ticks; done when off. | confirmed (code) |

### Padlocks: `dyn_lock_a` {#dyn-lock}

A breakable padlock. Data: `+0x00` state (−5 broken), `+0x04` hits taken, `+0x08` hits it takes (`CfgObj` value 0,
`+0x58`; message `0x19` sets it). Each hit (message 1) plays hash `0xf4bad950` and throws 1-3 `sub_anim_spark`
sparks in gold colours; the model follows the damage left (`0x003a4c48`). The last hit throws 4-6 sparks, spawns a
falling `dyn_lock_b` (sent message `0x30` with a push and a random spin), hides the lock, plays hash `0xe108f888`
and marks it broken, so its update ends it. `0x003ee248` fills a constant transform at `0x006f3160` that nothing
reads; it runs from the static-constructor table (`0x003ee318`, inferred).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003eda58` | `DynLockA_Init` | `dyn_lock_a` init | Model, hit count 0, hit points from CfgObj. | confirmed (code) |
| `0x003edb28` | `DynLockA_OnMessage` | `dyn_lock_a` message | 1 hit (sparks, sound, model by damage; last hit drops a dyn_lock_b), 0x15 remove, 0x19 hit points. | confirmed (code) |
| `0x003ee170` | `DynLockA_Update` | `dyn_lock_a` update | Done once broken; clears velocity after a contact. | confirmed (code) |
| `0x003ee248` | `DynLock_InitStaticTransform` | helper (static initialiser body) | Static initialiser body: fills a constant transform at 0x006f3160 (written only). | inferred |
| `0x003ee318` | `DynLock_StaticInit` | static initialiser | Static initialiser (in the constructor table) calling 0x003ee248. | inferred |

### `dyn_cbradio` {#dyn-cbradio}

A CB radio that talks: every 10-15 s it plays the next of four lines, `vags/speeches/l9/l9_t7_009` to `_012`, and
message `0x12` plays a random one. Hits (message 1) count down `CfgObj` value 1 (`+0x5a`) or value 0 by the hit's
kind; at 0 it breaks: sparks, dust, rubble, its broken model, a `dyn_cbradio_b` child, the type's material sound
(unless game flag word 3 has 4), and its line stops. Other hits play hash `0x690c2ae3`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ee338` | `DynCbRadio_Init` | `dyn_cbradio` init | Hit points (two CfgObj values), chatter every 10-15 s, the four l9_t7 line hashes. | confirmed (code) |
| `0x003ee4a8` | `DynCbRadio_OnMessage` | `dyn_cbradio` message | 1 hit (breaks into dyn_cbradio_b with sparks and dust), 4, 8 detach, 0x12 a random line. | confirmed (code) |
| `0x003ee838` | `DynCbRadio_Update` | `dyn_cbradio` update | Plays the four lines in turn every 10-15 s; done once broken. | confirmed (code) |

### `dyn_ctrl_box` {#dyn-ctrl-box}

A control box with 15 hit points (message `0x19` sets them). A hit takes 5 + 5 × the hit's strength and sparks; at 0
it throws rubble, sparks and dust, spawns `dyn_ctrl_box_a`, plays its material sound and ends.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ee9d8` | `DynCtrlBox_Init` | `dyn_ctrl_box` init | 15 hit points, model. | confirmed (code) |
| `0x003eeaa8` | `DynCtrlBox_OnMessage` | `dyn_ctrl_box` message | 1 hit (-5 - 5 x strength, sparks; at 0 breaks into dyn_ctrl_box_a), 8 detach, 0x19 hit points. | confirmed (code) |
| `0x003eee68` | `DynCtrlBox_Update` | `dyn_ctrl_box` update | Done once broken. | confirmed (code) |

### `dyn_blocker` {#dyn-blocker}

An invisible switch on the AI's walk mesh: message `0x13` sets flag 8 on the path polygon under it, `0x12`
clears it (what flag 8 means to the route planner is not traced here). A hit is passed on to its own
message slot; it has no model and its update does nothing.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ef980` | `DynBlocker_Init` | `dyn_blocker` init | Pose, no model, off. | confirmed (code) |
| `0x003efa20` | `DynBlocker_OnMessage` | `dyn_blocker` message | 0x12 frees the path polygon under it, 0x13 blocks it (flag 8), 1 passes the hit on. | confirmed (code) |
| `0x003efb60` | `DynBlocker_Update` | `dyn_blocker` update | Nothing. | confirmed (code) |

### The shore: waves and washes {#shore}

The sea along one beach, all at fixed world positions near (−556, −244 to −290): `dyn_o_shore` bobs the water line
by 0.24 m steps over a six-step tide, spawns a `dyn_o_wave` every 23-29 updates and a `dyn_o_whitecap_a` every 3,
and prepares one of four wave sounds (`0x005146e0`) at the camera 8 updates before each wave, played when it comes.
A `dyn_wave` spawns `dyn_o_animwave_a` and the two washes and then runs eight keyframes of position, interval and
alpha before it removes them; `dyn_animwave` steps through a chain of eight models; `dyn_wash_a` / `_b` run their
own keyframes; `dyn_outwave` is a far crest of one of four models at a random spot that lives 4-11 updates. Every
one is pinned so the object manager does not store it. Which beach this is is not traced; the keyframes' look is
inferred from the values.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003efb90` | `DynOShore_Init` | `dyn_o_shore` init | Fixed shore position, a dyn_o_wave child, wave timers. | confirmed (code) |
| `0x003efd00` | `DynOShore_OnMessage` | `dyn_o_shore` message | 0x12 sound on, 0x13 stops both wave sounds. | confirmed (code) |
| `0x003efda8` | `DynOShore_Update` | `dyn_o_shore` update | Tide bob, a new dyn_o_wave every 23-29 updates, whitecaps every 3, wave sounds prepared and played. | confirmed (code) |
| `0x003f01d8` | `DynWave_Init` | `dyn_wave` init | Pinned; children dyn_o_animwave_a and dyn_o_wavewash_a/_b. | confirmed (code) |
| `0x003f0360` | `DynWave_OnMessage` | `dyn_wave` message | None handled. | confirmed (code) |
| `0x003f0388` | `DynWave_Update` | `dyn_wave` update | Eight keyframes of position, timing and alpha; done after the eighth. | inferred |
| `0x003f0758` | `DynAnimWave_Init` | `dyn_animwave` init | Attached to its parent, 0.5 m up, random 1-4 tick interval. | confirmed (code) |
| `0x003f0898` | `DynAnimWave_OnMessage` | `dyn_animwave` message | None handled. | confirmed (code) |
| `0x003f08c0` | `DynAnimWave_Update` | `dyn_animwave` update | Steps through a chain of eight models. | confirmed (code) |
| `0x003f0988` | `DynWashA_Init` | `dyn_wash_a` init | Fixed position, transparent, keyframe 0. | confirmed (code) |
| `0x003f0ae8` | `DynWashA_OnMessage` | `dyn_wash_a` message | None handled. | confirmed (code) |
| `0x003f0b10` | `DynWashA_Update` | `dyn_wash_a` update | Keyframes of a wash up the beach and back, fading. | inferred |
| `0x003f0e80` | `DynWashB_Init` | `dyn_wash_b` init | As dyn_wash_a. | confirmed (code) |
| `0x003f0fe0` | `DynWashB_OnMessage` | `dyn_wash_b` message | None handled. | confirmed (code) |
| `0x003f1008` | `DynWashB_Update` | `dyn_wash_b` update | The second wash's keyframes. | inferred |
| `0x003f1388` | `DynOutWave_Init` | `dyn_outwave` init | One of four crest models at a random spot, life 4-11 updates of 40-80 ticks. | confirmed (code) |
| `0x003f1650` | `DynOutWave_OnMessage` | `dyn_outwave` message | None handled. | confirmed (code) |
| `0x003f1678` | `DynOutWave_Update` | `dyn_outwave` update | Counts its life, fades at the last update. | confirmed (code) |

### `pickup_item` {#pickup-item}

The class of items lying in the world ([Objects](objects.md#object-types)). Data: `+0x10` its glint, `+0x14` state (0
lying, 1 picked up, 2 used), `+0x18` a counter, `+0x1c` the update interval (60 ticks, 15 after a pick-up), `+0x20`
"never glints", set when the model's name hash (CRC-32 of the name) is one of seven, all hobo food: `0x8733e003`
`dyn_hobo_donut_a`, `0x1e3ab1b9` `dyn_hobo_donut_b`, `0x693d812f` `dyn_hobo_donut_c`, `0xe09f6c15` `dyn_hobo_hotdog`,
`0xc57ab00a` `dyn_hobo_mug`, `0x7056d232` `dyn_hobo_salami` and `0xb239f45d` `dyn_hobo_steak` (confirmed (code) at
`0x003f17e0` for the hashes; the names matched by computing the CRC-32 of the game's model names). One more hash,
`0x2fd690d6` `dyn_carstereo`, instead clears flag `0x8000` at init (its meaning not traced). While it lies within 30 m
on screen (`0x003a51f8`) it keeps a `sub_triglint` glint. Messages: 0 picked up (flag `0x100000`, the taker gets message
`0x14`), 8 detach, 10 shown or hidden, `0x19` flag `0x8000`, `0x1b` handed to a human (he gets messages 3 and `0x17`
with its type record), `0x1c` used, `0x20` glint off, `0x30` thrown with a velocity of 4-6. The update ends it two
updates after it is used, or when it falls faster than 500 (out of the world, inferred).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f16e8` | `PickupItem_ShowGlint` | helper (`pickup_item`) | Makes its sub_triglint glint when it has none and is not held. | confirmed (code) |
| `0x003f1760` | `PickupItem_RemoveGlint` | helper (`pickup_item`) | Removes its glint (message 0x15). | confirmed (code) |
| `0x003f17e0` | `PickupItem_Init` | `pickup_item` init | Pose, flags 0x208081, model; seven model hashes mark it as never glinting. | confirmed (code) |
| `0x003f1950` | `PickupItem_Update` | `pickup_item` update | Glint within 30 m on screen; state counters; done when used or falling fast. | inferred |
| `0x003f1ad8` | `PickupItem_OnPickUp` | helper (`pickup_item` message 0) | Held flag, tells the human (0x14), removes the glint. | confirmed (code) |
| `0x003f1bb0` | `PickupItem_OnEquip` | helper (`pickup_item` message 0x1b) | Attached to a human, tells him 3 and 0x17 with its type record. | confirmed (code) |
| `0x003f1ef0` | `PickupItem_OnUse` | helper (`pickup_item` message 0x1c) | When attached, marks it used (state 2). | confirmed (code) |
| `0x003f1f90` | `PickupItem_SetFlag8000` | helper (`pickup_item` message 0x19) | Sets or clears flag 0x8000. | confirmed (code) |
| `0x003f1fe0` | `PickupItem_OnHideGlint` | helper (`pickup_item` message 0x20) | Removes the glint. | confirmed (code) |
| `0x003f2000` | `PickupItem_SetShown` | helper (`pickup_item` message 10) | Shown (glint back) or hidden (flag 4). | confirmed (code) |
| `0x003f2070` | `PickupItem_OnMessage` | `pickup_item` message | Dispatches messages 0, 8, 10, 0x19, 0x1b, 0x1c, 0x20, 0x30 (thrown). | confirmed (code) |

### `powerup_item` and its glow {#powerup-item}

Spray cans and other power-ups ([walked over](player-state.md#walk-over)). Init: flags `0x808081`, a half turn, the
glow, and the object or car it sits on (`+0x58`). The glow's colour is chosen by model hash (`0x2b72ff`,
`0x4c0d16ff`, `0x23c00ff`, `0x757575ff`); one model (`0x2fd690d6`) gets a `sub_glint` instead. The glow is a
`sub_powerup_glow` sprite (`0x4001d`, size 0.2) attached to the item; message `0x34` sets its colour and `0x15` ends
it. The item's messages: 0 first touch, 8, 10 shown or hidden, `0x15` removed, `0x19` flag `0x8000` (glow off) or
cleared (glow back), `0x20` glow off, `0x30` ignored. Its update is `PowerupItem_Update` (`0x003f2870`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f2270` | `SubPowerupGlow_Init` | `sub_powerup_glow` init | Attached sprite 0x4001d, size 0.2. | confirmed (code) |
| `0x003f2340` | `SubPowerupGlow_Update` | `sub_powerup_glow` update | Done once told to go (state 0x32). | confirmed (code) |
| `0x003f2378` | `SubPowerupGlow_OnMessage` | `sub_powerup_glow` message | 0x15 ends it, 0x34 sets its colour. | confirmed (code) |
| `0x003f23f8` | `PowerupItem_MakeGlow` | helper (`powerup_item` init and messages) | Glow colour by model hash, a sub_powerup_glow (or a sub_glint for one model). | confirmed (code) |
| `0x003f2698` | `PowerupItem_RemoveGlow` | helper (`powerup_item`) | Removes its glow and glint. | confirmed (code) |
| `0x003f2700` | `PowerupItem_Init` | `powerup_item` init | Pose, flags 0x808081, model, glow; remembers the parent object or car. | confirmed (code) |
| `0x003f2af0` | `PowerupItem_SetShown` | helper (`powerup_item` message 10) | Shown (glow back) or hidden. | confirmed (code) |
| `0x003f2b60` | `PowerupItem_OnTouch` | helper (`powerup_item` message 0) | First touch clears flag 0x8000 and marks it taken. | confirmed (code) |
| `0x003f2bc8` | `PowerupItem_SetFlag8000` | helper (`powerup_item` message 0x19) | Flag 0x8000 with the glow off, or cleared with the glow back. | confirmed (code) |
| `0x003f2c40` | `PowerupItem_OnMessage` | `powerup_item` message | Dispatches messages 0, 8, 10, 0x15, 0x19, 0x20, 0x30. | confirmed (code) |

### Rain: `part_raindrops`, `sub_splash`, `sub_ripple` {#rain}

One `part_raindrops` per view (its init pops the view index). While on (message `0x12`; `0x13` off, `0x34` sets
size, count and colour) it drops count ÷ views splashes per update at random points 2-20 m ahead of that view's
camera, on a surface found below (`0x003a36e0`, 15 m) that a player can see; under 15 m a `sub_ripple` too. A splash
(sprite `0x10033`) doubles and fades in two updates; a ripple (sprite `0x27`, flat on the surface) grows to 3× and
fades.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f2d70` | `SubRipple_Init` | `sub_ripple` init | Ring sprite 0x27, size 0.09-0.15 x arg, colour, every 30 ticks. | confirmed (code) |
| `0x003f2ea0` | `SubRipple_Update` | `sub_ripple` update | Grows to 3x and fades out, then done. | confirmed (code) |
| `0x003f2f18` | `SubRipple_OnMessage` | `sub_ripple` message | None handled. | confirmed (code) |
| `0x003f2f40` | `SubSplash_Init` | `sub_splash` init | Sprite 0x10033, size 2.5-3.75 x arg. | confirmed (code) |
| `0x003f3038` | `SubSplash_Update` | `sub_splash` update | Doubles and fades out, then done. | confirmed (code) |
| `0x003f30b0` | `SubSplash_OnMessage` | `sub_splash` message | None handled. | confirmed (code) |
| `0x003f30d8` | `PartRaindrops_Init` | `part_raindrops` init | View index, colour, count 2, size 0.75, off. | confirmed (code) |
| `0x003f3178` | `PartRaindrops_Update` | `part_raindrops` update | Splashes (and ripples within 15 m) on surfaces 2-20 m ahead of the view's camera. | confirmed (code) |
| `0x003f3588` | `PartRaindrops_Configure` | helper (`part_raindrops` message 0x34) | Size, count and colour. | confirmed (code) |
| `0x003f35d8` | `PartRaindrops_OnMessage` | `part_raindrops` message | 0x12 on, 0x13 off, 0x34 configure. | confirmed (code) |

### `dyn_rat` {#dyn-rat}

A rat. Each tick: when the player camera comes within 8 m it bolts (a random push of ±5 m/s, a squeak from
`0x00514718`); within 60 m it wanders in short random hops; every 15 updates it is put back on the ground (rays of 5
and 2 m) and every 60 it squeaks (`0x00514720`); its heading follows its velocity. After 60 updates, once no player
can see it (35 / 50 m), it squeaks (`0x00514728`) and is gone. The distances' roles are inferred.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f36a0` | `DynRat_Init` | `dyn_rat` init | Pose, model, update every tick. | confirmed (code) |
| `0x003f3770` | `DynRat_Update` | `dyn_rat` update | Bolts from the camera within 8 m, wanders, stays on the ground, squeaks; leaves once unseen after 60 updates. | inferred |
| `0x003f3f30` | `DynRat_OnMessage` | `dyn_rat` message | None handled. | confirmed (code) |

### Rubble: `rubble` and `sub_rubble` {#rubble}

`sub_rubble` throws 8 `rubble` pieces (4 when game flag word 0 has 2) in random directions once, then lives 11
updates. A `rubble` piece (sprite 12-15 of batch 1) flies with gravity (−0.33 per update); on landing it plays its
material against what it hit, or puffs (`sub_shack_puff`) and bounces a few times; after 120 updates it fades.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f3f58` | `Rubble_Init` | `rubble` init | Sprite 12-15, thrown velocity, size, material for its landing sound. | confirmed (code) |
| `0x003f4188` | `Rubble_Update` | `rubble` update | Gravity, landing sound or a puff and a bounce, fades after 120 updates. | confirmed (code) |
| `0x003f45c0` | `Rubble_OnMessage` | `rubble` message | None handled. | confirmed (code) |
| `0x003f45e8` | `SubRubble_Init` | `sub_rubble` init | Material, size, colour, direction. | confirmed (code) |
| `0x003f46c0` | `SubRubble_OnMessage` | `sub_rubble` message | None handled. | confirmed (code) |
| `0x003f46e8` | `SubRubble_Update` | `sub_rubble` update | Throws 8 rubble pieces (4 with game flag 2) once; done after 11 updates. | confirmed (code) |

### Sliding doors (`dyn_door_sliding`, `sub_sliding_door`) {#sliding-doors}

The roll-up, portcullis, fence, lift, police, subway and spook doors. `dyn_door_sliding` makes, by its `CfgObj` name,
one or two leaf objects (`dyn_dr_rollup`, `dyn_dr_portcullis`, `dyn_dr_fence_r`, `dyn_dr_fence_s`, `dyn_dr_ele`,
`dyn_dr_ele_c`, `dyn_dr_pol`, `dyn_dr_subway`, `dyn_dr_spook_a` / `_b`), each a `sub_sliding_door`. The slide length
is `CfgObj` value 7 (else 8), the axis byte (`+0x2d`) picks ±x or ±y (fences and police doors one more), the speed is
twice value 6, and the open and close sounds are per type. Its command (message `0x22`, also `0x0b` when a human
opens it): 2 or 8 open (leaves get state `0xd`, the collision triangles off), 3 or 7 close (`0xe`, triangles on), 5
triangles off, 6 on, 10 / 11 the lock-pick glint on / off. Message `0x0c` answers "open". The leaf's update moves it
along the axis until the length (or 0), then tells the door `0x13` and rests (`0xc`). Compare the
[swinging doors](objects.md#door-states).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f4950` | `SubSlidingDoor_PlaceLeaf` | helper (`sub_sliding_door` update and message 0x40) | Sliding door leaf: takes its closed pose from the door and its slide axis from the door's axis byte. | confirmed (code) |
| `0x003f4ba0` | `SubSlidingDoor_SetSprite` | helper (`sub_sliding_door` message 0x19) | Sliding door leaf message 0x19: sets its sprite word. | confirmed (code) |
| `0x003f4bd0` | `SubSlidingDoor_NotifyDone` | helper (`sub_sliding_door` update) | Sliding door leaf: tells its door 0x13 when a slide ends. | confirmed (code) |
| `0x003f4c20` | `SlidingDoor_SendLeafPose` | helper (`dyn_door_sliding` leaf set-up) | Sends a leaf message 0x40 with its pose. | confirmed (code) |
| `0x003f4c80` | `SlidingDoor_SpawnLeaves` | helper (`dyn_door_sliding` init) | Sliding door: by type name spawns one or two dyn_dr_* leaves, sets slide length, axis and sounds. | confirmed (code) |
| `0x003f5318` | `SlidingDoor_OnStart` | helper (`dyn_door_sliding` message 0x10) | Sliding door message 0x10: pins its leaves and starts them. | confirmed (code) |
| `0x003f53b0` | `SlidingDoor_SendLeafState` | helper (`dyn_door_sliding` command) | Sends a leaf message 0x36 with a state (0xd opening, 0xe closing). | confirmed (code) |
| `0x003f5418` | `SlidingDoor_SetPickable` | helper (`dyn_door_sliding` command) | Sliding door lock-pick glint (sub_triglint) on or off. | confirmed (code) |
| `0x003f5608` | `SlidingDoor_StateCommand` | helper (`dyn_door_sliding` messages 0x22, 0x0b) | Sliding door message 0x22 / 0x0b: open, close, collision on/off, glint. | confirmed (code) |
| `0x003f5870` | `SlidingDoor_OnStop` | helper (`dyn_door_sliding` message 0x13) | Sliding door message 0x13: unpins its leaves once stopped. | confirmed (code) |
| `0x003f58f8` | `SubSlidingDoor_Init` | `sub_sliding_door` init | Its door, pose, collision body, rest state 0xc. | confirmed (code) |
| `0x003f5a68` | `SubSlidingDoor_Update` | `sub_sliding_door` update | Slides open (0xd) up to the length or closed (0xe) to 0, then tells the door. | confirmed (code) |
| `0x003f5c38` | `SubSlidingDoor_OnMessage` | `sub_sliding_door` message | 8, 0x15, 0x19 sprite, 0x36 state, 0x40 pose. | confirmed (code) |
| `0x003f5d10` | `DynDoorSliding_Init` | `dyn_door_sliding` init | Triangles two-sided and typed, slide speed, leaves, starts closed (command 7). | confirmed (code) |
| `0x003f5ec0` | `DynDoorSliding_Update` | `dyn_door_sliding` update | Nothing. | confirmed (code) |
| `0x003f5ef0` | `DynDoorSliding_OnMessage` | `dyn_door_sliding` message | 8, 0x0b opened by a human, 0x0c is open, 0x10, 0x13, 0x15, 0x22 command, 0x3e. | confirmed (code) |

### `dyn_door_barricade` {#dyn-door-barricade}

A barricade that only opens and closes: it has no hitpoints and no model work of its own. Its initialiser pops, last
pushed first, a link number and two collision triangles, makes both triangles two-sided and hides itself (flags
`+0x54` = 4, update every 240 ticks). Data: object `+0xd8` / `+0xdc` the triangles, `+0xe0` the link number.
Message **`0x22`** with argument 2 turns the triangles off (`0x003a46c8`) and opens the hole in the path graph for
the link number (`0x003a4b00`, [A door's hole](objects.md#barriers)); argument 3 turns them back on (`0x003a4710`)
and closes it (`0x003a4ae0`). Other arguments and messages do nothing.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f6018` | `DynDoorBarricade_Init` | `dyn_door_barricade` init | pops the link number and two triangles, makes them two-sided, hidden, update every 240 ticks | confirmed (code) |
| `0x003f60f8` | `DynDoorBarricade_OnMessage` | `dyn_door_barricade` message | `0x22`: 2 opens (triangles off, hole open), 3 closes | confirmed (code) |
| `0x003f61a8` | `DynDoorBarricade_Update` | `dyn_door_barricade` update | does nothing and never reports done | confirmed (code) |

### Steam, hood smoke and the ominous smoke {#steam-family}

The bigger vents and smokes built on `part_steam` ([Steam vents](particles.md#steam)). `part_steam_huge`,
`part_steam_large` and `part_ominous_smoke` share the vent's message helpers: **10** switches the vent on (data
`+0x2c` = 1, its spawn interval taken from data `+0x20`) or off (argument 0), **`0x12`** calls a helper that
rotates (0, 0, −1) by the vent's rotation and throws the result away (no effect), and **`0x27`** is `CfgSteam`
(`Steam_OnCfgSteam`). Their updates spawn `sub_smoke` puffs while on, a camera is near and the particle pool has room
(`0x003a5a50`); the interval is data `+0x29` then, 60 ticks (180 for the ominous smoke) when no camera is near. The
two car smokes are emitters that run a fixed number of updates.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f66f8` | `SubSmoke_OnMessage` | `sub_smoke` message | an empty stub | confirmed (code) |
| `0x003f68e0` | `Steam_SetOn` | helper (message 10 of the vents) | argument 0: off; else on, the spawn interval from data `+0x20` | confirmed (code) |
| `0x003f6930` | `Steam_ComputeDown` | helper (message `0x12` of the vents) | computes the vent's down vector and discards it | confirmed (code) |
| `0x003f6f08` | `SteamHuge_OnMessage` | `part_steam_huge` message | 10, `0x12`, `0x27` as above | confirmed (code) |
| `0x003f6fb8` | `SteamHuge_Update` | `part_steam_huge` update | spawns `sub_smoke` puffs while on and near a camera; else update every 60 | confirmed (code) |
| `0x003f72c8` | `SteamLarge_OnMessage` | `part_steam_large` message | 10, `0x12`, `0x27` as above | confirmed (code) |
| `0x003f7378` | `SteamLarge_Update` | `part_steam_large` update | as the huge vent's, with its own puff sizes | confirmed (code) |
| `0x003f74f8` | `SubHoodSmoke_Init` | `sub_hood_smoke` init | flags `0x404`, update every 30 ticks, counter 0 | confirmed (code) |
| `0x003f7580` | `SubHoodSmoke_Update` | `sub_hood_smoke` update | a `sub_smoke` puff on each of 30 updates (a random sprite of 16), then done; done at once with no camera near | confirmed (code); a car's hood inferred from the name |
| `0x003f7780` | `SubCarSteam_Init` | `sub_car_steam` init | flags `0x404`, update every 2, the first burst after 20-35 updates | confirmed (code) |
| `0x003f7838` | `SubCarSteam_Update` | `sub_car_steam` update | for 900 updates alternates bursts of `sub_smoke` (15-45 updates) and pauses (20-25), then done | confirmed (code) |
| `0x003f7c78` | `OminousSmoke_Init` | `part_ominous_smoke` init | update every 10, flags `0x405`, puff colours (`0xaf0d02`, `0x0a0a0aaf`), sizes, drag and lifetime 200 | confirmed (code) |
| `0x003f7e08` | `OminousSmoke_OnMessage` | `part_ominous_smoke` message | 10, `0x12`, `0x27` as the vents | confirmed (code) |
| `0x003f7eb8` | `OminousSmoke_Update` | `part_ominous_smoke` update | spawns `sub_smoke` while on; every 180 ticks when no camera is near | confirmed (code) |

### Door helpers {#door-helpers}

Two helpers of `dyn_door_swinging` ([Leaves](objects.md#leaves), [Lock picks](objects.md#lock-pick)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003f8048` | `DoorSwing_CountLockPick` | helper (called by `LockPick_End` `0x0022d908`) | for a `dyn_door_swinging`, counts finished picks (data `+0x29`) and returns true on the third, when `LockPick_End` reports a crime | confirmed (code) |
| `0x003f8f38` | `DoorSwing_PinLeaves` | helper (message `0x10` of `dyn_door_swinging`) | pins both leaves' spawn records (`ObjRecord_SetPinned`), so streaming keeps them, marks data `+0x1c` and links the two (`0x003a50a8`) | confirmed (code) |

### Spray tag helpers {#spray-tag-helpers}

The message helpers of `part_spray_tag` ([Tag spots](crimes.md#tag-spots)). The tag keeps a fade mode (data `+0x04`),
an alpha (`+0x10`), a size (`+0x18`) and a linked object (`+0x1c`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003fbe98` | `SprayTag_SpawnMist` | message `0x41` | while the alpha is between 0.03 and 0.95, a few `spray_mist` pieces in the tag's paint colour | confirmed (code) |
| `0x003fc218` | `SprayTag_SetSprite` | helper (init, `0x27`, `0x3b`) | frees the old sprite batch (`0x003a4f78`), makes a new one and puts its rectangle in the sprite word | confirmed (code) |
| `0x003fc2b0` | `SprayTag_OnShow` | message `0x12` | fading in (mode 7 and alpha below 1, or mode 5 and alpha above 0): message `0x17` to the linked object and `0x19` with the mode to itself; else `0x13` to the linked object | confirmed (code) |
| `0x003fc408` | `SprayTag_OnHide` | message `0x13` | `0x13` to the linked object, fade mode 0 | confirmed (code) |
| `0x003fc478` | `SprayTag_SetFadeMode` | message `0x19` | sub-command 3: mode 0 and hide the linked object; 4: alpha 0, mode 7; 5: mode 1; 6: alpha 1; 7: mode 2 | confirmed (code) |
| `0x003fc568` | `SprayTag_SetSize` | message `0x3b` | size = 10 + 0.1 × the value (10 when negative), then a new sprite | confirmed (code) |

### Truck sounds {#truck-sounds}

Two hidden sound emitters for a parked truck or bus (flags 4). Both switch on with **`0x12`** and off with **`0x13`**
or **`0x20`**; message 8 clears the attached flag `0x10`. `part_truck_sound` stays silent when the current level
record's type is `0x33` and game state `+0x33a` is 6 (inferred: one level's bus scene plays its own).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003fcbe0` | `PartTruckSound_Init` | `part_truck_sound` init | hidden, update every 2, on unless in that level setting | confirmed (code) |
| `0x003fccd8` | `PartTruckSound_OnMessage` | `part_truck_sound` message | `0x12` on (volume from 0), `0x13` / `0x20` off and stop, 8 detach | confirmed (code) |
| `0x003fce60` | `PartTruckSound_Update` | `part_truck_sound` update | fades the volume in over 1 s; plays the bus idle loop (`vags/cutscenes/l51/bus_idle_02`) at its position while the camera is within the sound's far distance + 10 m, else stops it | confirmed (code) |
| `0x003fd0c0` | `PartTruckHumans_Init` | `part_truck_humans` init | update every 10; reads the voice ids (class `+0x118`) of classes 100-105 | confirmed (code) |
| `0x003fd1c0` | `PartTruckHumans_OnMessage` | `part_truck_humans` message | `0x12` on (update 10), `0x13` off (update 240), `0x20` off, 8 detach | confirmed (code) |
| `0x003fd2b8` | `PartTruckHumans_Update` | `part_truck_humans` update | while on and no line plays, the next of the six voices says speech command `0x17` at its position | confirmed (code); people inside the truck inferred from the name |

### `melee_weapon` {#melee-weapon}

The class of hand-held weapons (`CfgObj` class `melee_weapon`; [Held objects](objects.md#held), [Breakables and
pick-ups](combat.md#breakables)). The message handler (`MeleeWeapon_OnMessage`) and the break (`MeleeWeapon_Break`)
are documented there. Data: `+0x10` state (2 thrown, −5 broken), `+0x14` the update interval, `+0x1c` a linked
object's handle, `+0x20` / `+0x24` a counter and its limit.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003fd420` | `MeleeWeapon_Init` | `melee_weapon` init | flags `0x218081`, update 20, model; one model (hash `0x13ff8ee4`) clears `0x80`; a type of `CfgObj` kind 11 links to itself (message 10 off) with a counter of 6-11 | confirmed (code) |
| `0x003fe9c0` | `MeleeWeapon_OnThrown` | message `0x30` | released (held flag off), spin 4-6, update every 2, flags `0x4208000`, state 2 | confirmed (code) |
| `0x003fee68` | `MeleeWeapon_Update` | `melee_weapon` update | update 20 when grounded, 2 when airborne; broken (−5): the link dropped and done; else every n updates (2; 6-11, or 12-23 while held) messages the link (inferred: a flicker); on landing it settles (rotation and spin reset below 0.5 m/s) | confirmed (code) |

### `sub_detergent` {#sub-detergent}

A burst popped with a kind 0-3 that picks two colours (kind 2 uses a smaller, random size); every kind but 2 throws 4
large and 80 small pieces at once. Then 8 updates (every 5 ticks) of trailing pieces in the kind's colour.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ff150` | `SubDetergent_Init` | `sub_detergent` init | pops the kind, hidden, update every 5, the first bursts | confirmed (code) |
| `0x003ff5c0` | `SubDetergent_OnMessage` | `sub_detergent` message | an empty stub | confirmed (code) |
| `0x003ff5e8` | `SubDetergent_Update` | `sub_detergent` update | a piece in the kind's colour per update; done at 8 | confirmed (code) |

### `overhead_weapon` {#overhead-weapon}

Large objects carried over the head and thrown (chairs, trash cans, appliances). Data: `+0x00` a counter, `+0x04`
state (2 thrown, −5 broken), `+0x08` the update interval, `+0x0c` breakable. The initialiser marks it breakable for a
list of 18 model hashes, or for any model while game state flag word 3 bit 0 is set; bit 3 clears a flag of its
collision body.

Messages (`OverheadWeapon_OnMessage`): **0** a pick-up offer (held flag `0x100000`, message `0x14` to the human),
**1** the break, **4** state 0 and update 2, **10** hittable on/off, **`0x15`** flag `0x40`, **`0x19`** flag
`0x8000`, **`0x1b`** picked up, **`0x1c`** dropped, **`0x30`** thrown (spin 4-6, airborne).

**The break** (`OverheadWeapon_Break`, 11 KB): marks the state −5 and, by the model's hash, plays a long chain of
effects, each spawned only while the particle pool has room: dust and spark bursts of set colours, coloured debris
(`sub_debris`), and **broken pieces as new objects** with random spin: chair parts (`dyn_chair_ba` ... `dyn_chair_wd`,
by chair), trash (`dyn_trashbit_a`/`b`/`d`, `dyn_parktrash_aa`, `dyn_trashcan_b`), `dyn_beerbottle`, `dyn_bumcart_aa`,
appliance parts (`dyn_dryer_ba`, `dyn_fridge_ca`, `dyn_stereo_a`, `dyn_vargas_stove_aa`, `dyn_vargas_washer_aa`),
and for one model a random one of seven hobo foods (`dyn_hobo_donut_a` ... `dyn_hobo_steak`). Some models instead
send a message to a linked object. It then plays the type's break sound unless game flag word 3 bit 2 is set (at
0.65 volume when the holder `+0x368` is set). Confirmed (code) for the structure and the names; which hash is which
model is not traced.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003ff6c8` | `OverheadWeapon_Init` | `overhead_weapon` init | flags `0x228001`, update 20, model, breakable by model list or game flag | confirmed (code) |
| `0x003ff918` | `OverheadWeapon_OnPickedUp` | message `0x1b` | the first time, sets flag 8 of the path polygon under it; resets its rotation, attaches to the human's bone, attached flag on; messages 3 and `0x17` (with `CfgObj` field 3) to the human | confirmed (code) |
| `0x003ffb48` | `OverheadWeapon_OnDropped` | message `0x1c` | while attached: detached, airborne, state 2, a random spin for seven models (wider for two); held flag off | confirmed (code) |
| `0x003ffe90` | `OverheadWeapon_Break` | message 1 | the break above | confirmed (code) |
| `0x00402bf8` | `OverheadWeapon_OnMessage` | `overhead_weapon` message | the messages above | confirmed (code) |
| `0x00402e70` | `OverheadWeapon_Update` | `overhead_weapon` update | broken: sets the path flag once and is done; airborne below z velocity −500: done; with flag `0x40`, after 4 updates the hit is cleared; thrown: after 30 updates flag `0x8000` and state 0; on landing the landing flag is cleared | confirmed (code) |

### `thrown_weapon` {#thrown-weapon}

Small thrown objects (bottles, bricks). As `melee_weapon` plus a break by model. Two models (hashes `0x0191ccb4` and
`0x26b7048b`) take a random tint from an eight-entry table at `0x00514770` (the second also with the `0x80` bit).
Data: `+0x20` state, `+0x24` update interval, `+0x30` a linked object's handle, `+0x34` / `+0x38` its counter.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00403090` | `ThrownWeapon_Init` | `thrown_weapon` init | flags `0x228081`, update 20, model, tint, link for `CfgObj` kind 11 | confirmed (code) |
| `0x004032c0` | `ThrownWeapon_OnPickedUp` | message `0x1b` | rotation reset, attached to the human's bone, messages 3 and `0x17` to him | confirmed (code) |
| `0x004034b8` | `ThrownWeapon_Break` | message 1 | state −5; by model hash, bursts of set colours and pieces (4-7 for the first tinted model, 10-13 for the second; inferred: glass), and for one model 60 particles | confirmed (code) |
| `0x004041a8` | `ThrownWeapon_OnMessage` | `thrown_weapon` message | 0 pick-up offer, 1 break, 4 update 2, 8 detach, 10 hittable (off also stops the link), `0x19` flag `0x8000`, `0x1b` picked up, `0x1c` dropped (detached, airborne), `0x20` the link off, `0x30` thrown, `0x32` attach to a human | confirmed (code) |
| `0x00404660` | `ThrownWeapon_Update` | `thrown_weapon` update | as `melee_weapon`'s; airborne below z velocity −500: done; on landing two models bleed (`sub_blo`, a message to itself) or set state −10; broken: the link dropped and done | confirmed (code) |

### The Molotov {#molotov}

`dyn_molotv` (the bottle) carries a `sub_molotv_flame` (data `+0x14`), which makes a `sub_molotv_light`.
`molotov_smoke` is a smoke puff. The bottle's data: `+0x10` the holder, `+0x18` state (−5 broken), `+0x1c` update
interval, `+0x24` its fuse sound.

**Set off by message `0x15`** (what [`BreakObjectsInRadius`](objects.md#break-objects-in-radius) sends), confirmed
(code) at `0x00404c48` and `0x00405600`:

1. `0x15`: flag `0x40` on, data `+0x1c` = 2. The task keeps its current schedule (20 ticks from the init).
2. Each update sets its interval to data `+0x1c` and, with flag `0x40`, adds one to it; when it reaches 4 (the
   second update after `0x15`, two ticks after the first) the bottle sends **itself message 1** with itself as the
   breaker, its position and the normal (0, 0, 1).
3. Message 1 (the break): the fuse sound stops if one plays; the bottle's rotation is turned from its velocity
   reflected about the normal; **one `sub_explode`** is made at the position **+ 0.7 m up** with that rotation;
   state −5. (No `sub_debris` here: the debris comes from the `part_explosion` the `sub_explode` makes,
   [Explosions](#part-explosion).)
4. The next update (3 ticks later) sees state −5: the flame is told `0x13` (out) and `0x15` (remove), the particles
   named at `0x00587620` within 1 m are killed, and the bottle ends (update returns 1).

The flame is never lit on this path (only messages 4, `0x12` and `0x1b` light it), so a molotov set off this way
shows no `sub_molotv_flame`, no `sub_molotv_light` and no `molotov_smoke`, and **leaves no fire behind**: nothing on
the path makes `sub_flames`, `sub_burn` or a fire. None of the functions on the path plays a sound. From the
`0x15` to the flash: up to about 22 ticks (the remaining part of the 20-tick schedule, then 2); inferred from the
intervals.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00404b38` | `DynMolotov_Init` | `dyn_molotv` init | flags `0x228081`, update 20, model, data defaults | confirmed (code) |
| `0x00404c48` | `DynMolotov_OnMessage` | `dyn_molotv` message | 0 pick-up offer; 1 break: fuse sound stopped, one `sub_explode` 0.7 m above it, state −5; 4 thrown (the flame lit with `0x12`); 10 hittable (forwarded to the flame); `0x12` light; `0x15` flag `0x40`; `0x1b` picked up (lights the flame, attaches to the hand); `0x1c` dropped (flame told `0x22`); `0x20` put out (flame `0x13`, then `0x15`); `0x30` thrown | confirmed (code) |
| `0x00405600` | `DynMolotov_Update` | `dyn_molotv` update | broken: tells the flame and ends; airborne below z velocity −500: state −5; keeps its last position | confirmed (code) |
| `0x004058c8` | `MolotovSmoke_Init` | `molotov_smoke` init | size, colours, one of three smoke sprites, update every 2 | confirmed (code) |
| `0x00405a08` | `MolotovSmoke_Update` | `molotov_smoke` update | grows over four steps (update 15, then 45 at the last), darkening; then done | confirmed (code) |
| `0x00405bd8` | `SubMolotovLight_Init` | `sub_molotv_light` init | a light attached to its parent, radius 1.5, update every 5 | confirmed (code) |
| `0x00405c98` | `SubMolotovLight_OnMessage` | `sub_molotv_light` message | 10 on/off, `0x15` remove (state 4), `0x19` radius | confirmed (code) |
| `0x00405d90` | `SubMolotovLight_Update` | `sub_molotv_light` update | every 10-15 ticks switches between two colours (a flicker); done at state 4 | confirmed (code) |
| `0x00405e78` | `SubMolotovFlame_Init` | `sub_molotv_flame` init | attached to its parent, update every 2, flame defaults | confirmed (code) |
| `0x00405f78` | `SubMolotovFlame_OnMessage` | `sub_molotv_flame` message | 8 detach; 10 on/off (forwarded to its light); `0x12` lit (makes and attaches its light); `0x13` out (light removed); `0x15` remove; `0x22` light radius 4 | confirmed (code) |
| `0x004061c0` | `SubMolotovFlame_Update` | `sub_molotv_flame` update | steps the flame; at step 0 attaches a new flame piece (one of four); at step 8 drops its light and is done | confirmed (code) |

### `dyn_chicken` {#dyn-chicken}

A live chicken that can be picked up, carried and thrown, squawking. Data: `+0x04` state (−5 dead), `+0x08` the
update interval, `+0x0c` path flag set, `+0x10` / `+0x14` two sound handles.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00406458` | `DynChicken_Init` | `dyn_chicken` init | flags `0x228001`, update 20, model, sound handles; game flag word 3 bit 3 clears a body flag | confirmed (code) |
| `0x004065c8` | `DynChicken_OnPickedUp` | message `0x1b` | the first time sets the path flag; a 3D squawk (hash `0x44c1e078`) and a 2D voice; attached to the human's bone; messages 3 and `0x17` to him | confirmed (code) |
| `0x00406838` | `DynChicken_OnDropped` | message `0x1c` | detached and airborne; the voice restarted | confirmed (code) |
| `0x00406a08` | `DynChicken_Break` | message 1 | 40 blood pieces (`sub_blo`), bursts, hidden, sounds stopped, state −5 | confirmed (code) |
| `0x00406e20` | `DynChicken_OnMessage` | `dyn_chicken` message | 0 pick-up offer, 1 killed, 4 update 2, 10 hittable, `0x1b`, `0x1c`, `0x20` sound off, `0x30` thrown | confirmed (code) |
| `0x00407068` | `DynChicken_Update` | `dyn_chicken` update | dead: sets the path flag, done after 30 updates; airborne below z velocity −500: done; while held its sounds follow it | confirmed (code) |

### `Wind_Manager` {#wind-manager}

One hidden task (update every 2) that owns the wind vectors at `0x006f31b0` and `0x006f31c0`, which the blowing
litter and the steam puffs read ([Blowing litter](particles.md#garbage)). Its update is `0x00407318`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x004071b8` | `WindManager_TypeInit` | `Wind_Manager` init | hidden, update every 2; both vectors random in x and y, z 0; strength 1 | confirmed (code) |
| `0x00407730` | `WindManager_OnMessage` | `Wind_Manager` message | an empty stub | confirmed (code) |
| `0x00407798` | `WindManager_GlobalCtor` | static constructor (table `0x005341ac`) | calls the file's static initialiser `0x00407758` with (1, `0xffff`) | confirmed (code) |

### Wood splinters {#wood-splinters}

`sub_wood_splinter` throws `wood_splinter_bit` pieces (through `Spawn_WoodSplinterBit`), each a small sprite that
falls and plays a material sound when it lands.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x004077b8` | `WoodSplinterBit_Init` | `wood_splinter_bit` init | pops sound material, colour, size and kind; a random splinter sprite (flagged when tiny); flags `0x14400001` (game flag word 0 bit 0 clears `0x10000000`; bit 2 drops the sound) | confirmed (code) |
| `0x00407ae0` | `WoodSplinterBit_Update` | `wood_splinter_bit` update | counts steps up to its limit; on landing plays the material pair (`Sound_PlayMaterialPair`) and settles; then fades and ends | confirmed (code) |
| `0x00407d50` | `WoodSplinterBit_OnMessage` | `wood_splinter_bit` message | an empty stub | confirmed (code) |
| `0x00407d78` | `SubWoodSplinter_Init` | `sub_wood_splinter` init | pops count, colour, sound, size, spread, lifetime (× 60) and speed; hidden | confirmed (code) |
| `0x00407ec0` | `SubWoodSplinter_OnMessage` | `sub_wood_splinter` message | an empty stub | confirmed (code) |
| `0x00407ee8` | `SubWoodSplinter_Update` | `sub_wood_splinter` update | spawns the bits in pairs (a quarter of them with game flag word 0 bit 1), the sound on the first only; done at once | confirmed (code) |

### Shack puffs {#shack-puffs}

Dust puffs (from `part_s_shack_dust_puff`): a puff sprite (one of three) that grows over two steps of 45 ticks to
0.6 × its size and ends. The aligned one is flat (flags 1), the other a camera-facing sprite (flags `0x1000`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00408610` | `SubShackPuffAligned_Init` | `sub_shack_puff_aligned` init | colour and size popped, start at 0.3 × size, update every 45 | confirmed (code) |
| `0x00408798` | `SubShackPuffAligned_OnMessage` | `sub_shack_puff_aligned` message | an empty stub | confirmed (code) |
| `0x004087c0` | `SubShackPuffAligned_Update` | `sub_shack_puff_aligned` update | alpha cleared, size 0.6 × size; done after two steps | confirmed (code) |
| `0x00408860` | `SubShackPuff_Init` | `sub_shack_puff` init | as the aligned one, flags `0x1000` | confirmed (code) |
| `0x00408a00` | `SubShackPuff_OnMessage` | `sub_shack_puff` message | an empty stub | confirmed (code) |
| `0x00408a28` | `SubShackPuff_Update` | `sub_shack_puff` update | as the aligned one | confirmed (code) |
