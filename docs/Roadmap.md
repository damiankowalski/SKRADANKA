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

**In-game inspector** (asked for after playing the fog of war): with the F4 panel up, a click on a guard, a lamp,
a door or anything else selects the whole object (a guard, not his helmet; outlined in yellow) and an Inspector
window edits its components live, the ones that matter for its kind first (a guard's sight and hearing, a lamp's
light, a door's script), the rest folded, its parts below. A right click opens its menu: knock out, kill, calm
down, this guard ignores the thief, only his view cone, light off/on, move here, copy values (JSON for the level),
delete. The field widgets moved from the editor into the engine (`ImGui/ComponentInspector`) and serve both. Left
click selects by default; test noises take the panel's other mode or Shift+click. Edits are not saved.

**Mood lab** (S, approved: material for choosing the lighting, direction B: stylised realism, dark places, thick
fog, warm fire, cold astral lamps): the `MoodLab` scene (the cloister's stone, no guards, the thief, torches, fire
baskets and astral lamps in a warm east and a cold west zone), its variants in one Lua table
(`Scripts/MoodLab/Variants.lua`: base, fire, astral, mixed) and `docs/art/mood/MoodLab.jsonl`, which renders each
in a wide and a close shot (8 PNGs, byte-identical on rerun; CTest `Automation.MoodLab`). No engine change. The
results and the engine's gaps (no light in the fog, no local fog) are in docs/art/mood/README.md. The owner chose
variant C (mixed: warm fire and cold astral zones apart in thick fog) as the lighting direction.

**Volumetric fog** (L, approved after the mood lab: the look that makes thick fog read as fog): a froxel grid over
the view (160 x 90 x 64 out to 64 m, compute) lit by every lamp with a phase function and shadows where the lamp
has a shadow map (one tap, jittered, blended with the last frame), then integrated along every view ray and read
by the mesh and sky shaders. Height fog (`FogHeight`, `FogHeightFalloff`) and local fog boxes (`FogVolume`) shape
it; `VolumetricFog`, `FogScattering`, `FogAnisotropy` light it. The density lives in `Scene/FogUtils.h` and guards
see through the same fog (`PerceptionSystem::OpticalDepth`). About 0.45 ms of GPU on an M1 Pro. F4 tunes it live;
the mood lab renders v2 with it. See docs/Fog.md. Out of scope: guards seeing the glow (a thief against lit fog),
lit smoke volumes, more shadow slots for lamps.

**AssetGen: local 2D textures** (M, the owner's brief, Mac only): `Tools/AssetGen` generates tileable PBR
materials with FLUX.2 klein 4B (Apache-2.0) through mflux on Apple Silicon. The model is pinned to a revision,
the dependencies to `uv.lock`, in `Tools/AssetGen/.venv`, the weights in `HF_HOME` and never in the repository;
the 9B model (non-commercial) is refused. It tiles at generation (four rolled copies of the latent blended on
every step, a wrapped VAE decode), with a masking fallback. A seam test checks the edges, the middle and faint
lines. Normal map and roughness come from the albedo's brightness, and a provenance `.json` sits beside every
material. `setup.sh` checks, installs and removes it all (`uninstall --dry-run`); the
`penumbra-generate-texture` skill tells agents the rules. Demo: three stone walls (`docs/art/textures/v1`). No
engine change and no textures in the game yet: UV scale in the engine and the mood lab with textures are
proposals in the PR. See Tools/AssetGen/README.md.

**Texture tiling** (S, approved after AssetGen): `Material.UVScale` repeats a material's textures across the mesh UVs
and `Material.UVOffset` shifts them (albedo, normal, metallic/roughness and emissive alike; draw constants grow to 112
bytes). A 4 x 2 m wall of the built-in cube takes `UVScale` 4, 2 for a texture a metre across. Editable in the
editor and F4 (two-number fields), from Lua and the command API. Out of scope: world-space or triplanar mapping, so
the narrow sides of a stretched cube get the same repeats as its face.

**Mood lab with textures** (S, approved after texture tiling): the mood lab's walls wear two of the generated
stone walls (AssetGen variants 1000, light, on the garden walls and 1002, dark, on the outer walls and wings), in
`DemoProject/Assets/Textures/StoneWall` with their provenance `.json`; one tile covers 2 m of wall, the albedo
tinted x2 towards the flat greys it replaces. Renders v3 in docs/art/mood/README.md. The playable cloister keeps
its flat greys. Engine fix: the built-in cube's -Z and +X sides mapped textures turned by 90 degrees; all four
sides now stand upright (u to the right, v down).

**Mood lab cobblestone** (S, approved after the textured mood lab): a second AssetGen template, `cobblestone`
(floors seen from above, three variations); variant 1001 (dark granite setts; 1000 came out as brick-like rows and
1002 repeats its big stones visibly) paves the mood lab's corridors and wings, one tile per 1.5 m, in
`DemoProject/Assets/Textures/Cobblestone` with its provenance. Renders v3 redone. No engine change.

**Texture catalog** (S, approved after the cobblestone): `docs/art/textures/catalog/` keeps every generated
variant worth keeping, chosen or not, as its provenance `.json` with a note and a 512 px 2x2 preview (about
0.25 MB each); the full maps stay out of git and come back with `generate.py --from <json>` (byte-identical on the
same Mac; refuses another model revision or mflux version). `Tools/AssetGen/catalog.py` adds variants and writes
the index, which says which ones the game uses. Six so far: three stone walls, three cobblestones.

**Leaning on Q/E** (S-M, approved after the texture catalog): holding Q or E leans the thief left or right round a
corner, standing or crouching still (a movement key ends it): the eye he sees with (`FogOfWar.Lean`, the one engine
change), the camera, the model (tilted from the feet) and the silhouette guards see all move up to 0.45 m
sideways (less crouched); a sphere swept from his head stops it at a wall. The use key moved from E to F and the
blackjack from F to V (prompts, the lockpick's give-up, the F1 panel, docs).

**Mood lab roofs** (S, approved after leaning): two AssetGen templates, `roof_tiles` (roofs from above) and
`wood_ceiling` (ceilings from below); nine variants, all in the catalog with notes (four fail the seam test and say
so). Each roof slab of the mood lab is two layers: dark wooden shingles on top (`roof_tiles` 1003; a first slate read
as the walls' stone and was dropped), smoke-darkened planks with beams below (`wood_ceiling` 1002), the ceiling the
thief sees from the corridors. The renders add a roof shot under a test lamp. `docs/art/World.md` anchors the world
(medieval, rare practical magic, nature taking back abandoned places, early steam machinery as accents) and what
each surface is made of (roofs are roofing, never the walls' stone). No engine change.

**Perception: guards see the glow** (M, approved 2026-11-20): guards see the fog the player sees, glow included.
Contrast and threshold use the luminances the eye gets through the fog (`L_p' = L_p T1 + S1`, `L_b' = (L_b T2 + S2) T1
+ S1`): a thief against lamp-lit fog is a dark, readable silhouette, bright fog between a guard and the thief hides
him, dark fog changes little, no fog changes nothing (and costs nothing). The glow is integrated on the CPU
(`FogMedium::Integrate`, the phase and the unshadowed lamps' share shared with the shaders; the shader refactor
leaves the picture byte-identical) and a GPU test compares it with the froxel grid read back (2-5% where lit). Smoke
still only dims. F4 shows the new values and switches the model off to compare; `ai.visibility` returns them.
PerceptionLab has two playtest spots. 10 guards cost 0.035 ms a frame on an M1 Pro. Measured in the foggy levels:
PhysicsLab's guards now see the thief against its torch-lit haze at four of six spots where they did not before
(the owner decides whether to tune it). See docs/Perception.md, "Fog".

**PhysicsLab at night, its fog tuned** (S, the owner's decisions after the fog glow): the lab is lit like the other
levels (ambient 0.006, moon 0.08, sky map 0.12; it was about three times brighter, with no sense of darkness) and its
`FogScattering` is 0.25 (was the default 0.5). With the glow, the haze its torches lit gave the thief away within a
second or two at four of five places where its guards could not see him before; the owner chose a middle way: at
0.25 they make him out against it after 2.9-7.1 s (measured 0.5 / 0.3 / 0.25 / 0.22 / 0.15 with the fog glow
report). A level test checks one of the places.

**Perception: a lit body's own shading** (M, approved after the PhysicsLab playtest): a figure lit from the side
shows a bright and a dark side and is made out by it even against a background as bright as he is. Per visible
point, the body's two visible halves (a Lambert sphere, tabulated once; lights add as vectors, ambient lifts both)
give an own contrast, weighted by `Perception.SelfShading` (0.5) and a detail factor that fades it with distance;
each point counts the larger of it and the contrast with the background. Through the fog like everything else. A
side-lit thief against an equally bright wall: 0.8 s instead of 2.0 s; front-lit or dark: unchanged. In the demo
levels it changes little (report test). F4 and `ai.visibility` show it. See docs/Perception.md, "Self-shading".

**Agent playtester, package A: the bot, series and the report** (M, approved 2026-11-22): a deterministic bot plays
the thief along designed routes (walk, crouch, run, lean, wait, wait for a guard to walk off, take loot, put lamps
out, open and pick doors, reach the exit) through the same code as the keyboard: `Player.lua` reads every key and
mouse move through `self.controls`, the keyboard or the bot (a test checks the two give the same frame-by-frame
trace). `Tools/Playtest/playtest.py` plays many seeds headless in parallel and writes JSONL per run (state changes
with causes, reaction times at the threshold, footsteps heard, 10 Hz samples with `ai.visibility`, per-step margins)
and a Markdown report: success rate, who noticed the thief first, where and why, the narrowest margins,
luck-sensitive routes. The bot's timing jitter (0-2 s start, ±20% waits) has its own generator. `--playtest-out` and
Lua `playtest.record` are the only engine additions. Routes for PerceptionLab (the tool check: lit before the sentry
always detected, dark behind him never) and Stealth (baseline, the cellar, the hall dark and lit). 80 Stealth runs in
25 s on an M1 Pro. See docs/Playtest.md and the `penumbra-playtest` skill.

**Controls and the player's view** (S-M, approved 2026-11-23 after the owner's playtest of the playtester PR): a
small crosshair always at the screen centre (warm when F would do something); C toggles crouching (again to stand,
setting off at a run stands him up; `crouch_toggle` brings the held key back). The fog of war takes the camera's
frame as the view (`FogOfWar.ConeFromCamera`: no dark edges at the screen's sides; walls and corners still hide),
its eye follows a crouch at the camera's pace instead of in one frame (`EyeSpeed`), and the floor beyond something
lower than his eyes counts as seen (`SeeOverHeight`, up to the eye; crouched behind a crate it still is not). The
playtester's Stealth series is byte-identical before and after: guards and the thief behave as before. See
docs/FogOfWar.md.

**Lightning that lights only what it reaches** (S, approved 2026-11-23): the renderer has one directional shadow
map, and which light got it depended on the order of the level's entities. In Stealth the flash happened to get
it (the moon then shone through the roofs during each flash, faintly); in the other order the flash itself would
light every interior. Now the brightest directional light with `CastShadows` gets it
(`Scene::GetShadowedDirectionalLight`) and the other shadow casters are left out meanwhile, in the picture and
in perception alike: a flash lights the yard and what it reaches through doorways and windows, never a roofed room
(a GPU test in both orders, a perception test). The F4 panel's weather section strikes lightning, starts and ends
storms, and holds a flash up to 5 s (`WorldEvents.flash_hold`). The flash's white air outdoors is the volumetric
fog it lights (intensity 3 against the moon's 0.08), not light through walls.

**Lamps never light through walls** (M, approved 2026-11-23 after playing the lightning build: the room by the
Stealth spawn glowed from a torch behind its wall). Guards always saw walls block lamps (a ray per light) but the
renderer drew a lamp without a shadow map straight through walls: lamps with `CastShadows` off, and every lamp past
the 4 closest to the camera. Now the shadow maps (8 point, up from 4, and 8 spot) go to the lamps whose range
reaches the view, nearest first (`Renderer/ShadowSlots.h`); a lamp in view with no map left is left out of the
picture instead of shining through walls (F3 counts them). Every torch and hall lamp in Stealth casts shadows. A
GPU test puts a torch behind a room's wall with more nearer lamps than maps (the old code lit the floor to 0.76).

**Developer tools' UI** (M-L, approved 2026-11-23; brief `docs/research/DevToolsUI.md` approved 2026-11-24, before and
after pictures there): four PRs. **A. Sharpness and foundations** (done, #43): Retina framebuffer and per-monitor DPI, Roboto + Cousine
fonts, a render scale for the 3D picture, one UI size for the editor and runtime, layouts saved per user;
`--window-screenshot` saves the window with its UI. **B. The editor's layout** (done, #44): a default docked
layout on the first start and View → Reset layout (`ImGui/DockLayout.h`), the toolbar and frame time along the
top of the viewport, Console and Statistics as tabs. **C. The editor's panels** (done, #45): Properties in
registry order with labels, units and groups (declared in `VisitFields`), right-click menus (fields, components,
Hierarchy, Content, Console), Hierarchy badges, search and rename, Console filters. **D. The runtime's tools**
(#46): F4 in seven tabs with the inspector among them, F3 at the top centre, both clear
of the HUD and of each other (`Debug/ToolsLayout.h`), F4 remembers its place and size.

**Suspicion tempo near the threshold** (S, variant A approved 2026-11-25): suspicion rises by
`r_rise·(B − 1 + SuspicionFloor)` while the guard registers the thief (`Perception.SuspicionFloor`, 0.25), so a thief
barely made out is reacted to within seconds instead of half a minute (B 1.05: a mutter after 1.7 s instead of 10, the
alarm after 5 s instead of 30); a clearly seen one changes little (B 5: 0.35 s instead of 0.38). Playtester routes for
PhysicsLab (`Assets/Playtest/PhysicsLab`: the thief still against the torch-lit haze): the guard mutters after
1.4 s instead of 2.6-3.6 s and raises the alarm after 2.9 s instead of 4.8-5.2 s; crouched he still goes unnoticed. The
fog places of the earlier tuning: 1.2-4.2 s instead of 2.9-6.9 s. Stealth: the cellar route's two lucky escapes of 20
are gone, the baseline escapes 3 of 20 instead of 4. See docs/Perception.md, "Suspicion tempo".

**F1 Fog-free interiors** (S-M, brief approved 2026-11-26): `FogVolume.Mode` `Clear` keeps the level's fog out of a
room, fading in over `EdgeFade` at its sides, so in a doorway the fog creeps a metre in; fog from `Add` volumes stays
inside (a room can keep its own haze). One density for the screen and the guards (`FogMedium::ClearAt`,
`shaders/Fog.hlsl`), a GPU test compares the froxels with it. The Stealth hall and cellar have one each; their lit
hall is no longer full of glowing haze. Stealth playtest (20 seeds): the same outcomes on every route; a servant in
the lit hall makes the thief out a little sooner (B 5.6 instead of 3.5 in one seed). See docs/Fog.md, "Laying out an
interior".

**Materials for the first mission** (S, brief approved 2026-11-26): five new AssetGen templates (`planks`,
`plaster`, `stone_floor`, `ground`, `metal`) and their 20 variants in the texture catalog with notes (#49); the owner
chose nine for the game, now in `DemoProject/Assets/Textures` with their provenance: oak and tarred planks, lime plaster
and half-timbering, rusted iron, thatch (`roof_tiles_3001`, the first thatch that tiles), grey and wet flagstones,
beaten earth. The mood lab shows them on a sample wall south of the cloister (docs/art/mood/README.md, "Sample wall").
Fired clay tiles and brass still fail the seam test; brick comes later. No level is textured yet.

**The first mission's blockout** (2026-11-27): the whole route in grey boxes, from the tavern through the old town,
the church and its catacombs, a collapsed tunnel under the city wall, into the mayor's vineyard and his two-storey
manor, and back; playable, walked unseen by the playtester in 20 of 20 seeds (`Scenes/Mission1.pscene`, the menu's
second mission; [`research/MissionOneBlockout.md`](research/MissionOneBlockout.md): map, counts, frame times, what it
taught). On the way: doors fixed for storeys, half-turned leaves, passers-by and the household's own noise, the
alarm bell's reach, the exit's `remind`, the playtester at doors, street lanterns on arms with a lamp-clearance test.

**No lamp goes dark** (2026-11-27, after the owner's playtest of the blockout): 32 point shadow maps kept between
frames instead of 8 drawn every frame (a lamp's map is drawn again only when something in its reach moves), so a
street of lanterns is lit and the picture and the light gem agree under every lamp (a test with sixteen lamps).

## In progress

**AudioGen, package A: effects and ambience with a listening room** (M, branch `feature/audiogen`, the owner's
brief [`research/AudioGenBrief.md`](research/AudioGenBrief.md)): `Tools/AudioGen` generates sound effects and
ambience loops on the Mac with Stable Audio 3 Small SFX (on the CPU: a 10 s clip in 3.9 s, 60 s in 8.8 s, 4.5 GB
at most), checks them (BS.1770 loudness, true peak, leading silence, mono for 3D effects, loop seams; a broken
file is rejected), plays a series in a local listening room where the owner accepts, rejects or asks for a change
with a note, and promotes accepted sounds to `Assets/Audio/Generated` with their provenance. `Scenes/Listening.pscene`
plays them in place: a strip of six floors, doors at 3, 10 and 25 m, the night's wind in fog. The first series
(footsteps on stone and wood, a door, a 60 s wind loop, 16 variants) waits for the owner's ears; the cycle and
wiring the accepted sounds into the game finish the package. See `Tools/AudioGen/README.md`.

**The prop pipeline, part 1: PropGen and the first families** (M, branch `feature/propgen`, after AudioGen):
`Tools/PropGen` builds props from one script per family (Python's standard library, no Blender needed yet):
boxes and lathes in metres with the pivot at the base, UVs on the generated board and iron sheets, three authored
LODs, collision boxes, provenance, checks (winding, pivot, budget, LODs, textures, staleness). 27 props in ten
families: crates, barrels, chests, tables, chairs, stools, benches, shelves, wine racks, plank doors (60-1,032
triangles), shown in `Scenes/PropLab.pscene`. The engine now loads
a texture a `.glb` names by file once for every model using it. Next: beds and cloth, clutter, the doors on
the blockout's doorways, the architecture kit; Blender for bevels, booleans and cleaning AI or CC0 meshes, with the official MCP for live sessions.

## Known bugs

**Player character jitters** (reported in a playtest after PR #22, not reproduced yet): after a while of
play the thief starts to shake. It began after several gas arrows, carrying unconscious guards' bodies a few
times and walking over them. Trigger not narrowed down; the owner will narrow it before we fix it. Suspects
to check then: the character controller fighting ragdoll bodies it stands on or pushes, and the camera
following a body the character collides with.

## Next

**The asset library before dressing the map** (the owner, 2026-11-27): materials, 3D props, characters, sound
and music generated into the repository's catalogs first; the mission's places are built from them afterwards.

### The goal: the first mission (the owner, 2026-11-26)

One playable mission on one map, with full graphics, 2D and 3D art and sound: from a street and a tavern, through
the city's lantern-lit streets, the church, its catacombs and a collapsed tunnel, into the mayor's vineyard and manor
(kitchen, bathrooms, a drawing room with a fireplace, two staircases, the bedroom and the library upstairs). The
thief leaves hidden in the cart of the feast's leftovers, which a guard pushes out of the grounds when the kitchen
bell rings (Manuel the head cook's note tells how), or back through the vineyard when no guard is left. What it
needs, the owner's decisions and the order of packages: [`research/MissionOne.md`](research/MissionOne.md). Still
open: the main loot, the lord's title, and Blender MCP beside the scripts (decided when the prop pipeline starts).

**Sound comes once the mission's world stands**: AudioGen package A (effects and ambience with a listening room) is
approved with the owner's brief, [`research/AudioGenBrief.md`](research/AudioGenBrief.md); B (music) and C (voices)
follow it.

### Priority order (redone for the first mission, 2026-11-26)

Each package still needs its own brief and the owner's approval before it starts. The first mission comes first
(`research/MissionOne.md`, "Order"); the earlier list follows it.

1. **The asset library** (the owner, 2026-11-27: before the places): materials batch 2, the prop pipeline (3 below),
   the characters (4 below), AudioGen A and B (7 below), each into its catalog.
2. **The engine for a big map** (about 6 PRs, M-L in all), starting with the stress-test district: instancing, light
   culling, LOD and draw distance, room-bounded lights, alpha-tested materials, texture compression, collision
   proxies.
3. **The prop pipeline** (M): Blender (installed then, with the owner's approval), `Tools/PropGen`, its skill, a prop
   lab, the architecture kit; Blender MCP beside the scripts decided then.
4. **The characters' decision** (KayKit → Quaternius, M, art pipeline below; a mock-up first, waiting for the
   owner's approval of the download), then **Pixal3D PoC** (M, hero props). **Kimodo PoC** (M) after the
   characters: Kimodo retargets onto our skeleton, so the skeleton is settled first.
5. **The mission's places**, one PR each (M): street and tavern; church and catacombs; vineyard; the manor's ground
   floor; its first floor. Materials batch 2 and emissive windows come with them.
6. **The mission's gameplay**: the leftovers cart (M), readables, keys, the tunnel, checkpoints (S-M), townsfolk.
7. **AudioGen** (M, local AI tools below), with Whisper for subtitles: package A's brief is approved
   (`research/AudioGenBrief.md`); it starts once the mission's world stands. Then B (music) and C (voices).
8. **Polish**: the decal system (M-L: dirt at the foot of walls, moss, soot), playtester series, the frame budget
   checked on Windows. **Agent playtester, package B: maps from above** (S-M: the runs drawn on a top-down picture
   of the level, engine screenshots of the key moments) comes here, or earlier if the blockout's playtests need it.

After the first mission: the rest of the art pipeline (the full-screen map and its picture, concept art, img2img
variations, the other AssetGen add-ons); **F2 / F3** drifting and reacting mist; movement and UI (below): rope
arrows, climbing, swimming, the scouting orb, map and compass, inventory.

The player character's jitter (Known bugs) waits for the owner to narrow it down.

**Later, discussed**: a full-screen map on a key (S as a static picture, M drawn from the level; the picture can
come from AssetGen, see below).

### Later, discussed: fog for interiors, cellars and horror

Each package needs the owner's approval before its brief. All three change `FogMedium::DensityAt` and the shaders
together, so guards see what the player sees without touching perception.

* **F1 Fog-free interiors**: done (above).
* **F2 Drifting ground mist** (S-M): 3D noise and wind in the density (Environment/FogVolume: scale, strength,
  speed), deterministic from game time, the same on CPU and GPU; about +0.1-0.2 ms.
* **F3 Mist that reacts** (M): the thief's and NPCs' steps part the mist and leave a fading trail, an opened door
  gives a gust; a bounded number of disturbances in `FogMedium` (guards see the same); about +0.05-0.15 ms; soft
  motion (froxel resolution, temporal smoothing).
* F2 and F3 are research first (the owner, 2026-11-25): whether mechanics or animation make the fog more than static.
  The Stealth cellar's smudge on the wall goes with them; until then the fog stays as it is.
* Rejected: a full fluid simulation (L+, beyond the frame budget).

### Later, discussed: art pipeline (AssetGen)

Each item needs the owner's approval before its brief. Generation runs on the Mac (`Tools/AssetGen`); results keep
their provenance and go to the texture catalog; materials follow `docs/art/World.md`.

* **More materials** (S per material): brick, fired clay roof tiles and brass (both fail the seam test so far).
  The first mission's set is done (above).
* **The full-screen map's picture** (S): one generated image for the map item above.
* **Concept art of locations** before their levels are built (S).
* **Material variations through img2img** (S): wet, mossy and dirty versions of a material, against the repetition
  of one tile. Needs mflux's image-to-image path with the pinned model.
* **Emissive textures** (S), e.g. lit windows; `Material.EmissiveTexture` already exists.
* **Better height maps** (S): normal maps from depth instead of the albedo's brightness, with Depth Anything V2 Small
  (Apache-2.0; it replaces DepthPro here), one of the AssetGen add-ons under "Local AI tools" below.
* **Inventory icons with background removal** (S-M): background removal with BiRefNet (MIT), one of the AssetGen
  add-ons under "Local AI tools" below.
* **A decal system in the engine** (M-L): stains, cracks, moss laid on any surface. Renderer (a decal pass or
  deferred decals in the forward renderer), Scene (a component), editor placement.
* **KayKit characters replaced by Quaternius Universal Base Characters and the Universal Animation Library** (M,
  CC0; to be decided): KayKit's chibi proportions do not fit direction B. Areas: `Tools/ImportCharacters.py`, the
  animation clip names the scripts use, silhouette points and the perception tests tuned to the current models.
* Rejected for now: generating character textures on their unwrapped UVs (L, poor quality).

### Later, discussed: local AI tools

Each package needs the owner's approval before its brief. The tools, their licences and the house rules (one model
at a time, weights in `~/AI/huggingface`, uv per tool, pinned versions, provenance beside every result, approval
before any install) are in [`research/AITools.md`](research/AITools.md).

* **Agent playtester**: package A done (docs/Playtest.md); package B in "Next" above.
* **AudioGen** (M): a tool on the AssetGen model. Package A (effects, ambience, a listening room) with Stable Audio
  3 Small SFX, brief approved (`research/AudioGenBrief.md`); B music (ACE-Step 1.5 against Stable Audio 3 Small
  Music); C voices (Chatterbox Multilingual + Whisper).
* **Kimodo PoC** (M): text to animation, retargeted in Blender onto our skeleton; whether it fits in 16 GB.
* **Pixal3D PoC** (M): photo to a 3D prop with PBR textures, cleaned in Blender.
* **AssetGen add-ons** (S each): Depth Anything V2 Small (height maps: the "better height maps" item above),
  BiRefNet (background removal: for the inventory icons above), Real-ESRGAN (upscaling); Whisper for subtitles.

### Later: movement and UI (~12+ sessions)

Rope arrows (#9), climbing and mantling (#26), swimming (#27), scouting orb (#28), map,
compass and readables (#30), inventory and shop (#29).
