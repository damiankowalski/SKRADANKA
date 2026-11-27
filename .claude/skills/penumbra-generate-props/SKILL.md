---
name: penumbra-generate-props
description: Build 3D props (crates, barrels, tables, chairs, benches, shelves, the architecture kit) from PropGen's family scripts into DemoProject, check them, and show them in the engine's prop lab. Use when asked for a prop, furniture or a piece of architecture, or to change the size, look or detail of existing ones.
---

# Generate props with PropGen

The tool, its rules and its checks: `Tools/PropGen/README.md`. Read it first. What the world looks like:
`docs/art/World.md`; what the mission needs: `docs/research/MissionOne.md`.

## When

* A prop or a family of props the levels need, a new variant (another size, the dark boards), or a change to one.
* Anywhere: the kit is Python's standard library, no install. Blender only for what the README's "Blender" section
  names, and only once it is installed with the owner's approval.

## How

```sh
python3 Tools/PropGen/generate.py <family>               # writes the props, then checks them
python3 Tools/BuildDemoProject.py                        # the prop lab and every scene using add_prop
PenumbraEditor --headless --exec docs/art/props/PropLab.jsonl --project DemoProject
```

1. Real sizes in metres first (a seat at 45 cm, a table at 76 cm, a doorway of 1.4 m); the pivot at the middle of
   the base; the front of a chair faces -Z.
2. Build from `Prop.box` and `Prop.lathe`. Boards follow the grain with `board=True`; wood is `wood` or `wood_dark`,
   iron `iron` (config.json); a new material is a texture sheet from AssetGen added there.
3. Author three levels: every board in LOD0, slabs in LOD1, the silhouette in LOD2. Keep within the family's budget.
4. Collision boxes the thief and bodies meet: a table's top and legs, not its aprons.
5. Look at it: render the prop lab and read the screenshots (engine screenshots only). Fix what looks wrong in the
   script, never in the `.glb`.
6. Commit the script, the generated files and the screenshots together; `Tools.PropGen` fails when the committed
   files are stale.
