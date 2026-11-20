# Missions

A mission is a level plus a small Lua table that says what the thief must do there. The demo's
is `DemoProject/Assets/Scripts/Missions/Manor.lua` on `Scenes/Stealth.pscene`. Everything is
gameplay script; the engine only keeps values across scene loads (`game.get` / `game.set`).

## Flow

1. **Menu** (`Scenes/Menu.pscene`, `Scripts/Menu.lua`, the project's start scene): the briefing, the
   choice of difficulty (1/2/3 or up/down) and the objectives it brings. Enter stores the difficulty
   with `game.set("difficulty", ...)` and loads the mission's scene.
2. **Mission** (`Scripts/MissionControl.lua` on an entity of the level): starts the mission at
   `game.get("difficulty", "Normal")`, shows the loot (top right) and the objectives (for a few seconds
   at the start, then on Tab), and counts what happens: knockouts, kills (finishing off someone knocked
   out turns the knockout into a kill), alarms raised and bodies found.
3. **The way out** (`Scripts/Goal.lua`): reaching it completes the mission once every other objective
   is done; otherwise the thief is told what is missing.
4. **Summary**: time, loot (and how many of the level's pieces), pickpockets, knockouts, kills, alarms,
   bodies found and the objectives. Enter goes back to the menu after a success, or tries again after a
   failure. A caught thief starts over with the same difficulty (it lives in the game values).

## Definition

```lua
return {
    name = "The Ashcombe Manor",
    scene = "Scenes/Stealth.pscene",        -- loaded by the menu, reloaded to try again
    briefing = { "line", "line" },
    objectives = {
        { loot = { Normal = 300, Hard = 500, Expert = 700 } },              -- "Steal loot worth at least N"
        { item = "CellarKey", text = "...", difficulty = { "Hard", "Expert" } }, -- take a particular item
        { no_kill = { "Cook", "Servant" }, text = "...", difficulty = { "Hard" } }, -- by entity name
        { no_kill = "all", text = "Do not kill anyone", difficulty = { "Expert" } },
        { escape = true, text = "Escape through the gate" },
    },
}
```

Objectives without `difficulty` apply to all. Killing someone a `no_kill` objective protects fails the
mission at once.

## Loot

`Scripts/Loot.lua` on anything worth stealing, with `label`, `value`, `item` (an id for item
objectives) and `pickpocket`:

* On a table or a shelf it needs a collider: F (use) takes it when it is under the crosshair.
* On a belt (`pickpocket = true`, a child of the NPC, no collider) F takes it from **behind** an NPC who is
  calm (Unaware or merely Suspicious), or from anyone lying down. A guard who is investigating,
  searching or alarmed feels the hand: a Seen stimulus of strength 9.5 ("Theft") at the thief.

Scripts reach the mission through `require("Scripts/Mission/Mission")`: `Mission.current` (state,
loot, items, stats, objectives), `Mission.take(value, item, how)`, `Mission.try_escape()`,
`Mission.fail(reason)`, `Mission.over()`, `Mission.summary(mission)`, `Mission.has(item)` (a key taken
opens its own door: `Scripts/World/Door.lua`, property `key`).
