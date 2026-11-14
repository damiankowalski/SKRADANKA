# Roadmap

Ideas approved for Penumbra, in the order we plan to build them. Each package lands as one PR. Sizes are
working sessions: S = under one, M = one or two, L = three or more. Mechanics are numbered as in
[`research/Thief.md`](research/Thief.md).

## Done

**A. Guards that think** (navigation + hearing + reactions): Recast/Detour navmesh and agents, 0-10
loudness scale, sound around corners along the navmesh, surface-dependent footsteps and landing noise,
noisy physical objects, reactions that last in proportion to the stimulus, search behaviour, alert memory,
the `AILab` scene and the noise probe. Covers #3, #4, #5, #15, #16 and #18. The noisemaker arrow (#10) was
left out on purpose.

**Area and world events**: the scene's random seed (`--seed`), event zones (the cook in the kitchen
drops a knife, bangs a pot), the storm (rain, lightning that lights the player and dazzles guards,
thunder that masks footsteps), dogs, and the competition between a distraction and a guard's reaction
(`ai.explain_away`, `Diligence`). Out of scope, decided together: rain visuals (needs a particle system,
M-L), realistic voices and howls (need CC0 sound packs), NPC daily schedules (L).

**C1. Takedowns, bodies and talking guards**: the use key (#22), blackjack (#13), sleeping gas (#11),
bodies found by other guards and carried away (#14), shouts that spread the news, barks with subtitles
and a servant who runs to the alarm bell (#17). Out of scope: real voices (need recordings), ragdolls (a
body is a death animation, carried rigidly on the shoulders; ragdolls would be a package of their own, L).

**Arrows that matter**: health and the `Damage` / `Alarm` stim actions (fire arrows hurt and kill; a
guard hit by any arrow searches where it came from), arrow impact noise by surface, arrows seen in flight
through the contrast model (a dark arrow against a bright background, a burning one in the dark), a
capped motion factor, and carried bodies resting across the shoulders.

**Touch beats shadow** (S, after playtesting arrows: a guard following an arrow's flight walked into the
player in the dark and did not notice): contact between a guard and the player alarms the guard whatever
the light, both when he bumps into the player and when the player bumps into him; a figure right in front
of a guard is made out in the dark, slowly enough to back off.

**C2. Mission loop**: game values kept across scene loads (`game.get/set`, `--set`), a title screen
with the briefing and the choice of difficulty, missions described as data (`Scripts/Missions/*.lua`,
docs/Missions.md) with loot, item and no-kill objectives per difficulty (#21), loot to take and purses
and a key to pickpocket from behind a calm guard (#23), the gate that only opens once the objectives are
done, and the end-of-mission summary (time, loot, pickpockets, knockouts, kills, alarms, bodies found).
Out of scope: a real UI toolkit (menus are text drawn by scripts), saving mid-mission, loot that moves
with the hips (purses ride on the NPC's root, not on a bone).

## In progress

**macOS as a development platform** (L): the game ships on Windows (Direct3D 12); a Mac (Apple Silicon)
is an equal place to develop it. A portable build (clang, POSIX ports of the few Windows-only pieces,
cross-platform build/test scripts) and a Vulkan renderer through NVRHI that runs on macOS via MoltenVK
(`--vulkan` selects it on Windows too, so both backends are tested on one machine). Shaders stay HLSL,
compiled by DXC to DXIL and SPIR-V.

## Next

### B. Thief's workshop (~5 sessions, after the macOS work)

Moss arrows (#8, the floor side is done), light switches (#24), doors that block sight
and sound and that guards open (#20, navmesh links), lockpicking (#19).

### Later: movement and UI (~12+ sessions)

Rope arrows (#9), climbing and mantling (#26), swimming (#27), leaning (#25), scouting orb (#28), map,
compass and readables (#30), inventory and shop (#29).
