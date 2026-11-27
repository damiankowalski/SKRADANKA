# PropGen: procedural props

Builds the props the world is furnished with (crates, barrels, chests, tables, chairs, stools, benches, shelves,
wine racks, plank doors, pottery, buckets, candlesticks) and the start of the architecture kit (walls with doorways
and windows, timber, stairs) from scripts: one Python file per family of objects, with parameters per variant. The
scripts are the source; the generated `.glb` files and their provenance are committed, like the demo's scenes. Agents:
`.claude/skills/penumbra-generate-props`. Why this way: `docs/research/MissionOne.md`, "Filling the world with 3D
objects".

```sh
python3 Tools/PropGen/generate.py                  # every family; then the checks
python3 Tools/PropGen/generate.py crate barrel      # some families
python3 Tools/PropGen/generate.py --check           # only the checks
PenumbraEditor --headless --exec docs/art/props/PropLab.jsonl --project DemoProject   # engine screenshots
```

Python's standard library only: no Blender, no packages, nothing to install; it runs on the Mac and on Windows.

## What lives where

| What | Where | In git |
|---|---|---|
| The kit: `propgen/mesh.py` (parts, UVs, LODs), `propgen/glb.py` (writer, reader), `propgen/check.py` | `Tools/PropGen/` | yes |
| Families: `families/<family>.py` (`FAMILY`, `BUDGET`, `VARIANTS`, `build`) | `Tools/PropGen/families/` | yes |
| Budgets, LOD ratios, the materials (texture sheets) | `Tools/PropGen/config.json` | yes |
| Props: `<family>_<variant>.glb` (LOD0), `_lod1.glb`, `_lod2.glb`, `.json` (provenance) | `DemoProject/Assets/Models/Props/<family>/` | yes, generated |
| The prop lab: every family under the game's light | `Scenes/PropLab.pscene` (`Tools/BuildDemoProject.py`) | yes, generated |
| Engine screenshots for review | `docs/art/props/` (`PropLab.jsonl`) | yes |

## How a prop is made

* **Parts**: boxes (`Prop.box`: a board, a leg, a strap; turned by Euler degrees) and solids of revolution
  (`Prop.lathe`: a barrel's staves, its heads and hoops), flat-shaded, in metres, Y up, with the pivot at the
  middle of the base.
* **Materials** are the generated texture sheets in `Assets/Textures` (config.json `materials`: oak boards, tarred
  boards, rusted iron, light and dark stone, lime plaster; clay, glaze and wax are plain colours until they have
  sheets of their own). A `.glb` refers to them by relative URI, never embeds them: the engine loads a sheet once
  and every prop shares it (`AssetManager`), in memory and on the GPU.
* **UVs** are in metres of the texture (`metres_per_repeat`), along the wood's grain (`grain`, default the
  longest side). The board textures have eight boards per repeat running along V: a board-sized part
  (`board=True`) is mapped onto exactly one of them, so the texture's dark gaps fall on the part's edges. Stone
  and plaster stay upright (`grain="y"`), and the pieces of a wall around an opening share the wall's own UVs
  (`aligned=True`), so the stones run on across them.
* **LODs** are authored, not decimated: each part says which levels it belongs to (`lods`). LOD0 is every board,
  LOD1 a side as one slab (no nails, braces or straps), LOD2 the silhouette (a crate is one box). The engine draws
  LOD0 until it has LODs and draw distances (`MissionOne.md` A3); the files are ready for it.
* **Collision**: a family adds boxes (`Prop.collide`: a table's top and legs, a chair's seat and back), or the
  prop gets one box around it. `Tools/BuildDemoProject.py` `add_prop` places a prop with its boxes as children.
* **Determinism**: every random choice (which texture board a board shows, where a texture starts) comes from a
  seed made of the family and the variant's names, so a regeneration that changes nothing changes no file.

## Checks

`generate.py` checks everything it writes (and `--check` what is there); the tests check the committed files too:

* each `.glb` reads as glTF 2.0; every triangle winds the way its normal points; UVs are numbers;
* the base lies on the pivot (y = 0 within 2 mm) and the footprint's middle within 15% of the pivot;
* triangles within the family's size class (config.json `budgets`: clutter 500, furniture 2,000, big 5,000,
  hero 10,000), LOD1 and LOD2 lighter (about 50% and 15%, or under 120 and 64 triangles for light props);
* every texture a prop names exists inside the project's assets; collision boxes exist;
* the provenance's triangle counts match the files, and the committed files are what the scripts make.

## Families

| Family | Variants | Triangles (LOD0 / 1 / 2) |
|---|---|---|
| crate | banded, large, long, small | 252-504 / 108 / 12 |
| barrel | keg, water, wine | 304-464 / 172-296 / 38 |
| chest | dowry, strongbox, travel | 300-336 / 108-144 / 12 |
| table | feast, kitchen, small | 132-192 / 108 / 60 |
| chair | plain, slatted | 156-204 / 108 / 48 |
| stool | milking, tavern | 60-80 / 60-80 / 56-60 |
| bench | long, short | 96 / 72 / 36 |
| shelf | cellar, kitchen, low | 108-156 / 72-108 / 36-48 |
| wine_rack | barrel, wall | 288-1032 / 144-312 / 60 |
| door | cellar, house, leaf | 336-372 / 72 / 12 |
| pottery | bowl, jug, mug, storage_jar | 124-200 / 60-116 / 20-32 |
| bucket | washtub, well | 176-212 / 22-25 / 16 |
| candlestick | pricket, table | 126 / 74 / 20 |
| wall | plaster_4m, plaster_window, stone_2m, stone_4m, stone_door, stone_window | 12-132 / 12-108 / 12-48 |
| beam | ceiling_4m, ceiling_6m, post, post_braced | 24-60 / 12-60 / 12-24 |
| stairs | cellar, short, storey | 48-300 / 24-156 / 12-48 |

A new family: copy the closest script, set `FAMILY`, `BUDGET` and `VARIANTS`, build it from boxes and lathes, add
it to `families/__init__.py`, run `generate.py`, add it to the prop lab (`PROP_LAB_ROWS` in
`Tools/BuildDemoProject.py`), render the lab and look at it. Real sizes: a chair's seat at 45 cm, a table at 76 cm,
a door the thief walks through at 1.4 m.

## Blender

Not needed for these families: boards, beams, staves and iron are boxes and lathes, which the kit builds exactly,
with UVs on the texture's boards, in a few milliseconds and the same bytes every time, and which CI checks on
Windows without installing anything. Blender (approved by the owner, not installed yet) comes in where a family
needs modelling the kit does not do: bevels and booleans for the architecture kit's arches and openings, cleaning
AI or CC0 meshes (Pixal3D's hero props: scale, pivot, decimation, LODs), and the official Blender MCP for live
sessions with the owner. Its scripts then write into the same folders with the same provenance and checks.

## Tests

`Tools.PropGen` (CTest, any Python 3): the kit (faces out, UVs along the grain, a board on one texture board,
lathes and caps, the seed), the writer and reader (round trip, float32 bounds, URIs not embedded, the same bytes
twice, broken files rejected), the checks (a good prop passes; off the pivot, inside out, over budget, heavy LODs and
a missing texture fail), and the committed props (all pass, all match their scripts). In the engine,
`PropLab: every generated prop loads...` (LevelTests): triangles and bounds as the provenance says, one oak sheet
shared by every prop, collision boxes in place.
