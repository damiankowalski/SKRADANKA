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

**macOS as a development platform** (L): the game ships on Windows (Direct3D 12); a Mac (Apple Silicon) is
an equal place to develop it. A portable build (clang, POSIX ports of the few Windows-only pieces,
cross-platform build/test scripts) and a Vulkan renderer through NVRHI that runs on macOS via MoltenVK
(`--vulkan` selects it on Windows too, so both backends are tested on one machine). Shaders stay HLSL,
compiled by DXC to DXIL and SPIR-V. Polish after the first days on an M1 Pro: the Vulkan driver is chosen
deterministically (MoltenVK over KosmicKrisp, `PENUMBRA_VULKAN_DRIVER` to force another, the choice in the
log) and third-party code compiles without warnings on every compiler.

**B. Thief's workshop** (two PRs): light switches (#24): a wall switch puts its lamps out (lights,
glowing lanterns and what guards perceive), and a calm guard or servant who sees a dark lamp walks
over and lights it again. Moss arrows (#8, key 4): a patch of moss on a floor (footsteps -3), nothing
on a wall. Doors (#20): hinged, kinematic doors that block sight and light while closed and muffle
sound (`Surface.SoundLoss`, straight through and along the navmesh, which runs through doorways);
NPCs open them and shut them behind them. Locks and keys (#19), as the owner designed it: the right
key (a Mission item, the captain's cellar key) opens at once; otherwise a progress bar hangs at the
lock in the world (world-space HUD, `ui.world_rect` / `ui.world_text`, hidden by whatever stands in
front, the thief's own body included) over random W/A/S/D letters to press in order; a wrong key
slips one pin back; the thief stands still while picking, E gives up. The Stealth level's new cellar
has a locked door on GuardA's round. Out of scope: lockable chests (the lock works on any door-like
script, chests need their own lid), guards noticing doors left open.

**Test levels**: `Cloister` (a square garden ringed by covered corridors, a pair of doors into it from
each side, east and west wings; two guards walk the ring opposite ways; from the east wing to the west
as a mission) and `PhysicsLab` (a ramp up to a table with a copper jug: push the table down, or shoot
the jug off and let the guard hear it). Doors now open only for NPCs heading at them, not for those
walking past along a corridor.

**Playtest fixes** (from playing the cloister): the HUD is laid out for 1080 lines and scales up on taller
screens (`ui.scale()`: a Retina Mac draws it twice as large), an F1 controls panel (up for 15 s when a level
starts); the camera sweeps a sphere (`physics.spherecast`) to the shoulder and back along the boom, so it
no longer sees through walls; the crossbow (the main weapon from now on) aims with the right mouse button
(a dot at the centre) and shoots with the left, which stays free for another action when not aiming;
crossbow bolts with a shaft, a head and glowing fletchings; a compass strip (top right, `north_yaw` per
level, no position: no minimap, ever); guards see a 130 degree cone: a 90 degree focus plus 20 degree
bands each side where the thief's contrast counts half (`FocusFieldOfView`, `PeripheralContrast`).

## In progress

Nothing: the next package is NPC avoidance.

## Next

**NPCs give way** (M, Navigation): agents look about 1.5 m ahead and, seeing another NPC coming the other
way, step to their right before they meet (keep-right rule for people); a standing NPC is walked around
with clearance, then the agent rejoins its path. Today they walk into each other and stop.

**Ragdolls** (L, ~3 sessions; Animation, Physics, AI, `Player.lua`): Jolt ragdolls built from the
skeleton (capsules per main bone, swing-twist limits) that take over from the animation on a knockout or
death with the body's current pose and speed; bolts push them; bodies settle and sleep; guards see the
body by its bones (a body folded behind a crate is harder to spot); carried rigidly, a ragdoll again when
put down. Risks: the KayKit proportions (big head, short limbs), stability with light limbs, identical
results on Windows and macOS.

**Later, discussed**: a fog of war over what the thief cannot see (rendered from the character's eyes,
not the camera: no peeking round corners for free) together with leaning on Q/E, which needs the use key
moved off E first; a full-screen map on a key (S as a static picture, M drawn from the level).

### Later: movement and UI (~12+ sessions)

Rope arrows (#9), climbing and mantling (#26), swimming (#27), leaning (#25), scouting orb (#28), map,
compass and readables (#30), inventory and shop (#29).
