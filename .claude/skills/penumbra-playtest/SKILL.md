---
name: penumbra-playtest
description: Run the agent playtester on a Penumbra level (many seeds along designed routes) and read its report. Use to check whether a level can be done unseen and where it breaks, and before and after every change to perception, guards, lighting, fog or a level's layout, to measure what the change did.
---

# Playtesting a level with the agent playtester

Everything about the tool is in `docs/Playtest.md` (routes, JSONL, metrics). This is when and how to use it.

## When

* **Before and after every perception change** (contrast, thresholds, suspicion, reactions, fog, lighting math,
  hearing): run the same series on the same build configuration and compare the reports. Put both summaries in the PR.
* **A level was built or changed** (geometry, lamps, patrols, loot): run its routes; a new route for each new way
  through.
* **The owner asks whether a level works**: what gets through, where and why guards notice, what is luck-sensitive.

## How

1. Build release: `Tools/Build.sh release` (Mac) or `Tools\Build.bat release`.
2. Check the tool first when anything in perception or the bot changed:
   `python Tools/Playtest/playtest.py --scene Scenes/PerceptionLab.pscene --routes Playtest/PerceptionLab`
   LitFront must be detected 20/20 and DarkBehind undetected 20/20; otherwise fix the tool before reading anything else.
3. Run the level: `python Tools/Playtest/playtest.py --scene Scenes/<Level>.pscene --seeds 20` (baseline plus the
   routes in `DemoProject/Assets/Playtest/<Level>/`). Before/after comparisons: the same `--seeds`, `--first-seed`,
   routes and `--set`, recorded in each folder's `series.json`.
4. Read `out/playtest/<Level>/<date-time>/report.md`. `stuck`, `error` or `incomplete` runs are route or tool
   problems: fix the route (a waypoint off the navmesh, a use target out of reach) and run again.

## Reporting to the owner

In Polish, in the PR or the chat: what gets through (rate, time), where and why guards notice the thief (who first,
cause, step, nearest lamp, light gem, distance), what is luck-sensitive, the narrowest margins. Level changes are
proposals with an S/M/L estimate; do not make them unasked. The tool measures; it does not tune: perception,
guards and levels change only in their own approved packages.

## Rules

* Results compare only within one machine and build (Mac and Windows may differ). Name the platform and commit.
* Do not commit `out/`. Routes (`DemoProject/Assets/Playtest/`) are hand-written and committed.
* Full series do not run in CI; `PenumbraTests --test-suite=Playtest` covers determinism and the tool check.
