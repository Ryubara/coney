# SPDX-License-Identifier: GPL-3.0-or-later
"""The topics of the game reference lists: each one's file, key, fields and page layout.

The order here is the order of the index page and of the navigation. A field's `doc` is rendered on the topic's
page as its schema, so it is written for a modder reading the page.

Research: docs/guides/research-workflow.md#reference-lists
"""

from __future__ import annotations

from coney_tools import refs_env
from coney_tools.refs import Field, Topic
from coney_tools.refs_engine import CAMERAS, GOAL_TYPES, SCREEN_EFFECTS

F = Field

CHARACTERS = Topic(
    "characters",
    "id",
    "char",
    (
        F("id", "int", "Character type: the number `HuCreate` takes and `CfgChar` configures.", "Type", required=True),
        F("label", "str", "Our short name for the type (who it is).", "Who", curated=True),
        F("model", "str", "Model name: the Character List entry the type uses.", "Model"),
        F("armies_model", "str", "The `<model>_a` model used instead in levels 60-64, when the list has one."),
        F(
            "faction",
            "str",
            "The group the model's name prefix stands for (our reading of the prefix).",
            "Faction",
            prose=True,
        ),
        F("behaviour", "int", "Byte `+0x11a`: the brain kind given to the human's AI (3 the Warriors, 4 civilians)."),
        F(
            "category",
            "int",
            "Byte `+0x11b`: role category; picks the cash range and civilian reactions (14 Warriors).",
        ),
        F(
            "speed_class",
            "int",
            "Byte `+0x11c`: the [speed class](speed-classes.md) used when no clip speeds exist.",
            "Speed class",
            link="speed-classes.md#speed",
        ),
        F("v11d", "int", "Byte `+0x11d`, copied to the human (`+0x1b8`); meaning not traced."),
        F("health", "int", "16-bit `+0x116`, copied to the human's maximum and current health.", "Health"),
        F("damage_table", "str", "The 45-entry damage table (a global of `config_preload2.lua`)."),
        F("attack_table", "str", "The 45-entry attack table."),
        F("damage_scale", "float", "Multiplier applied to the damage table."),
        F("range_table", "str", "The 45-entry range table."),
        F("hat", "str", "Hat object worn (an [object](objects.md) name), or none."),
        F(
            "voice",
            "int",
            "Voice set: the folder number of `vags/character/voices/<n>/...` (16-bit `+0x118`).",
            "Voice",
        ),
        F(
            "flag_14b",
            "int",
            "Byte `+0x14b`, copied to the human (`+0x3b8`); changes the body scale and a civilian colour.",
        ),
        F("drop_group", "str", "Item or object group the human may carry (`+0x16c`)."),
        F(
            "drop_chance",
            "int",
            "Value at `+0xb4` used with the item: compared with a random 0-99 (a chance, inferred).",
        ),
        F("weapon", "str", "Weapon object the human is given at creation (`+0x18c`), or none.", "Weapon"),
        F(
            "speeds",
            "dict",
            "Walk, jog, run and sprint in m/s from the model's clips (root displacement / duration).",
            "Speeds (m/s)",
        ),
        F(
            "script_names",
            "list",
            "Names level scripts give humans of this type (`HuCreate`'s first argument).",
            "Script names",
        ),
        F("levels", "list", "Level numbers whose scripts create this type with a constant type argument.", "Levels"),
    ),
    nav="Characters",
    images=True,
)

CHARACTER_MODELS = Topic(
    "character-models",
    "index",
    "model",
    (
        F("index", "int", "Position in the Character List (stable for the NTSC-U disc).", "Record", required=True),
        F("crc", "hex", "CRC-32 of the model name, lower case: what the game looks up.", "CRC", required=True),
        F("name", "str", "The model name, when recovered.", "Model"),
        F("data", "str", "Name of the character data resource (animations and anim table), when recovered.", "Data"),
        F("data_crc", "hex", "Hash naming the character data resource."),
        F("geo_crc", "hex", "Hash naming the model resource; the name is `<model>_geo`."),
        F("tex_crc", "hex", "Hash naming the texture dictionary; the name is `<model>_tex`."),
        F("data_size", "int", "Size of the character data in bytes."),
        F("geo_size", "int", "Size of the model resource in bytes."),
        F("tex_size", "int", "Size of the texture dictionary in bytes."),
        F("types", "list", "Character types (`CfgChar`) that use the model.", "Types", link="characters.md#char"),
    ),
    compact=True,
    nav="Character models",
    images=True,
)

GANGS = Topic(
    "gangs",
    "id",
    "gang",
    (
        F("id", "int", "Gang type: `CfgGang`'s first argument and `GangCreate`'s.", "Type", required=True),
        F("label", "str", "Who the gang type is, in our words.", "Who", curated=True),
        F("strategy", "str", "The strategy table passed (a `Strat...` global of `config_preload2.lua`).", "Strategy"),
        F("strategy_values", "list", "That table's seven numbers (bytes `+0x73`...)."),
        F("v2", "int", "Byte `+0x6c` of the gang record."),
        F("v3", "int", "Byte `+0x6d`."),
        F("v4", "int", "Byte `+0x6e`."),
        F("v5", "int", "Byte `+0x6f`."),
        F("v7", "int", "Byte `+0x70`."),
        F("v8", "int", "Byte `+0x71`."),
        F("v9", "int", "Byte `+0x72`."),
        F("music", "list", "The three music tracks `CfgGangMusic` gives the gang type.", "Music"),
        F(
            "script_names",
            "list",
            "The names level scripts give gangs of this type (`GangCreate`), most used first.",
            "Script names",
        ),
    ),
    nav="Gangs",
)

SPEED_CLASSES = Topic(
    "speed-classes",
    "id",
    "speed",
    (
        F("id", "int", "Speed class number.", "Class", required=True),
        F("base", "float", "Entry `+0x00`: base speed (m/s).", "Base"),
        F("v04", "float", "Entry `+0x04`: no reader found.", "+0x04"),
        F("walk", "float", "Walk (m/s).", "Walk"),
        F("jog", "float", "Jog (m/s).", "Jog"),
        F("run", "float", "Run (m/s).", "Run"),
        F("sprint", "float", "Sprint (m/s).", "Sprint"),
        F("types", "int", "How many character types name the class.", "Types"),
    ),
    nav="Speed classes",
)

OBJECT_GROUPS = Topic(
    "object-groups",
    "name",
    "group",
    (
        F("name", "str", "Group name as referenced.", "Group", required=True),
        F("defined", "bool", "Whether any script defines the group with `CfgObjectGroup`.", "Defined"),
        F(
            "referenced_by",
            "list",
            "Character types whose `CfgChar` names the group.",
            "Referenced by",
            link="characters.md#char",
        ),
    ),
    nav="Object groups",
)

OBJECTS = Topic(
    "objects",
    "name",
    "obj",
    (
        F("name", "str", "Object type name (`CfgObj`'s first argument); also the model's name.", "Name", required=True),
        F("category", "str", "Our grouping, from the object type (weapons, hats, piles, doors ...).", "Category"),
        F("class", "str", "The object class name (`hat_object`, ...).", "Class"),
        F("type", "str", "The `OBJECT` type constant.", "Type"),
        F("type_value", "int", "Its number."),
        F("shape", "str", "Physics shape (`PHYS` constant).", "Shape"),
        F("axis", "str", "Main axis (`AXIS` constant)."),
        F("size", "list", "Collision box size {x, y, z} in metres.", "Size (m)"),
        F("mass", "float", "Float at `+0x88` (0.1 for hats; a mass, inferred)."),
        F("material", "str", "Surface material (`MATERIAL` constant)."),
        F("pickup_anim", "str", "Pick-up animation (`ANIM` constant)."),
        F("anim_set", "str", "Animation set used while holding it (`ANIM` constant)."),
        F("script", "str", "The preload that configures it."),
    ),
    group_by="category",
    compact=True,
    nav="Objects and weapons",
    images=True,
)

LEVELS = Topic(
    "levels",
    "index",
    "level",
    (
        F(
            "index",
            "int",
            "Level record index (`GetCurrentLevelIndex`); the list's position minus one.",
            "Index",
            required=True,
        ),
        F("name", "str", "Level name: `<name>.lev`, `<name>.lua`, `MenuLoadLevel(name)`.", "Name"),
        F("world", "str", "Streamed world name (`<world>s` / `<world>d`).", "World"),
        F("number", "int", "Level number (`GetLevelId`); names the intro movie `L<n>_IN`.", "Number"),
        F(
            "kind",
            "str",
            "Our reading of what the level is (story mission, hub, rumble arena, test ...).",
            "Kind",
            curated=True,
        ),
        F("sections", "int", "Record `+0x08`: number of sections (packs `<name>_<k>.pak`).", "Sections"),
        F("order", "int", "Record `+0x0c`: a small number rising through the story list (meaning not traced)."),
        F("flag1", "str", "First `LT_*` value (record flag bit 0)."),
        F("intro", "str", "Second `LT_*` value: `LT_PLAY` sets flag bit 1, play the intro movie.", "Intro"),
        F("outro", "str", "Third `LT_*` value: `LT_PLAY` sets flag bit 2, play an outro movie.", "Outro"),
        F("lock", "str", "`LOCKED` or `UNLOCKED` (record `+0x10`).", "Lock"),
        F("map", "list", "Three floats (record `+0x6c`-`+0x74`); x and y look like a map position (inferred)."),
        F("v78", "hex", "Record `+0x78`: a word in two 16-bit halves (meaning not traced)."),
        F("subway", "str", "`SUBWAY_*` line constant (record `+0x7c`).", "Subway"),
        F("v80", "hex", "Record `+0x80`: a word in two 16-bit halves (meaning not traced)."),
        F("has_lev", "bool", "Whether `<name>.lev` exists on the disc (a level without one cannot load).", ".lev"),
        F("packs", "int", "Number of `<name>_<k>.pak` packs on the disc."),
    ),
    nav="Levels",
)

LEVEL_STARTS = Topic(
    "level-starts",
    "id",
    "start",
    (
        F(
            "id",
            "str",
            "`<level>-<checkpoint>` for a story level, `<level>-<mode>` for a Rumble arena.",
            "Start",
            required=True,
        ),
        F("level", "str", "The level's name (a [level](levels.md) record's name).", "Level"),
        F(
            "checkpoint",
            "int",
            "Checkpoint (`GetCheckPoint`, `W_GameState + 0x33a`) the start belongs to.",
            "Checkpoint",
        ),
        F("mode", "str", "Rumble mode whose flag script holds the start (`level<N>_<mode>_init.lua`).", "Mode"),
        F("character", "str", "The name the script gives player 1's human (`HuCreate`'s first argument).", "Name"),
        F(
            "type",
            "int",
            "Player 1's [character type](characters.md); a Rumble arena's comes from the chosen gang.",
            "Type",
            link="characters.md#char",
        ),
        F("pos", "list", "Position `{x, y, z}` in metres, game axes (z up), before the ground snap.", "Position"),
        F("heading", "float", "Facing in degrees about z, as `HuCreate` or the flag gives it.", "Heading"),
        F("via", "str", "Where the value comes from: the creating function or the flag."),
        F(
            "script",
            "str",
            "The script the start loads: the checkpoint's chapter script or the mode's flag script.",
            "Script",
        ),
    ),
    compact=True,
    nav="Level starts",
)

WORLD_FLAGS = Topic(
    "flags",
    "id",
    "flag",
    (
        F("id", "str", "`<script>:<name>`, with `#2`, `#3` ... for a name the same script adds again.", required=True),
        F("level", "str", "The level whose scripts add the flag; none for a shared script.", "Level"),
        F(
            "name",
            "str",
            "The flag's name (`AddFlag`'s first argument; `FindFlag` matches the first 15 characters).",
            "Flag",
        ),
        F(
            "pos",
            "list",
            "Position `{x, y, z}` in metres, game axes (z up), when the script gives it literally.",
            "Position",
        ),
        F("heading", "float", "Facing in degrees about z.", "Heading"),
        F(
            "activity",
            "int",
            "Fourth argument (flag `+0xd0`): what a human does there; picks the clip and who may use the flag "
            "([activities](../research/flags.md#activities)). 0, none, is left out.",
            "Activity",
        ),
        F(
            "group",
            "int",
            "Fifth argument (flag `+0xd8`): a store's group number or a path network's bits "
            "([groups](../research/flags.md#groups)). 0 is left out.",
            "Group",
        ),
        F("script", "str", "The script that adds it (without `.lua`).", "Script"),
    ),
    group_by="level",
    compact=True,
    nav="World flags",
    split=True,
)

OBJECT_ZONES = Topic(
    "zones",
    "id",
    "zone",
    (
        F("id", "str", "`<level>:<zone>`.", required=True),
        F("level", "str", "The level whose scripts use the zone.", "Level"),
        F("zone", "int", "Zone number (`ObjEnableZone`'s first argument, `ObjSpawn`'s fifth).", "Zone"),
        F("names", "list", "The globals and table fields the level's scripts give the number.", "Names"),
        F("objects", "int", "How many `ObjSpawn` calls of the level put an object in the zone.", "Spawned objects"),
        F("enabled_by", "list", "Scripts that switch the zone on.", "Enabled by"),
        F("disabled_by", "list", "Scripts that switch it off.", "Disabled by"),
    ),
    group_by="level",
    nav="Object zones",
)

VOLUME_BOXES = Topic(
    "boxes",
    "id",
    "box",
    (
        F("id", "str", "`<script>:<name>`, with `#2`, `#3` ... for a name the same script adds again.", required=True),
        F("level", "str", "The level whose scripts add the box; none for a shared script.", "Level"),
        F("name", "str", "The box's name (`AddVolumeBox`'s first argument).", "Box"),
        F("kind", "int", "`AddVolumeBox`'s second argument: 0, 2 or 3 (1 is refused).", "Kind"),
        F("class", "str", "The class the kind makes: `VolumeBox`, `PlayerBox` or `TurfBox`.", "Class"),
        F("corner", "list", "Minimum corner `{x, y, z}` in metres.", "Corner"),
        F("size", "list", "Extent `{dx, dy, dz}` in metres.", "Size"),
        F("held_in", "str", "The global the level's scripts keep the handle in.", "Held in"),
        F("turf_of", "list", "Gangs (the scripts' globals) given the box as turf with `GangAddTurfBox`.", "Turf of"),
        F("script", "str", "The script that adds it (without `.lua`).", "Script"),
    ),
    group_by="level",
    compact=True,
    nav="Volume boxes",
)

SCENES = Topic(
    "scenes",
    "id",
    "scene",
    (
        F("id", "str", "A scene's name; `movie:<name>` for a movie.", required=True),
        F("kind", "str", "`movie` or `scene`.", "Kind"),
        F("name", "str", "The name `PlayMovie` or `ScenePreload` takes.", "Name"),
        F("scene", "int", "Scene id: the record's index in `scene_list.cnk`, what `ScenePreload` returns.", "Scene id"),
        F("size", "int", "The scene record's size in bytes."),
        F("levels", "list", "Levels whose scripts preload the scene by name.", "Levels"),
        F("scripts", "list", "Scripts that preload it by name.", "Preloaded by"),
        F(
            "played_by",
            "list",
            "What plays the movie: the boot, a level record's intro or outro, the front end.",
            "Played by",
        ),
        F(
            "on_disc",
            "bool",
            "Whether the disc's `PSS` folder holds `<name>.BIK` (unknown for a disc image).",
            "On disc",
        ),
    ),
    group_by="kind",
    compact=True,
    nav="Scenes and movies",
)

ANIMATIONS = Topic(
    "animations",
    "id",
    "clip",
    (
        F(
            "id",
            "str",
            "Our stable id: the clip name, with `#2`, `#3` ... for clips that share a name.",
            "Clip",
            required=True,
        ),
        F("name", "str", "The name in the clip's descriptor (cut to 30 characters)."),
        F("frames", "int", "Length in frames at 30 a second.", "Frames"),
        F("duration", "float", "Length in seconds.", "Seconds"),
        F("distance", "float", "Horizontal root displacement over the clip (m).", "Distance (m)"),
        F("events", "int", "Number of events in the clip."),
        F("characters", "list", "Character data resources holding the clip.", "In character data"),
        F("files", "list", "Standalone `.anm` resources holding it.", "In .anm"),
        F("packs", "int", "How many packs hold a copy.", "Packs"),
    ),
    compact=True,
    nav="Animation clips",
)

ANIM_IDS = Topic(
    "anim-ids",
    "id",
    "anim",
    (
        F("id", "int", "Anim id (0-721): an index into every character data's anim table.", "Id", required=True),
        F("name", "str", "The `ANIM_*` name the scripts give the id.", "Name"),
        F("slot", "int", "Locomotion slot (0-34) that holds this id by default.", "Slot"),
        F("generic", "str", "The clip the generic character data plays for it (`generic_header`).", "Generic clip"),
        F(
            "rembrandt",
            "str",
            "The clip Rembrandt's character data plays for it (`warr_re_header`).",
            "Rembrandt's clip",
        ),
    ),
    compact=True,
    nav="Anim ids",
)

CONTROLS = Topic(
    "controls",
    "id",
    "ctl",
    (
        F("id", "str", "Our stable id.", required=True),
        F("context", "str", "Where it applies: pad, menus, on foot, scripts ...", "Context"),
        F("input", "str", "The pad input (gamepad with analog sticks).", "Input", curated=True),
        F("action", "str", "What it does there.", "Action", curated=True),
        F("bit", "hex", "Bit in the game's button word.", "Bit"),
        F("glyph", "str", "The markup tag that shows its icon in text.", "Glyph"),
    ),
    group_by="context",
    nav="Controls",
)

HUD_COLOURS = Topic(
    "hud-colours",
    "id",
    "colour",
    (
        F("id", "str", "The key in `global.lua`'s colour table `CL`.", "Key", required=True),
        F("rgba", "str", "The colour as `RRGGBBAA` hex.", "RGBA"),
        F("swatch", "str", "A swatch of the colour (rendered).", "Swatch"),
        F("markup", "str", "The text the table holds (a `<COLOR>` tag, or a list of four bytes)."),
        F("hud_slot", "int", "`CfgHUDColor` slot the colour is given to, if any.", "HUD slot"),
    ),
    nav="HUD colours",
)

TEXT_FORMATTING = Topic(
    "text-formatting",
    "index",
    "tag",
    (
        F("index", "int", "Position in the tag table (`0x0050d718`, 66 entries of 61 bytes).", "Index", required=True),
        F("tag", "str", "The tag as the table spells it (arguments follow where the tag has a trailing space).", "Tag"),
        F("effect", "str", "What it does.", "Effect", curated=True),
        F("char", "hex", "For icon tags: the font character drawn.", "Character"),
    ),
    nav="Text formatting",
)

ENUMS = Topic(
    "enums",
    "id",
    "enum",
    (
        F("id", "str", "`ENUM.NAME` (or the global's name for loose constants).", required=True),
        F("enum", "str", "The table or prefix the value belongs to.", "Enum"),
        F("name", "str", "The constant's name.", "Name"),
        F("value", "any", "Its value.", "Value"),
        F("meaning", "str", "What it means.", "Meaning", curated=True),
        F("script", "str", "The script that defines it.", "Defined in"),
    ),
    group_by="enum",
    compact=True,
    nav="Script enums",
)

SOUND = Topic(
    "sound",
    "id",
    "sound",
    (
        F("id", "str", "Our stable id (the kind and the name or number).", required=True),
        F(
            "kind",
            "str",
            "Music track, interface sound, sound matrix, inventory item, ambient sound, ambient emitter or speech "
            "line.",
            "Kind",
        ),
        F("name", "str", "The sound's name as the scripts pass it (a speech line's whole sound name).", "Name"),
        F("number", "int", "A slot or id number the binding takes.", "Number"),
        F(
            "values",
            "list",
            "The other numbers the binding takes; an ambient sound's slots in the ambient table, an emitter's first "
            "slot and slot count (-1: one named sound).",
            "Values",
        ),
        F("in_list", "bool", "Whether the name is in the game's sound list (`warriors.glr`).", "In sound list"),
        F("used_by", "list", "Where it is configured or played.", "Used by"),
    ),
    group_by="kind",
    compact=True,
    nav="Sound and music",
)

SCRIPT_EVENTS = Topic(
    "script-events",
    "id",
    "event",
    (
        F("id", "int", "Message number (`SetMsgHandler`'s second argument).", "Message", required=True),
        F("meaning", "str", "What the message reports, in our words.", "Meaning", curated=True),
        F("handlers", "int", "How many `SetMsgHandler` calls name a callback for it.", "Handlers"),
        F("clears", "int", "How many calls pass nil (removing a handler)."),
        F("examples", "list", "The most common callback names (evidence for the meaning).", "Example callbacks"),
        F("gang_handlers", "int", "How many `GangSetMsgHandler` calls name a callback for it.", "Gang handlers"),
        F("gang_clears", "int", "How many `GangSetMsgHandler` calls pass nil."),
        F("gang_examples", "list", "The most common gang callback names.", "Example gang callbacks"),
    ),
    nav="Script events",
)

TEXT_LABELS = Topic(
    "text-labels",
    "id",
    "text",
    (
        F("id", "str", "The label as scripts write it: `<table>.<key>`.", "Label", required=True),
        F(
            "table",
            "str",
            "The string table: `GSTRING`, `LABEL`, `TSTRING` (every level), `RUMBLE` (Rumble arenas), `LEVEL<n>` "
            "(one level's own).",
            "Table",
        ),
        F("key", "str", "The key in that table."),
        F(
            "meaning",
            "str",
            "What the text is for, in our words, where the key does not say it.",
            "Meaning",
            curated=True,
        ),
        F(
            "ids",
            "str",
            "For a table the engine or the menus read by number or name (`GSTRING.HUD`): the ids it holds.",
            "Ids",
        ),
        F("size", "int", "For such a table: how many strings it holds.", "Strings"),
        F(
            "languages",
            "int",
            "In how many of the five languages a script defines it (0: used but never defined).",
            "Languages",
        ),
        F("defined_in", "list", "The scripts that define it.", "Defined in"),
        F("uses", "int", "How many places in the scripts name it.", "Uses"),
        F(
            "passed_to",
            "list",
            "What it is passed to, most often first: a binding (`HUDSetObjective`) or a script's own helper "
            "(`ObjectiveSetup`).",
            "Passed to",
        ),
        F("used_in", "list", "The scripts that name it.", "Used in"),
    ),
    group_by="table",
    compact=True,
    nav="Text labels",
)

COMMANDS = Topic(
    "commands",
    "id",
    "cmd",
    (
        F("id", "str", "Our stable id: `trigger:<n>`, `pad:<n>` or `warrior:<n>`.", required=True),
        F("kind", "str", "Trigger kind, pad command or Warrior command.", "Kind"),
        F(
            "number",
            "int",
            "The number scripts pass: `AddCommand`'s trigger, its command id (1-57), or the Warrior command "
            "(`WCIssueCommand`, 0-6).",
            "Number",
        ),
        F("hex", "hex", "A pad command's id in hex, as the research pages write it.", "Hex"),
        F("name", "str", "Our short name for it.", "Name", curated=True),
        F("bound", "list", "How the scripts bind a pad command: `trigger: buttons`.", "Bound to"),
        F("action", "str", "What it does.", "Action", curated=True),
        F("speech", "str", "The speech command a war chief says when giving a Warrior command.", "Says", curated=True),
        F("bound_in", "list", "The scripts that bind it."),
        F("toggled_in", "list", "The scripts that switch a pad command per human (`EnableCommand`).", "Switched in"),
        F("used_in", "list", "The scripts that enable, disable or issue a Warrior command.", "Used in"),
    ),
    group_by="kind",
    nav="Commands",
)

SPEECH = Topic(
    "speech",
    "id",
    "speech",
    (
        F("id", "str", "Our stable id: `command:<n>` or `voice:<n>`.", required=True),
        F("kind", "str", "Speech command or voice set.", "Kind"),
        F(
            "number",
            "int",
            "The number scripts pass: `SoundPlayCommand`'s command (0-206), or a voice set (`CfgChar`'s voice, "
            "`HuSetStateRespVoiceIndex`).",
            "Number",
        ),
        F("name", "str", "The command's name in the executable's table; its lines are `<name>_<nn>`.", "Name"),
        F("meaning", "str", "When it is said, in our words, where the name does not say it.", "Meaning", curated=True),
        F("voices", "int", "How many voice sets have lines for the command.", "Voice sets"),
        F("commands", "int", "How many commands the voice set has lines for.", "Commands"),
        F(
            "lines",
            "int",
            "How many lines the game finds: for a command over all voice sets, for a voice set in all.",
            "Lines",
        ),
        F(
            "types",
            "list",
            "Character types `CfgChar` gives the voice set.",
            "Character types",
            link="characters.md#char",
        ),
        F("percent", "list", "The chance `SndSetCommandSoundPercent` gives the command to be said.", "Chance"),
        F(
            "used_in",
            "list",
            "Scripts that play the command (`SoundPlayCommand`) or give the voice set to a human for its state "
            "responses (`HuSetStateRespVoiceIndex`).",
            "Used in",
        ),
    ),
    group_by="kind",
    compact=True,
    nav="Speech",
)

WAD_NAMES = Topic(
    "wad-names",
    "crc",
    "wad",
    (
        F(
            "crc",
            "hex",
            "The entry's hash in WARRIORS.DIR: CRC-32 of `./ee_files/<name>`, lower case.",
            "CRC",
            required=True,
        ),
        F("name", "str", "The recovered name.", "Name", required=True),
        F("kind", "str", "The name's extension ([WAD contents](../research/formats/wad-contents.md)).", "Kind"),
        F("index", "int", "The entry's position in WARRIORS.DIR (stable for the NTSC-U disc).", "Entry"),
    ),
    group_by="kind",
    compact=True,
    nav="WAD entry names",
)

RADAR_ICONS = Topic(
    "radar-icons",
    "id",
    "radar",
    (
        F("id", "str", "Our stable id: `icon-<n>` for an icon, `blip-<n>` for a blip type.", required=True),
        F("kind", "str", "`icon` (a radar icon id) or `blip type` (the kind of blip the radar adds).", "Kind"),
        F("number", "int", "The icon id `HUDSetRadarItemTexture` takes, or the blip type number.", "Number"),
        F("sheet", "str", "The sprite sheet the icon is a rectangle of.", "Sheet"),
        F("rect", "int", "The rectangle's index in that sheet (for an icon, the icon id itself).", "Rect"),
        F("size", "list", "The rectangle's size in texels {w, h}, read from the disc.", "Size (px)"),
        F("icon", "int", "For a blip type: the icon it starts with (69 when none is set).", "Icon"),
        F("batch", "str", "For a blip type: the radar's sprite batch it is drawn in.", prose=True),
        F("set_by", "str", "Where the game itself sets or uses the number.", "Set by", prose=True),
        F("scale", "float", "The scale the code forces for this icon, if any."),
        F("tint", "str", "The colour (`RRGGBBAA`) the code forces for this icon, if any."),
        F("swatch", "str", "A swatch of that colour (rendered).", "Tint"),
        F("calls", "int", "`HUDSetRadarItemTexture` calls that pass it.", "Calls"),
        F("scripts", "list", "The scripts that pass it (at most six).", "Scripts"),
        F("meaning", "str", "What it marks, in our words.", "Marks", curated=True),
    ),
    group_by="kind",
    nav="Radar icons and blips",
    images=True,
)

PARTICLES = Topic(
    "particles",
    "name",
    "part",
    (
        F(
            "name",
            "str",
            "The type name `SpawnParticle` (or `CfgObj`'s class, for an object behaviour) takes.",
            "Name",
            required=True,
        ),
        F("index", "int", "Position in the script type table (`0x00512f28`, 270 records of 20 bytes).", "Index"),
        F("kind", "str", "From the record's flags: particle system, object behaviour, light or glass.", "Kind"),
        F("flags", "hex", "The record's flags word (`0x10` particle, `0x08` object, `0x04` light, `0x400` glass)."),
        F("init", "hex", "The type's initialiser (first code pointer of the record)."),
        F("sheet", "str", "The sprite sheet its sprite comes from, where traced.", "Sheet"),
        F("rect", "int", "The first rectangle of that sheet it draws, where traced.", "Rect"),
        F("size", "list", "That rectangle's size in texels {w, h}, read from the disc.", "Size (px)"),
        F("spawns", "list", "Other types its code spawns by name (inferred).", "Spawns", link="particles.md#part"),
        F("calls", "int", "`SpawnParticle` calls that name it.", "Script calls"),
        F("scripts", "list", "The scripts that spawn it (at most six).", "Scripts"),
        F("meaning", "str", "What it is, in our words.", "What", curated=True),
    ),
    group_by="kind",
    compact=True,
    nav="Particle effects",
    images=True,
)

CARS = Topic(
    "cars",
    "id",
    "car",
    (
        F("id", "str", "Our stable id: the type name, `part-<n>` or `colour-<n>`.", required=True),
        F("kind", "str", "`type`, `part` or `colour`.", "Kind"),
        F("number", "int", "Type index (`0x00512ba8`), part id or our colour number.", "Number"),
        F("model", "str", "The model resource: a RenderWare clump in chunk `0x47`.", "Model"),
        F("model_hash", "hex", "Its resource hash (CRC-32 of the name)."),
        F(
            "textures",
            "str",
            "The texture dictionary resource (chunk `0x2a`), or its hash when the name is unknown.",
            "Textures",
        ),
        F("atomics", "int", "Atomics in the model's clump.", "Atomics"),
        F("size", "list", "Type: the body box {x, y, z} in metres; part: the part's box (car type 0).", "Size (m)"),
        F("open_number", "int", "Part: the number `CarSetPartOpen` takes for it.", "Open number"),
        F("linked", "int", "Part: the part removed with it.", "Linked part"),
        F("sides", "hex", "Part: byte `+0` of its record, a set of side bits (inferred)."),
        F("rgba", "list", "Colour: the four numbers `CarSetColor` is given.", "Values"),
        F(
            "calls",
            "int",
            "Script calls: `CarSpawn` for a type, `CarRemovePart` for a part, `CarSetColor` for a colour.",
            "Calls",
        ),
        F("scripts", "list", "The scripts that make those calls (at most six).", "Scripts"),
        F("meaning", "str", "What it is, in our words.", "What", curated=True),
    ),
    group_by="kind",
    nav="Cars",
    images=True,
)

#: Every topic, in index order.
TOPICS: tuple[Topic, ...] = (
    CHARACTERS,
    CHARACTER_MODELS,
    GANGS,
    GOAL_TYPES,
    SPEED_CLASSES,
    OBJECTS,
    OBJECT_GROUPS,
    CARS,
    PARTICLES,
    LEVELS,
    LEVEL_STARTS,
    WORLD_FLAGS,
    OBJECT_ZONES,
    VOLUME_BOXES,
    SCENES,
    *refs_env.TOPICS,
    CAMERAS,
    SCREEN_EFFECTS,
    ANIMATIONS,
    ANIM_IDS,
    CONTROLS,
    COMMANDS,
    HUD_COLOURS,
    RADAR_ICONS,
    TEXT_FORMATTING,
    TEXT_LABELS,
    ENUMS,
    SOUND,
    SPEECH,
    SCRIPT_EVENTS,
    WAD_NAMES,
)


def topic(key: str) -> Topic:
    """The topic whose file stem is `key`. Raises KeyError when there is none."""
    for candidate in TOPICS:
        if candidate.key == key:
            return candidate
    raise KeyError(key)
