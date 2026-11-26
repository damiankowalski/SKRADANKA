# AssetGen: local 2D textures

Generates tileable PBR materials (stone walls, planks, plaster) on an Apple Silicon Mac with
**FLUX.2 klein 4B** (Black Forest Labs, Apache-2.0) through **mflux** (MLX). One command loads the model,
renders, writes the files and exits. Nothing runs in the background. Agents: `.claude/skills/penumbra-generate-texture`.

## What lives where

| What | Where | Size | In git |
|---|---|---|---|
| The tool: `generate.py`, `assetgen/`, `prompts/`, `config.json`, `setup.sh`, tests | `Tools/AssetGen/` | small | yes |
| Pinned dependencies | `pyproject.toml`, `uv.lock`, `.python-version` | small | yes |
| Python environment (Python 3.12 from uv, mflux 0.24.0, MLX, torch) | `Tools/AssetGen/.venv` | ~1.1 GB | no (ignored) |
| Exploration output | `Tools/AssetGen/out/` | ~5 MB per material variant | no (ignored) |
| The catalog: every variant worth keeping, as provenance + 512 px preview | `docs/art/textures/catalog/` | ~0.25 MB per variant | yes |
| Model weights, pinned revision | `$HF_HOME/hub` (default `~/AI/huggingface`) | ~16 GB | never |
| Hugging Face token | `$HF_HOME/token` | - | never |
| uv's Python 3.12 and download cache | `~/.local/share/uv/python`, `~/.cache/uv` | ~70 MB, shared | no |

`HF_HOME` is the shell's (the owner sets it in `~/.zshrc`), else `~/AI/huggingface`. The weights and the token
there are shared with other tools, so AssetGen never logs in or downloads when they are already there.

## Setup

```sh
Tools/AssetGen/setup.sh check        # changes nothing: Apple Silicon, uv, .venv, token, model, access, disk
Tools/AssetGen/setup.sh install      # .venv from uv.lock (asks first)
Tools/AssetGen/setup.sh login        # hf auth login into HF_HOME; skipped if a token is there
Tools/AssetGen/setup.sh download     # the pinned revision into HF_HOME; skipped if it is there (asks first)
```

On Windows or Linux `setup.sh` says that generation needs Apple Silicon and exits with 1; the tests still run.

## Generate

```sh
Tools/AssetGen/.venv/bin/python Tools/AssetGen/generate.py stone_wall --variants 3
```

Options: `--seed` (variant *i* uses seed + *i*), `--steps` (4), `--size` (1024), `--quantize` (8; 4, or 0 for full
precision, about twice the memory), `--out`, `--sheet <png>` (a contact sheet of all variants),
`--no-seamless` (plain sampling, for comparison), `--blend-steps`, `--no-fallback`, `--only N` (redo variant N of
a set), `--from <json>` (regenerate exactly the variant a provenance file describes: see the catalog below),
`--reprocess` (no model: redo the maps, seam test, previews and sheet from the albedos already in `--out`,
e.g. after changing `normal_strength`), `--model` (only the pinned one runs). Close PenumbraEditor first on a 16 GB Mac: `generate.py` warns when it is running.

Measured on an M1 Pro with 16 GB, one 1024x1024 image, editor closed:

| | time | MLX peak | process peak | image |
|---|---|---|---|---|
| 8-bit (default) | 238 s | 6.5 GB | 8.0 GB | the reference |
| full precision (`--quantize 0`) | 323 s | 9.9 GB | 11.4 GB | same composition, 1.1% mean pixel difference |

With the editor open, 8-bit took 320-470 s (the editor competes for the GPU and memory).

Per variant, in `out/<material>/`:

* `<name>_albedo.png`: tileable colour (sRGB).
* `<name>_normal.png`: tangent-space normals, glTF convention as `Mesh.hlsl` reads them: +X right, +Y up the image
  (green up), +Z out.
* `<name>_mr.png`: glTF metallic/roughness: G = roughness, B = metallic, R = 1.
* `<name>_preview.png`: the albedo tiled 2x2 at its own size. Every seam (edges and middle) is in view.
* `<name>.json`: provenance. Prompt, seed, steps, size, quantisation, model, revision and licence, mflux version,
  date, `"ai_generated": true`, the seam test's numbers and timings.

Prompts are templates in `prompts/`. `style.txt` is the shared art direction (hand-painted stylised realism, dark,
muted, flat light: `docs/art/mood/README.md`). `<material>.json` holds the prompt with `{style}` and `{variation}`,
one variation per variant, `roughness` [smooth, rough], `metallic` and `normal_strength`. Describe the material.
Never name games, studios or living artists.
Templates so far: `stone_wall` (walls), `cobblestone` (floors seen from above), `roof_tiles` (roofs seen from
above), `wood_ceiling` (ceilings seen from below), and for the first mission `planks`, `plaster`, `stone_floor`,
`ground` and `metal`, three variations each.

## The catalog

Variants that are not picked today may serve elsewhere tomorrow, so every one worth keeping goes into
`docs/art/textures/catalog/<material>/`: its provenance `.json` with a note (what it looks like, what it could
serve) and its 2x2 preview at 512 px. The index, `docs/art/textures/catalog/README.md`, is generated and says which
variants the game uses (by the albedo's file name under `DemoProject/Assets`).

```sh
Tools/AssetGen/.venv/bin/python Tools/AssetGen/catalog.py add out/<material>/<name>.json --note "..."
Tools/AssetGen/.venv/bin/python Tools/AssetGen/catalog.py index    # after adding a texture to the game
Tools/AssetGen/.venv/bin/python Tools/AssetGen/generate.py --from docs/art/textures/catalog/<material>/<name>.json
```

The full maps stay out of git (3-4 MB per variant, forever in history). `--from` brings them back: it takes the
prompt, seed, steps, size, quantisation, blend steps and maps settings from the `.json` and refuses a record made
with another model revision or mflux version. On the same Mac the result is byte-identical (checked on
`cobblestone_1001`); another Apple chip or MLX version may differ by a few pixel levels. `catalog.py check` (and
the tests) fail when a record is broken or the index is stale.

## How it tiles

**At generation** (`assetgen/generator.py`): the transformer has no idea that a texture wraps, so on every
denoising step it predicts the noise for four copies of the latent rolled by half a tile (none, across, down,
both). The predictions are blended, each copy weighted away from its own cut, so every point is drawn by a copy
in which it lies in the middle of the picture. The VAE then decodes the latent padded with its own wrapped edges.
This costs four transformer passes per step. `--blend-steps 1` rolls one copy per step and blends only on the
last; it is about twice as fast but left a faint line through the middle of a stone (see the PR).

**The test** (`assetgen/processing.py`, `seam_report`) runs along both axes on the luminance:

* *step*: the change across the wrap must be no bigger than between neighbouring columns (1.6x the median,
  or the texture's own strongest lines);
* *detail*: no band of 24 px, anywhere (the middle included), may lose fine detail (0.55x the median). This
  catches the soft cross that offset-and-blend leaves;
* *line*: no straight step in tone along part of the edge or the centre line (2.5x the median column). This
  catches a cut that only shows across one stone.

**Fallback** (when the test fails): the image is blended with itself shifted by half a tile, one axis at a time,
band by band (fine detail crosses over within a few pixels, so the middle stays sharp). The `.json` records
`generation+masking`. If it still fails, `generate.py` exits with 1.

**Limits**: height, and from it the normals and roughness, comes from the albedo's brightness (light stone faces
up, dark mortar down). That is an approximation: a dark stone reads as low, painted highlights as relief. The
albedo can carry painted light and shade, which the engine's lighting then doubles.

## Tests

`Tools/Test.sh` / `Tools\Test.bat` run `Tools.AssetGen` (CTest) when `uv` is installed: the seam test (a tiling
texture passes; a cut-out one, a blurred cross and a faint line through the middle fail; the fallback fixes a
cut-out one without a soft cross), normal map convention and wrapping, metallic/roughness channels, the
provenance schema and the model allow-list. No model and no Apple Silicon are needed (CI runs them on Windows
and macOS). By hand: `uv run --project Tools/AssetGen --frozen python -m unittest discover -s Tools/AssetGen/tests`.

In the engine: `PenumbraEditor --headless --exec Tools/AssetGen/EngineCheck.jsonl` imports a generated stone wall
into a throwaway project in `build/` and renders it on a 4 x 2 m box, repeated 4 x 2 times (`Material.UVScale`).

## Updating

Only deliberately, in a PR of its own: change `mflux` in `pyproject.toml` and `mflux_version` in `config.json`,
`uv lock`, `setup.sh install`, rerun the tests and the demo, and compare. `generator.py` reuses mflux internals
(`_encode_prompt_pair`, `_prepare_generation_latents`, `_predict`), so a new mflux can break it. A new model
revision: change `revision`, `setup.sh download`, compare. A new model: add it to `allowed_models` with its
licence; non-commercial models never.

## Removing everything

```sh
Tools/AssetGen/setup.sh uninstall --dry-run            # lists everything with sizes, removes nothing
Tools/AssetGen/setup.sh uninstall                      # .venv and out/ only (asks first)
Tools/AssetGen/setup.sh uninstall --with-model         # also the model in HF_HOME (shared: other tools lose it)
Tools/AssetGen/setup.sh uninstall --with-uv-cache      # also uv's cache and its Python 3.12 (shared by uv projects)
```

The token is never removed by the script. To remove it: run `hf auth logout`, or delete `$HF_HOME/token` and
`$HF_HOME/stored_tokens`, then revoke the token on huggingface.co (Settings, Access Tokens). Remove the
`HF_HOME` line from `~/.zshrc` only if nothing else uses it.
