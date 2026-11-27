# Agent playtester

A measuring tool, not a clever AI: a deterministic bot plays the thief along routes you design, once per seed, and a
report says what got through, where and why guards noticed him, how close the calls were and how much the result
depends on luck. Use it to check a level (can it be done unseen, where does it break, how long it takes) and to
measure a change to perception: the same series before and after.

## Running a series

From the repository root, with a release build (`Tools/Build.sh release` or `Tools\Build.bat release`):

```sh
python Tools/Playtest/playtest.py --scene Scenes/Stealth.pscene --seeds 20
python Tools/Playtest/playtest.py --scene Scenes/Stealth.pscene --routes Playtest/Stealth/DarkHall baseline --seeds 20
python Tools/Playtest/playtest.py --scene Scenes/PerceptionLab.pscene --routes Playtest/PerceptionLab
python Tools/Playtest/playtest.py report out/playtest/Stealth/20261122-2300    # the report again, from the runs
```

* `--routes`: route folders or modules under the project's `Assets`, or `baseline`. Default: `baseline` plus every
  route in `Playtest/<Level>`.
* `--seeds 20 --first-seed 1`, `--jobs N` (parallel processes, default up to 4), `--set difficulty=Hard` (more game
  values for every run), `--config release`, `--out <folder>`.
* Output: `out/playtest/<Level>/<date-time>/` with `report.md`, `series.json` (the command, platform, commit, wall
  clock) and `runs/<route>/seedNNN.jsonl`. `out/` is not committed.
* Speed on an M1 Pro (release, 4 jobs): 80 runs of the Stealth level (about a minute of game each) in 25 s.

Each run is one process:

```sh
PenumbraRuntime --project DemoProject --scene Scenes/Stealth.pscene --headless --no-render --fixed-timestep 0.0166667 \
    --seed 7 --set playtest=Playtest/Stealth/DarkHall,playtest_seed=7 --playtest-out run.jsonl
```

The game value `playtest` names the route; with it `Player.lua` takes its keys and mouse from the bot
(`Scripts/Playtest/Bot.lua`) instead of the keyboard. Nothing else changes: the thief moves, crouches, leans, makes
footsteps and shows his silhouette through the same code a person plays with, and a test checks that a scripted
run of keys gives the same frame-by-frame trace either way. `--playtest-out` gives the scripts' `playtest.record`
somewhere to write (without it nothing is written; game scripts still cannot touch files).

## Determinism and luck

The same build on the same machine, level, route and seed give the same JSONL, byte for byte (a test in CI). Mac and
Windows may differ (Jolt is not built cross-platform deterministic). Two kinds of randomness vary between seeds:

* **The game's**: `math.random` from the scene seed (guards' lines, storms and dogs, kitchen noises, lock pins) and
  `Scene::NextRandom` (whether a storm explains a noise away). Guards' search spots (`nav.random_point`) use a fixed
  generator, the same for every seed.
* **The player's timing**: patrols are deterministic, so the bot's own generator (seeded by `playtest_seed`, never
  touching `math.random`) delays the start by 0-2 s and varies waits by ±20% (route `jitter`, default 1; 0 turns it
  off). That is when a person sets off compared with the patrol.

A route that succeeds in 10-90% of seeds is **luck-sensitive**: a methodical stealth game should not depend on it.

## Routes

A route is a Lua module under `Assets/Playtest/<Level>/`:

```lua
return {
    name = "Dark hall",          -- shown in the report
    time_limit = 300,            -- seconds (default 300)
    jitter = 1,                  -- the player's timing spread (0: none)
    survive = false,             -- true: a lab scenario that succeeds by staying undetected to the end
    start = { at = vec3(1, 0.05, 1), yaw = 0 }, -- optional: place the thief first (lab scenarios)
    steps = {
        { to = vec3(-4, 0, -10), gait = "crouch", label = "east across the north yard" },
        { to = "WP_A2" },                                -- an entity's position
        { use = "HallSwitch", lit = false },             -- a lamp switch: until the lamps are out (lit = true: on)
        { use = "GoldGoblet" },                          -- loot: walk up, aim the camera at it, F until taken
        { wait = 4 },                                    -- seconds (varied by the jitter)
        { wait_for = "GuardB", away = 10, timeout = 60 },-- until the guard is farther than 10 m and walking off
        { lean = "left", hold = 2 },                     -- Q / E held
        { face = 90 },                                   -- turn the view to a yaw
        { goal = true, label = "to the gate" },          -- the level's exit (Goal.lua); the mission must be complete
    },
}
```

* `gait` (`crouch`, `walk`, `run`) holds from the step that sets it until another changes it.
* Between points the bot follows `nav.path` corner to corner and only walks while facing within 50° of where it goes
  (turning at most 540°/s). A closed door in the way is used (a locked one is picked, a letter every 0.3 s): he
  steps 1.1 m back from the leaf on his side and aims at its middle (the camera over his shoulder cannot be turned
  onto something closer to him than the shoulder). An open leaf is not on the navmesh: a route that has to pass the
  hinge side of a door it opened goes round the leaf with a step of its own.
* No progress for 4 s: it looks for a path again; a second time ends the run as `stuck`, a problem with the route
  or the tool, not with the level.
* `baseline` needs no file: crouched, the nearest loot first (by navmesh distance) until the mission's loot target
  is met, then the exit.

## What a run records (JSONL)

One JSON object per line, keys sorted, numbers rounded (positions to 1 cm, most values to 0.001):

| `type` | When | Fields |
|---|---|---|
| `run` | first | `route`, `route_module`, `seed`, `jitter`, `time_limit`, `difficulty`, `mission`, `steps`, `guards`, `start`, `version` |
| `event` | a guard's state changes | `t`, `guard`, `from`, `to`, `kind` (Seen, Heard, Reported, Touch...), `cause`, `strength`, `b`, `d`, `step`, `label`, `guard_at`, `thief_at`, `stimulus_at`, `distance`, `light` and `light_distance` (the nearest lamp), `gem`, `crouched`, and `reaction_time` on a rise caused by sight |
| `exposure` | a guard registered the thief (B >= 1) and it faded without any state change | `start`, `duration`, `max_b` |
| `noise` | every footstep or landing | `loudness`, `material`, `at`, `heard` (the level that reached each guard; > 0 heard) |
| `sample` | 10 times a second | the thief (`at`, `gem`, `light`, `crouched`, `lean`, `gait`) and every guard (`state`, `b`, `d`, `strength`, `distance`, `at`, and `vis`: `ai.visibility`'s contrast, threshold, size, peripheral, motion, atmosphere, fog transmittance and glow, self-contrast, visible points, luminances) |
| `segment` | a route step ends | `index`, `label`, `t0`, `t1`, per guard `max_b`, `max_d`, `max_strength`, `max_heard`, `peak_state` |
| `use`, `door`, `wait`, `down` | a use worked or gave up, a door was in the way, a `wait_for` timed out, a guard went down | |
| `result` | last | `outcome`, `reason`, `t`, `steps_done`, `steps`, `loot`, `alarmed`, `thief_at` |

B and D are the engine's own values (`Perception.Stimulus`, `Perception.Suspicion`), read every frame; `vis` is
computed again for the sample (`ai.visibility` has no side effects: a run without it, `playtest_detail=false`,
records the same events).

## Metrics

* **Outcome**: `escaped` (the mission complete or, without a mission, the exit reached), `undetected` (a `survive`
  route stood out), `detected` (a guard became Alarmed: by sight, a touch, a shout or a bell, or he caught the
  thief), `failed` (the mission failed), `timeout`, `incomplete` (at the exit with objectives missing, or the route
  ended), `stuck` and `error` (tool or route problems).
* **Detections** are grouped by who noticed the thief first in the run (the first rise in any guard's state that the
  thief caused: sight, touch, his footsteps, doors, picks; not kitchen pots, storms, shouts or bells), at which
  route step and next to which lamp, and who then raised the alarm.
* **Margins**, per route step and guard over all runs: visual `1 - max B` (below 0: he was registered), awareness
  `10 - max reaction strength` (Alarmed is 10; 10 means never noticed), heard (a footstep reached him). The narrowest
  are listed first; "closest calls" gives each successful run's highest B, when and from whom.
* **Reaction time at the threshold**: from the first frame a guard registers the thief with his suspicion at zero
  (B >= 1) to each state change caused by sight, grouped by B at that moment (1-2, 2-5, 5+) and state reached.
  Exposures are those episodes that faded without any reaction.
* **Spread**: success rate, the outcome per seed, time min / mean / max of the successful runs.

## Checking the tool

`Playtest/PerceptionLab/LitFront` (in the campfire's light before the sentry) must be `detected` in every seed and
`DarkBehind` (crouched in the dark behind him) `undetected` in every seed; anything else is a bug in the
playtester. Tests (`PenumbraTests --test-suite=Playtest`) check both, that the bot's controls drive the thief exactly
as the keyboard does, and that a run is reproducible.
