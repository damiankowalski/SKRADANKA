# Demo characters

| File | Source | License |
|---|---|---|
| `Thief.glb` | `Rogue_Hooded.glb` from [KayKit Character Pack: Adventurers 1.0](https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0) by Kay Lousberg (commit `672074b`) | CC0 1.0 |
| `Knight.glb` | `Knight.glb` from the same pack | CC0 1.0 |

The pack's license text is in `KayKit_LICENSE.txt`. Crediting Kay Lousberg (www.kaylousberg.com)
is appreciated but not required.

Both files are produced by `Tools/ImportCharacters.py`, which:

* keeps the pack's 41-joint rig, meshes, weapons/shields/capes (rigid parts on hand slots and the
  chest bone) and its 76 clips (`Idle`, `Walking_A/B/C`, `Running_A/B`, `1H_Ranged_Shoot`, `Cheer`, ...);
* authors the clips the pack lacks, baked at 30 fps: `Crouch_Idle` and `Crouch_Walk` (hips lowered,
  spine leaned, legs re-solved with two-bone IK so the feet stay planted) and `Look_Around`
  (head and chest sweep over four idle cycles);
* recolours the thief's green cloth to charcoal.

Characters pick the parts they show with `MeshComponent.HiddenNodes` (see `Tools/BuildDemoProject.py`):
the thief carries a one-hand crossbow, city-watch guards a sword and round shield, the knight a
sword, badge shield and red cape.
