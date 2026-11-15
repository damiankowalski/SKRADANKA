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
bands each side where the thief's contrast counts half (`FocusFieldOfView`, `PeripheralContrast`). NPCs
give way: an agent sees another NPC up to ~2 m ahead and steps to its right when they meet head on (both
do), goes round one who stands, and stops beside a waypoint another NPC already stands on (the two
cloister guards used to circle a shared corner for good).

**Ragdolls**: knocked out or killed, a guard or servant goes limp. Twelve capsules and a ball (hips,
spine, chest, head, arms, legs) built from the humanoid skeleton and sized to the rig, swing-twist joints
for the trunk, shoulders and hips, hinges for knees (backwards) and elbows (forwards); the fall starts
from the pose and speed he had, the body settles and sleeps, and his origin follows the hips along the
floor. Guards see a body by its parts (less of it behind a crate); a bolt shoves the part it strikes;
carried in the animation's pose, limp again when put down; `set_position` moves a limp body whole. The
PhysicsLab has a lookout on the ramp to knock out and shove down with bolts (an 18 degree slope is too
gentle for a body to roll by itself). Component `Ragdoll` (`Mass`, `RadiusScale`, `Friction`), Lua
`physics.ragdoll_start/stop/active/impulse`. Out of scope: ragdolls for the dogs (another skeleton),
per-bone tuning in the editor.

**Testing tools** (asked for before the fog of war, to test it): in the runtime, F3 shows performance (FPS and
frame times with a graph, CPU of the process and per simulation system, GPU time per frame, video and main
memory, draw statistics), F4 a debug panel (every guard's view fan cut by walls with focus and side bands, a
label with his state and how he sees the thief, rings around noises, test noises clicked into the level with a
loudness table of the game's sounds and a report of who hears them, guards ignoring the thief, pause, single
steps, time scale), F5 freelook (the camera leaves the thief and flies through the level), F6 pause. Dear ImGui
moved from the editor into the engine; shipping builds have the tools with `--dev`. See docs/DebugTools.md.

**Fog of war** (L): the thief sees only what his own eyes see, in the spirit of Darkwood. A view cone where the
camera looks (120 degrees, soft edge), a circle of awareness around him, out to 40 m and never through walls (an
eye depth cube rendered like a point light's shadow, tested per pixel); the rest is darkened and drained of
colour, and guards and bodies there are not drawn, though still heard. `FogOfWar` component on the player (in
every demo level), Lua `fog.*`, F7 and the F4 panel switch and tune it. See docs/FogOfWar.md. Leaning on Q/E
follows as its own package (it moves the eye; the use key must leave E first).

## In progress

Nothing: the next package waits for the owner's choice (leaning on Q/E is the natural follow-up).

## Known bugs

**Player character jitters** (reported in a playtest after PR #22, not reproduced yet): after a while of
play the thief starts to shake. It began after several gas arrows, carrying unconscious guards' bodies a few
times and walking over them. Trigger not narrowed down; the owner will narrow it before we fix it. Suspects
to check then: the character controller fighting ragdoll bodies it stands on or pushes, and the camera
following a body the character collides with.

## Next

**In-game inspector** (M, asked for after playing the fog of war): in the runtime's debug panel, click a guard,
a lamp, a door or any object to select it (a whole character, not one of his parts; outlined in the world) and
edit its components live, like the editor's Properties panel (which moves from the editor into the engine and
serves both). Contextual: the sections that matter for the kind of thing come first (a guard's sight, hearing
and AI state; a lamp's light; a door's lock), the rest folded. A right-click menu of actions on the selection
(knock out, kill, move to the cursor, ignore the thief, show only his view cone). Risks: the panel's left click
makes test noises today (selection needs its own button or mode); some fields are read only when a system
starts (collider shapes, a nav agent's speed) and need the object rebuilt to take effect; edits are not saved
to the scene (a "copy values" button carries them to the editor).

**Later, discussed**: leaning on Q/E (S-M, after the fog of war; the use key moves off E first); a
full-screen map on a key (S as a static picture, M drawn from the level).

### Later: movement and UI (~12+ sessions)

Rope arrows (#9), climbing and mantling (#26), swimming (#27), leaning (#25), scouting orb (#28), map,
compass and readables (#30), inventory and shop (#29).
