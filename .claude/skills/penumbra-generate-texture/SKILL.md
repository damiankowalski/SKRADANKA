---
name: penumbra-generate-texture
description: Generate a tileable 2D material (albedo, normal map, metallic/roughness, provenance) locally with AssetGen (FLUX.2 klein 4B through mflux) on the owner's Apple Silicon Mac. Use when asked for a texture or material such as a wall, floor, planks or plaster, or to explore looks for one.
---

# Generate a texture with AssetGen

The tool, its files and its limits: `Tools/AssetGen/README.md`. Read it first.

## When

* Exploring how a material could look, or producing a candidate texture for the owner to judge.
* Only on the owner's Mac with Apple Silicon (MLX). On Windows or Linux, say that generation is Mac-only.
  The post-processing and its tests run anywhere.
* Not for 3D, animation, characters or text. Generated textures enter `DemoProject/Assets` only when the owner
  asks, in a PR of their own, with the `.json` provenance file next to them.

## Housekeeping rules (the owner's Mac)

1. Ask the owner before any command that installs, downloads or deletes something on the Mac:
   `setup.sh install`, `login`, `download`, `uninstall` (without `--dry-run`), `uv sync`, `hf download`.
   `setup.sh check`, `uninstall --dry-run` and `generate.py` change nothing outside `Tools/AssetGen/out` and
   need no permission.
2. Never `pip install` globally, touch the system Python or edit `~/.zshrc`. Everything lives in
   `Tools/AssetGen/.venv` (uv, `uv.lock`) and `HF_HOME` (default `~/AI/huggingface`).
3. The weights and the Hugging Face token are shared with the owner's other tools. If `check` sees them, do not
   log in or download again. Never print, copy or commit the token. Never remove the model unless the owner
   asks (`uninstall --with-model`).
4. Only `black-forest-labs/FLUX.2-klein-4B` at the revision in `config.json` (Apache-2.0). FLUX.2 klein 9B is
   non-commercial and blocked. A different model or a new mflux version is a deliberate change in its own PR.
5. Before a full-precision run (`--quantize 0`, about twice the memory), warn the owner so they can close other
   apps. 8-bit is the default.
6. Close `PenumbraEditor` or ask the owner to, before generating: the Mac has 16 GB and the editor competes for
   memory and the GPU.

## How

```sh
Tools/AssetGen/setup.sh check                       # all "ok"? then generate
Tools/AssetGen/.venv/bin/python Tools/AssetGen/generate.py stone_wall --variants 3
```

* A material is a template in `Tools/AssetGen/prompts/<name>.json`: prompt with `{style}` (the shared
  `style.txt`) and `{variation}`, one variation per variant, `roughness` [smooth, rough], `metallic`,
  `normal_strength`. New template: copy `stone_wall.json`. Describe materials, never name games, studios or
  living artists.
* Output: `Tools/AssetGen/out/<material>/` (git-ignored). Per variant `_albedo`, `_normal` (glTF: green up),
  `_mr` (G roughness, B metallic), `_preview` (2x2 tiled: look at it), `.json` (provenance, seam numbers).
* The run fails (exit 1) if a variant fails the seam test even after the masking fallback; look at its preview.
* About 4 minutes per 1024x1024 image on an M1 Pro at 8-bit with the editor closed (four rolled copies per
  step; README.md has the measurements). `--blend-steps 1` is about
  twice as fast but can leave a faint line through the middle; do not use it for textures you hand over.
* To see a texture in the engine: `Tools/AssetGen/EngineCheck.jsonl` (a throwaway project in `build/`).
* Keep every variant worth keeping, chosen or not, in the catalog: `catalog.py add <json> --note "..."` (what
  it looks like, what it could serve), and `catalog.py index` after a texture enters `DemoProject/Assets`. Before
  generating something new, look in `docs/art/textures/catalog/README.md`: a variant there comes back with
  `generate.py --from <json>`.
