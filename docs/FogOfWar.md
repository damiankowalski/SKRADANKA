# Fog of war

The player sees only what the thief's own eyes see. A limited view makes the dark threatening and forces every
move to be planned: you look before you step round a corner, and a guard you heard but cannot see is somewhere
out there. Darkwood is the reference (a top-down 2D game). Penumbra's fog keeps that idea in a third-person
3D game.

## What the player sees

`FogOfWarComponent` on the player (`Components.md`; the demo levels' thief has the defaults):

* the **camera's frame** (`ConeFromCamera`, on by default): everything the picture shows counts as looked at.
  The cone starts at the primary camera and is as wide as the picture (from its vertical field of view and the
  screen's aspect), its edge fading out over `EdgeSoftness` degrees just outside it, so the screen has no dark
  edges; what is behind the camera is not seen. Turning the camera turns the view, as turning the head would. Only
  the horizontal angle counts: what is above or below him in the frame is seen. With `ConeFromCamera` off: a cone
  `ViewAngle` wide (120 degrees) around the eye, centred on where the camera looks.
* a **circle of awareness**, `AwarenessRadius` (1.8 m) around him in every direction: you sense what is right
  behind your back.
* out to `ViewDistance` (40 m);
* **never through walls**, which is what the fog is for: what a wall, a corner or a closed door hides from his eyes
  stays dark however it is framed. The eye is `EyeHeight` (1.65 m) above the character's origin and lower when his
  CharacterController crouches, following the crouch at `EyeSpeed` (12 per second, the pace of the player's
  camera) rather than in the frame the capsule changes: the view behind a crate darkens as he gets down. Floors,
  walls, closed doors and furniture block it. Characters, bodies, loose (dynamic) objects, triggers and bodies
  marked `TransparentToSight` (grates, glass) do not, and neither does the thief himself.
* **over low things**: a surface counts as seen when the space above it is, up to `SeeOverHeight` (1 m) and never
  above the eye. Standing behind a crate lower than his eyes he sees the floor beyond it (he would see anyone
  standing there); crouched below its top, or behind a wall taller than his eyes, he does not. Characters are
  tested at their own points, never lifted: a guard crouching behind the crate stays hidden.

Everything else is drawn **darkened** (`Brightness`, 0.22 of its light) and **drained of colour**
(`Saturation`, 0.1). The level stays readable as a memory of the place. **Characters** there (NPCs, bodies:
entities with Perception, CharacterController or Ragdoll, with all their children) are **not drawn at all**
(`HideActors`), but their noises are still heard. A guard who walks out of the view stays drawn for
`MemoryTime` (0.25 s), so he does not flicker on the cone's edge. A lamp such a character carries (a light below
him in the hierarchy) goes dark with him, so a lantern does not give a hidden guard away; lamps on walls and
posts stay lit whether he sees them or not.

What the fog does not do (yet): it does not remember what was explored (everything unseen is shown dimmed,
explored or not), and darkness in view is only as dark as the lighting makes it.

**Leaning** (Q / E held, `Scripts/Player.lua`): `Lean` moves the eye that many metres sideways, square to where
the view faces (+ right, - left), so the thief sees past a corner without stepping out. The script keeps the eye
out of walls (a sphere swept from his head), and moves the camera, the model (tilted from the feet) and the
silhouette points guards see by the same amount: his head and shoulders are out, his feet are not.

## How it works

* **CPU (`AI/FogOfWarSystem`)**, after animation each frame (`Scene::RunRuntimeFrame`):
  - finds the viewer, the first entity with an enabled FogOfWar;
  - places the eye (eased towards the crouching capsule) and the facing (the camera's forward, flattened; without
    a camera, the body's), and the cone's apex and width (the camera's frame, `ConeFromCamera`);
  - for every character, tests its silhouette points (or three points up its height) against the cone and
    circle (`FogOfWarView::Coverage`) and casts a physics ray from the eye;
  - characters with no point in sight go into the hidden set the renderer skips.
* **GPU (`SceneRenderer`)**:
  - an **eye depth cube** (`EyeMapResolution`, 1024 per face) is rendered from the eye like a point light's
    shadow map, from sight blockers only (the same kinds of things as above);
  - a full-screen pass (`shaders/FogOfWar.hlsl`) rebuilds every pixel's world position, takes its coverage and
    compares its distance from the eye with the cube (5 taps; where that fails, the point `SeeOverHeight` above it,
    up to the eye, the same as `FogOfWarSystem::CanSee`), then blurs the result with the depth-aware SSAO
    blur so the edge of sight is a soft gradient;
  - the tonemap darkens and desaturates by that mask once bloom and exposure are applied, before the tone curve.
* `FogCoverage` in `shaders/FogOfWar.hlsli` mirrors `FogOfWarView::Coverage`: change both or neither, or the
  guards drawn would not match the pixels lit.

The cost is six depth passes over the static geometry in view distance and three full-screen passes. The
performance overlay (F3) lists the CPU side as "fog of war".

## Testing and scripting

* **F7** in the runtime (or the F4 panel's "Fog of war" section) switches it off and on, so you can compare
  what the thief sees with the whole level (`FogOfWarSystem::SetForcedOff`). The panel also tunes the cone,
  circle, distance and darkness live.
* Freelook (F5) keeps the thief's view: you fly through the level and see what he sees.
* Lua: `fog.active()`, `fog.can_see(pos or entity)`, `fog.coverage(pos)`, `fog.hidden(e)` (`ScriptingAPI.md`);
  a script can also change the component (`entity:set("FogOfWar", { ViewAngle = 90 })`, a potion of night sight).
* Tests: `PenumbraTests/src/FogOfWarTests.cpp` (suite `FogOfWar`) and the GPU test "Fog of war darkens and
  greys..." in `RendererTests.cpp`.

Pictures (`docs/art/fogofwar/`, Stealth, the same spot with `ConeFromCamera` and `SeeOverHeight` off and on):
`old_edges.png` / `new_edges.png` (the dark wedges at the screen's sides), `old_crate.png` / `new_crate.png` (the floor
beyond low crates), `new_crouch.png` (crouched behind a crate: still hidden).
