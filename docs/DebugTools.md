# Runtime testing tools

`PenumbraRuntime` carries a set of tools for playtesting and tuning, drawn with Dear ImGui over the game.
Development builds (debug, release) always have them; a shipping (Dist) build only with `--dev`. They need a
window (nothing is drawn headless).

| Key | Tool |
|---|---|
| F3 | performance overlay |
| F4 | debug panel and the inspector (frees the mouse; the game gets no input while it is up) |
| F5 | freelook: the camera leaves the thief |
| F6 | pause / resume the simulation |
| F7 | fog of war on / off (what the thief sees, or the whole level) |

`--debug-ui performance,panel,freelook` opens any of them at start. F1 (the game's controls panel) and F12
(screenshot) stay with the game. The panel's text follows the HUD's scale (1080 lines = 1, twice as large on a
Retina Mac), and its "UI size" slider scales it further (0.75 by default, 0.5 to 2).

## Performance overlay (F3)

Top left, refreshed twice a second over the frames since the last refresh:

* **FPS** and frame time (average, best, worst) with a graph of the last 240 frames; the blue line is 60 FPS.
* **CPU**: the process's CPU time over wall time, as a share of one core (100% = one core busy) and of every
  logical core; the time the simulation (`update`) and the CPU side of rendering (`render`) take per frame.
* **Systems**: what each step of `Scene::RunRuntimeFrame` costs (scripts, navigation, physics, contacts, stims,
  perception, smoke, audio, ragdolls, animation, fog of war, deferred destruction), averaged (`Scene::GetFrameProfile`).
* **GPU**: the time the GPU spends on a frame (timestamp queries around `SceneRenderer::Render`, read back a
  few frames later; "no timer" until the first arrives) and video memory: used of budget on D3D12 (DXGI) and on
  Vulkan with `VK_EXT_memory_budget` (MoltenVK has it); otherwise only the size of the GPU's memory.
* **RAM**: the process's resident memory (working set), its peak, and the machine's installed memory.
* Draw calls, triangles, lights, shadow views and culled objects of the last frame; entity and body counts.

## Debug panel (F4)

**Fog of war: what the thief sees** (`FogOfWar.md`): the switch (also F7; it lasts across scene loads and
overrides the level's FogOfWar component, without changing it), and the thief's FogOfWar settings live: whether
it is on for him and hides guards, the cone, its soft edge, the awareness circle, the view distance, and how
dark and grey the unseen part is. Values tuned here are not saved: copy them into the level. The count shows the
guards and bodies hidden right now.

**Guards: sight and hearing** draws, for every awake NPC with `Perception`:

* the **view fan**: its whole field of view (130 degrees by default) at eye height, every ray cut where a wall,
  a closed door or a crate stops the eye (characters do not). Bright: the focus cone (90 degrees); dim: the side
  bands where the thief's contrast counts for half. Colour = state: green unaware, yellow suspicious, orange
  investigating / searching, red alarmed. Light is not in the fan: whether he makes the thief out depends on the
  contrast (the label).
* **magenta lines** to the thief's silhouette points this guard has a line of sight to (bright in focus, pale
  in the bands);
* a **suspicion bar** over his head and a line to what he is reacting to;
* a **label**: state, reaction strength and time left with its cause, suspicion, and the thief as this guard
  sees him right now: stimulus `B` (>= 1 is seen), visible points, contrast against the threshold
  (`PerceptionSystem::ComputeVisibility`, docs/Perception.md).
* **noise rings** for every noise the game makes (footsteps, impacts, doors...; or only test noises): blue, how
  far it carries at all; orange, where it is still 4 loud (a guard walks over to look). Rings ignore walls; a
  test noise's report below does not.
* the navmesh with agents' paths, and colliders, on request.

**Guards ignore the thief** (`PerceptionSystem::SetIgnoreIntruders`): no sight, no touch, no hearing of noises
he makes (footsteps). Bodies, arrows and thrown objects still alarm them. It lasts across scene loads.

**Test noise**: with "left click in the world: makes the noise" (or with Shift held), a click on the level (through characters, onto the floor or
a wall) makes a noise of the chosen loudness there (`NoiseProbe::Emit`, cause `Debug`), heard like any other.
The panel lists every NPC who will hear it, at what level after distance, walls, doors, paths and ambient noise,
and the reaction that starts. The reference table (click a row to pick it) is the demo's sounds:

| L | example |
|---|---|
| 1 | sneaking on stone, a lock pin set, a moss arrow |
| 2 | a light switch, a key turning in a lock |
| 3 | walking, a twig, a door creaking open, a blackjack, a bowstring |
| 4 | a door falling shut |
| 5 | running on stone, a knife dropped on stone |
| 6 | a pot banged on the stove |
| 7 | a table knocked over, running on a metal grate |
| 8 | a shout |
| 8.5 | the copper jug knocked off the table onto wood |
| 9 | a metal bucket on cobbles |
| 10 | the alarm bell, an explosion |

The log under it lists the last noises the game made with their loudness and cause.

**Camera and time**: freelook and its speed, pause (F6) and single steps, a time scale (0.1x - 4x) for watching
a patrol or a search in slow motion or fast forward.

**Fog and volumetric light** (folded; `Fog.md`): the level's fog live, not saved: volumetric on or off, density,
its own glow, height and height falloff, scattering and forward glare. Guards see through the fog the sliders set.

## Inspector (F4)

With the panel up, a **left click** on anything in the level selects it (pick "left click in the world: makes the
noise" in the panel, or hold Shift, to make test noises instead). A click on a part selects the whole: a guard,
not his helmet; a torch, not its flame. The selection gets a yellow box. Guards the fog of war hides cannot be
clicked (F7 shows them). The **Inspector** window shows:

* its name, what kind of thing it is (NPC, thief, body, light, a scripted object named after its script such as
  Door, loose object, a prefab's name...) and the object it belongs to (click to go up);
* the components that matter for that kind first, open (a guard: Perception, Vitality, NavAgent, Script...; a
  lamp: its light and flame; a door: its script's properties), every other component folded below, and its
  parts (children) to select. Every field is editable and applies at once; runtime state (suspicion, alert
  level...) is shown read-only under each component.

Edits are **not saved**: "Copy values" puts the object's components on the clipboard as JSON (the scene file's
form) to carry good values into the level or `Tools/BuildDemoProject.py`. Some fields are read only when a system
starts (a collider's shape, a body's mass, a navigation agent's radius): changing them does nothing until the
level is loaded again.

A **right click** on an object (a click, not a freelook drag) selects it and opens its menu; the same actions sit
as buttons at the top of the inspector:

* a guard or servant: **Knock out**, **Kill**, **Calm down** (ends his reaction), **Ignores the thief** (this one
  NPC neither sees, touches nor hears him; `PerceptionSystem::SetIgnoresIntruders`), **Only his view cone** (the
  other fans are hidden);
* anything with lights on it or below it: **Light off / on** (the lights' `Enabled`, which guards see too);
* everything: **Move here...** (then left-click where it goes; Esc cancels: a character is teleported, a limp
  body moves whole), **Copy values**, **Delete**.

## Freelook (F5)

The camera detaches from the thief and starts where the game camera was. Hold the right mouse button to look,
WASD to move, Q/E down/up, Shift for four times the speed, the wheel to change the speed. The game goes on (pause
with F6); the thief does not move. The guards' overlays are drawn while it is on, so their fans and labels can be
followed from above. F5 again returns to the thief's camera. The fog of war stays the thief's: from freelook you see
what he sees (and what he does not, dark); F7 shows the whole level.

## For engine code

The pieces are engine code with tests (`PenumbraTests/src/DebugToolsTests.cpp`, suite `Debug`):
`Debug/PerformanceMonitor`, `Debug/FreeCamera` (with `ScreenRayDirection` / `WorldToScreen`), `Debug/NoiseProbe`,
`Debug/ObjectInspector` (picking, the kind of object and its key components, the menu's actions;
`InspectorTests.cpp`), `ImGui/ComponentInspector` (the field widgets, shared with the editor's Properties panel),
`Debug/DebugOverlays` (shared with the editor's "Perception debug" view), `Platform::GetProcessUsage`,
`RenderDevice::QueryVideoMemory`, `SceneRenderer::GetGpuTimeMs`, `Input::SetGameInputBlocked`. The ImGui panel is
`PenumbraRuntime/src/RuntimeDebugPanel`.
