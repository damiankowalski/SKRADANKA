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

## In progress

**Developer tools' UI** (M-L, approved 2026-11-23; brief `docs/research/DevToolsUI.md` approved 2026-11-24): four
PRs. **A. Sharpness and foundations** (done, #43): Retina framebuffer and per-monitor DPI, Roboto + Cousine
fonts, a render scale for the 3D picture, one UI size for the editor and runtime, layouts saved per user;
`--window-screenshot` saves the window with its UI. **B. The editor's layout** (in progress): a default docked
layout on the first start and View → Reset layout (`ImGui/DockLayout.h`), the toolbar and frame time along the
top of the viewport, Console and Statistics as tabs. Then **C** the
editor's panels (Properties, right-click menus, Hierarchy search, Console), **D** the runtime's F3-F7 tools
(F4 in tabs, no overlaps, remembered layout).

## Known bugs

**Player character jitters** (reported in a playtest after PR #22, not reproduced yet): after a while of
play the thief starts to shake. It began after several gas arrows, carrying unconscious guards' bodies a few
times and walking over them. Trigger not narrowed down; the owner will narrow it before we fix it. Suspects
to check then: the character controller fighting ragdoll bodies it stands on or pushes, and the camera
following a body the character collides with.

## Next

**Agent playtester, package B: maps from above** (S-M, the next step of this item; needs its brief): the runs drawn
on a top-down picture of the level (routes, where guards noticed the thief, their view and lamps), and engine
screenshots of the key moments. Package A (the bot, series and report) is done.

### Priority order (approved 2026-11-22, updated 2026-11-23)

Each package still needs the owner's approval and its own brief before it starts. The owner's playtest of the
playtester added three packages in front (2026-11-23): controls and the player's view (done), the lightning
(done), then the developer tools' UI (above).

1. **Agent playtester** (package A done; package B above).
2. **Suspicion tempo near the threshold** (S): from the PhysicsLab playtest, a guard who barely makes the thief out
   (stimulus just above 1) takes seconds to react; how fast suspicion should rise there, measured with the
   playtester.
3. **F1 Fog-free interiors** (S-M, "fog for interiors" below): needed before the first mission's interiors.
4. **Materials for the first mission** (S each, art pipeline below), then **the decal system** (M-L): dirt at the
   foot of walls, moss, soot (`docs/art/World.md`).
5. **AudioGen** (M, local AI tools below), with Whisper for subtitles.
6. **The characters' decision** (KayKit → Quaternius, M, art pipeline below), then **Kimodo PoC** (M): Kimodo
   retargets onto our skeleton, so the skeleton is settled first.
7. **Pixal3D PoC** (M, props).
8. The rest of the art pipeline: the full-screen map and its picture, concept art, img2img variations, emissive
   textures, the other AssetGen add-ons.
9. **F2 / F3** drifting and reacting mist.
10. Movement and UI (below): rope arrows, climbing, swimming, the scouting orb, map and compass, inventory.

The player character's jitter (Known bugs) waits for the owner to narrow it down.

**Later, discussed**: a full-screen map on a key (S as a static picture, M drawn from the level; the picture can
come from AssetGen, see below).

### Later, discussed: fog for interiors, cellars and horror

Each package needs the owner's approval before its brief. All three change `FogMedium::DensityAt` and the shaders
together, so guards see what the player sees without touching perception.

* **F1 Fog-free interiors** (S-M): a volume that clears the fog (a FogVolume with negative density or a "clear"
  mode), in `FogMedium` and the shaders; costs about nothing. Today a foggy level's interiors are foggy too.
* **F2 Drifting ground mist** (S-M): 3D noise and wind in the density (Environment/FogVolume: scale, strength,
  speed), deterministic from game time, the same on CPU and GPU; about +0.1-0.2 ms.
* **F3 Mist that reacts** (M): the thief's and NPCs' steps part the mist and leave a fading trail, an opened door
  gives a gust; a bounded number of disturbances in `FogMedium` (guards see the same); about +0.05-0.15 ms; soft
  motion (froxel resolution, temporal smoothing).
* Rejected: a full fluid simulation (L+, beyond the frame budget).

### Later, discussed: art pipeline (AssetGen)

Each item needs the owner's approval before its brief. Generation runs on the Mac (`Tools/AssetGen`); results keep
their provenance and go to the texture catalog; materials follow `docs/art/World.md`.

* **Materials for the first mission** (S per material): planks, plaster, roof tiles (wooden shingles exist; fired
  clay and thatch are in the catalog but fail the seam test), rusty metal, mud.
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
* **AudioGen** (M): a tool on the AssetGen model for Stable Audio Open Small (effects, ambience), ACE-Step 1.5
  (music) and Chatterbox Multilingual (voices).
* **Kimodo PoC** (M): text to animation, retargeted in Blender onto our skeleton; whether it fits in 16 GB.
* **Pixal3D PoC** (M): photo to a 3D prop with PBR textures, cleaned in Blender.
* **AssetGen add-ons** (S each): Depth Anything V2 Small (height maps: the "better height maps" item above),
  BiRefNet (background removal: for the inventory icons above), Real-ESRGAN (upscaling); Whisper for subtitles.

### Later: movement and UI (~12+ sessions)

Rope arrows (#9), climbing and mantling (#26), swimming (#27), scouting orb (#28), map,
compass and readables (#30), inventory and shop (#29).
