# The first mission: what we need before building it

Status: **proposal of 2026-11-26 with the owner's decisions of the same day** (at the end; two points are still
open). The goal is the owner's: one playable mission on one map, with full graphics, 2D and 3D art and sound. This
survey lists what the engine and the content lack for it, how to fill the world with 3D objects, and an order of
packages. Each package still gets its own brief.

## The mission as the owner described it

1. It starts in the city, on a street, perhaps in a tavern.
2. City streets lit by street lanterns (not torches).
3. The target is the mayor's manor. The way in: **the church → its catacombs → a collapsed tunnel → the manor's
   vineyard → the manor**. The catacombs border the vineyard.
4. The manor: kitchen, bathrooms, a drawing room with a fireplace, two staircases to the first floor, and upstairs the
   bedroom and the library.

Rough sizes (to be fixed in the blockout): streets about 120 x 80 m, the church's nave about 25 x 12 m, about 60 m of
catacomb passages, the vineyard about 40 x 30 m, the manor about 30 x 20 m on two floors with about 12 rooms.
Expected contents: 1,500-2,500 pieces of architecture, 1,500-3,000 props placed from 120-160 different models,
150-250 lights (street lanterns, candles, hearths), 8-14 guards, 3-6 servants, 6-10 townsfolk.

### The way out: the leftovers cart (the owner's design, 2026-11-26)

Last evening the lord of the manor gave a feast. What is left of it (bones, scraps, meat going off) waits in a
handcart in the kitchen yard, to be taken out of the grounds.

1. **The note.** Pinned to the kitchen door (or the kitchen wall), from Manuel, the head cook. A draft: *"To whoever
   leaves the kitchen last: the cart with the feast's leftovers already stinks up the whole house. Ring the bell by
   the back door and the guards will take it out past the gate. Manuel."* The note is how the player learns the way
   out; nothing else announces it.
2. **The bell** by the kitchen's back door (F rings it). It is not the alarm bell the servants run to (`Servant.lua`,
   cause "Bell"): guards hear it as an ordinary chore, not as trouble.
3. **The cart.** The thief climbs in (F) and lies hidden under the scraps: guards' perception skips him while he
   stays in it. Climbing in under a guard's eyes is seen as usual.
4. **The ride.** The nearest guard who is awake and calm walks to the cart, takes its handles and pushes it along a
   path out through the gate; the view stays low inside the cart; at the gate the screen fades and the mission ends.
   Between the bell and the guard's arrival the thief has a few seconds to get in: the bell is also a risk.
5. **Nobody left to push it.** If every guard is knocked out or dead, the bell brings no one. Then the only way out is
   the way in: back through the vineyard to the collapsed tunnel (an exit zone at the tunnel's mouth, as
   `Goal.lua` works today). This way stays open in every case: long and dangerous, while the cart is the short way
   for those who read the note.
6. As at today's exit, climbing into the cart before the other objectives are done gets the thief's "I still need
   ..." instead of the ride.

A guard grumbling as he pushes (a subtitled line, "Manuel and his carrion...") is a cheap touch.

Engine and scripts: a hiding place (the thief hidden from perception while in it, S), a "haul" task for guards
(walk to the cart, push it along a path, the cart follows; S-M), the bell and the ending in `MissionControl.lua` and
a new objective kind (S), tests at scene level and in `FeatureTest.lua`. About **M** in all, one package.

## What we have today

| Area | State |
|---|---|
| Thief | Walk, run, crouch, lean (Q/E), steps up to 0.35 m; crossbow with elemental bolts, blackjack, carrying bodies, lockpicking, light gem, compass |
| NPCs | Guards (sight by silhouette contrast in light and fog, hearing through doors and walls, reactions up to alarm), servants and a cook; navmesh with several floors (steps up to 0.4 m) |
| Mission | `MissionControl.lua` + `Missions/*.lua`: objectives, loot, difficulty, summary; doors, light switches, event zones |
| World | PBR forward renderer, sun/moon cascades, 8 point and 8 spot shadow maps, volumetric fog with fog-free interiors, the agent playtester |
| 2D art | AssetGen (FLUX.2 klein 4B), 14 materials in the game (stone walls, cobblestones, shingles, ceiling, and the nine of the first mission) |
| 3D art | Two KayKit characters (Thief, Knight). **No props**: levels are built-in cubes and cylinders |
| Performance | Stealth (122 entities, about 380 draw calls): about 10 ms a frame, 1600 x 900 windowed, M1 Pro, release, no vsync |

## What is missing

### A. The engine, for a map of this size (needed before building it)

Measured against the expected contents above. Without A the map would cost several times today's frame.

| # | What | Why | Size |
|---|---|---|---|
| A1 | **Instancing** of repeated meshes (one draw for every copy of a model with one material) | Today every object is its own draw: 3,000-5,000 draws; instancing brings that to hundreds | M |
| A2 | **Light culling** (clustered forward: each pixel loops only over the lights near it) | Today every pixel loops over every light in the scene (`Lighting.hlsli`); 200 lanterns and candles would cost far more than the whole frame does today | M |
| A3 | **LOD and draw distance**: models carry 2-3 levels of detail, small props vanish past a distance per size class | A street seen end to end draws every cup on every table | S-M |
| A4 | **Room-bounded lights and interiors**: a light lights only inside its room's box; inside a room the sky's ambient light is dimmed (as the Clear fog volumes do for fog). Renderer and perception together (rule 5) | Lights without a shadow map shine through walls into the next room (8 point shadows for 150+ lights); interiors today get the sky's ambient light | S-M |
| A5 | **Alpha-tested materials** (cutout): leaves, vines, ivy, grates, cobwebs; sight rays pass leaves by their own rule | The vineyard and overgrown places are impossible without it | S |
| A6 | **Texture compression** (BC7/BC5, made once at import, cached with the mip chain) | Textures are RGBA8 today with mips built at load: about 17 MB of memory per material, slow loading with 40+ materials | S-M |
| A7 | **Collision proxies** from glTF (`_col` nodes: boxes and convex hulls instead of the render mesh) | `MeshCollider` uses the render mesh: slow physics and raycasts (perception casts many) | S |
| A8 | **Stress-test district**: a generated scene with the expected numbers (5,000 props, 200 lights, 14 guards) and a frame budget measured on the Mac and on Windows | Proves A1-A7 before the real map is built; stays as a performance test | S |

Budget proposed: 60 FPS at 1080p on the owner's Windows PC and the M1 Pro (16.6 ms: about 12 ms GPU, about 4 ms of
simulation), at most about 1,500 draws and about 16 lights per pixel after culling.

### B. Gameplay this mission needs

| What | Note | Size |
|---|---|---|
| **Blockout of the mission**: the whole route in grey boxes, playable with today's mechanics, with playtester routes | The first step of building: sets the sizes, sightlines and the prop list before any art | M |
| Readables: books in the library, the mayor's letter, notes, Manuel's note (roadmap "map, compass and readables") | Story and hints; Manuel's note teaches the way out | S-M |
| **The leftovers cart** (above): hiding place, the bell, a guard hauling the cart out, the ending | The mission's main way out | M |
| Keys for locked doors (lockpicking exists) | The manor's rooms | S |
| The collapsed tunnel: crawl through rubble (crouch) or clear it by an interaction | A scripted passage | S |
| Townsfolk in the tavern and the street: sit, drink, flee and call the guards | Needs animations (with the characters' decision) | M |
| **Checkpoints** (the owner's decision: enough for the first version): the mission keeps a snapshot at the church door, the end of the catacombs and the manor's kitchen door; a caught thief starts again from the last one instead of the street. The snapshot lives in memory (play mode already copies scenes; each script hands over its state through a small Lua hook). Free saving stays for later | The hard part of full saves (the scripts' state) shrinks to a few hooks | S-M |
| A full-screen map (roadmap: a static picture, S) | | S |
| Climbing, rope arrows, swimming | **Not needed** if the route stays on the ground and stairs: left for later | — |

### C. 2D art

* **Materials, batch 2** (S each, 2-3 PRs): brick, fired clay tiles, the church's dressed light stone and floor
  tiles, rough rock and bone niches for the catacombs, vineyard soil and grass, the manor's wood panelling,
  wallpaper or painted plaster, rugs, kitchen and bath tiles, polished wood for furniture. About 12-15.
* **Decals** (roadmap: polish, step 8, M-L): dirt at the foot of walls, soot above hearths, damp, moss.
* Emissive textures: lit windows (S). Paintings for the manor's walls, book spines, the tavern's sign, generated
  as 2D images by AssetGen (S).

### D. 3D objects (the owner's main question, below)

### E. Characters

KayKit's chibi proportions do not fit direction B (roadmap: Quaternius Universal Base Characters and the Universal
Animation Library, CC0, M). The mission needs guards, the mayor, servants and townsfolk: variants of a few base
bodies (outfits, colours, hats, a lantern). Generating rigged characters with AI is not possible today (rejected in
`AITools.md`), so the bodies come from CC0 and their variants from materials and attached props.

The owner confirmed Quaternius (2026-11-26) and asked to see a mock-up first: Universal Base Characters (standard
zip about 122 MB) with Modular Character Outfits - Fantasy (12 outfits from 62 parts, three texture variants each,
standard zip about 263 MB), both CC0 from quaternius.itch.io, rendered by the engine in the mood lab beside today's
KayKit knight. The download waits for his approval; nothing of it enters the repository before the characters'
package.

### F. Sound

**AudioGen, package A** (effects and ambience with a listening room, the owner's brief of 2026-11-26:
`docs/research/AudioGenBrief.md`), then B (music) and C (voices). The owner's order: once the mission's world
stands (A-E).

## Filling the world with 3D objects

Four sources, by share of the objects:

### 1. Procedural, in Blender, scripted by agents (about 75%)

Everything built from boards, beams, stone blocks and iron: the architecture kit (walls with door and window
openings, frames, arches, vaults, stairs, columns, beams, roof pieces, fences, the vineyard's trellises, lantern
posts) and most furniture (tables, chairs, benches, stools, beds, wardrobes, chests, shelves, bookcases, desks,
a fireplace, a bathtub, the kitchen hearth and its pots, barrels and wine racks, crates, coffins and niches).

* **How**: a new tool, `Tools/PropGen`, one Python script per family of objects, run by Blender without a window
  (`blender --background --python`). Each script takes parameters (sizes, wear, variant) and writes a `.glb` with
  its LODs, collision proxy and a provenance `.json`, like AssetGen. Generated files are committed, like the demo's
  scenes; the scripts stay the source.
* **Why**: exact sizes in metres (doors fit the thief, steps fit the controller), clean low-poly topology, UVs that
  take the generated materials (`docs/art/World.md`: "planks with iron bands and nails" is a parameter, not luck),
  variants for free, and optimisation built in. The look, simple shapes under hand-painted textures, is the
  stylised realism of direction B.
* **Blender**: yes, we need it (free; GPL covers Blender itself, not what it makes). One install on the Mac, about
  1 GB, with your approval, when step 3 starts; on Windows only if you want to regenerate props there.
* **Scripts or Blender MCP** (the owner's question, 2026-11-26): both drive the same Python API (`bpy`), so neither
  makes a better or lighter model by itself. What makes a prop fit the engine is in the scripts: sizes, triangle
  budgets, LODs, collision proxies, shared texture sheets, and the check in the engine's prop lab. The difference is
  the way of working:
  * *Headless scripts* (`blender --background`) are the source: in the repository, reproducible, regenerated in
    one run when a budget or a material changes, run without a window and on Windows too, each prop's provenance
    is its script and parameters.
  * *Blender MCP* is a live workbench: the agent works in the open Blender while the owner watches, and sees
    Blender's own renders; good for exploring a new family or fixing one prop by eye. Whatever is worked out there
    goes back into the family's script, or it is lost with the `.blend`. Blender now has an official MCP server
    (a Blender Lab project, Blender 5.1 or newer, an add-on); its own page warns that it runs the model's code in
    Blender without guards, so it runs only in sessions the owner opens, never unattended. The community
    `ahujasid/blender-mcp` (MIT, in `AITools.md`) came first.
  * Recommended: scripts as the base, the official MCP as an addition for live sessions. Decided when step 3
    starts.
* **A prop lab** scene (like the mood lab) shows each family under the game's light, with engine screenshots for
  your review, as with the materials.

### 2. CC0 libraries, chosen piece by piece (about 15%)

Small clutter where shape matters little: bottles, cups, books, candles, cutlery, tools. Candidates: Poly Haven
(CC0, realistic PBR: its textures are redone in our style or used where they fit), Quaternius and KayKit (CC0,
stylised: checked one by one for proportions). Allowed by the owner (2026-11-26); each download needs his approval
(the file, the source, the size); each piece keeps its licence and source in a provenance file. Which packs fit is
checked in the prop pipeline's brief.

### 3. AI, image to 3D (about 5%: organic "hero" props)

A saint's statue in the church, skulls and bone piles in the catacombs, a gargoyle, food on the table. Pipeline:
a concept image from AssetGen → **Pixal3D** (MIT, MLX port, already on the roadmap as a PoC) → a Blender script
cleans it (scale, pivot, decimation, LODs, collision) → `.glb`. Risk: quality and the 16 GB limit (about 10 minutes
a model on a much faster Mac). If the PoC fails, these come from CC0 or simpler procedural shapes.

### 4. Plants (about 5%)

Vines, ivy, grass tufts and hedges as procedural cards with alpha (needs A5); a few trees from Blender's tree
generator or CC0. At night in fog they need little detail.

### Rules for every object, from the first one

* Metres, Y up, pivot at the base, named `<family>_<variant>`.
* Triangles: clutter 50-500, furniture 300-2,000, big pieces 1,000-5,000, hero props up to 10,000; LOD1 about 50%,
  LOD2 about 15%; a draw distance per size class.
* Shared materials: each kit uses a few texture sheets (trim sheets and atlases) so that copies instance (A1).
* Collision: boxes or convex proxies, never the render mesh; small clutter has none or one box.
* Small clutter casts no shadows; perception flags (`SightBlocker`) set per family.
* Every object has a provenance file (script and parameters, or source and licence, or model and seed).

## Order (the blockout first: the owner left the choice to the agent, 2026-11-26)

1. **Blockout of mission 1** (B, M): the whole route playable in grey boxes; the playtester walks it. Fixes the
   prop list and the budget, and its real counts (objects, lights per room, guards) become the numbers of the
   stress test in step 2, so the engine work is measured against the map rather than guesses. Both ways out are laid
   out; the vineyard one works with today's `Goal.lua`, the cart waits for its package in step 6.
2. **Engine for a big map** (A1-A8, about 6 PRs, M-L in all), starting with the stress-test district.
3. **Prop pipeline**: Blender install, `Tools/PropGen`, the skill, the prop lab, the architecture kit (M).
4. **Pixal3D PoC** (M) and the characters' decision (M), in that order.
5. **Content per place**, one PR each: street and tavern; church and catacombs; vineyard; the manor's ground floor;
   its first floor. Each: materials, props, lights, then the place built over the blockout (M each).
6. **Gameplay**: the leftovers cart, readables (Manuel's note first), keys, the tunnel, checkpoints, townsfolk (S to
   M).
7. **Sound**: AudioGen A (the owner's brief), then B and C.
8. **Polish**: decals, playtester series, the Windows check of the budget.

Roughly 25-35 PRs. Steps 1-2 also show early whether the plan holds.

## What is not possible, and the compromises recommended

| Wish | Problem | Recommendation |
|---|---|---|
| Unique characters (the mayor, townsfolk) | No generator for rigged characters fits 16 GB and a commercial licence | CC0 base bodies (Quaternius), variants by outfit, colour and attached props |
| AI-generated architecture and furniture | Image-to-3D gives soft, heavy meshes with wrong sizes | Procedural in Blender; AI only for a few organic props |
| A dense vineyard and overgrowth | Needs cutout materials and instancing (A1, A5) | After A; simple stylised vines; night and fog hide detail |
| Every light casting real shadows | 8 point shadow maps; more cost a lot | Room-bounded lights (A4) stop leaks; shadows for a few key lights |
| Saving anywhere | Lua state and AI state are the hard part (M-L) | **Decided**: checkpoints in the first version (B above); free saving later |
| Music and voice in the first playable version | Packages B and C | Effects and ambience (A) first; music and voices can follow the first playable mission |

## Decisions (the owner, 2026-11-26)

1. **Blender and MCP**: open. The owner leaned to Blender MCP if it makes better models for the engine; it does
   not by itself (D.1 above). Recommended: headless scripts as the base, the official Blender MCP added for live
   sessions. Settled when step 3 starts; Blender is installed then, with his approval.
2. **Order**: left to the agent: the blockout first (Order, step 1).
3. **Saving**: checkpoints are enough for the first version.
4. **The way out**: the leftovers cart, with the vineyard back to the tunnel when no guard is left (above).
   Still open: the main loot, and whether the lord of the manor is the mayor, a baron, or a baron who is the
   city's mayor.
5. **Characters**: Quaternius confirmed, after a mock-up rendered by the engine (E above).
6. **CC0 clutter**: allowed, each download approved by the owner.
