# Local AI tools for making the game

Research notes on AI models that could help make Penumbra games, run locally on the owner's MacBook Pro (M1 Pro,
16 GB), as of 2026-11-22 (researched in the design project). Nothing here is installed or implemented; each tool
enters the repository only through its own approved package (`docs/Roadmap.md`, "Local AI tools"). Licences,
sizes and memory figures are as researched; re-check them against the model card before each brief.

## Criteria

* **Runs locally on 16 GB** (unified memory, Apple Silicon): no cloud service, no account beyond a model download.
* **Commercial licence**: the game may be sold. Non-commercial models are out, however good.
* **No duplicates**: one model per job.

## House rules (as AssetGen, `Tools/AssetGen/README.md`)

* One model in memory at a time; close the editor and other heavy apps first.
* Weights live in the shared `~/AI/huggingface` (`HF_HOME`), never in the repository.
* Each tool has its own uv environment (`.venv` beside it, `uv.lock`), pinned versions; nothing global, no changes
  to the system Python or the shell profile.
* Every result carries a provenance file beside it (model, revision, licence, prompt, seed, settings,
  `ai_generated: true`), like AssetGen's `.json`.
* The owner approves every install, download and removal before it runs.

## For the game

| Tool | What it makes | Licence | On 16 GB | Notes |
|---|---|---|---|---|
| **Pixal3D** (MLX port, e.g. github.com/lyonsno/pixal3d-mlx) | One photo → a 3D model with PBR textures | MIT; the image encoder is under the DINOv3 licence | At the limit: 512 px, about 10 min on an M4 Max, so longer here; close other apps | Props only (a jug, a chair, a lantern), not characters or architecture. Meshes need cleaning in Blender (retopology, scale, pivot, collision) |
| **Blender MCP** (github.com/ahujasid/blender-mcp) | Claude drives Blender through its Python API | MIT | Yes (no model) | Architecture (walls, doors, crates, stairs) is built procedurally, not generated: exact sizes, clean topology, UVs that tile with `Material.UVScale` |
| **Kimodo** (NVIDIA) through kimodo-turbo (github.com/octaviusp/kimodo-turbo) | Text → body animation → BVH | NVIDIA Open Model License (commercial use allowed); its text encoder is under the Llama 3 Community License | To check | Retargeted in Blender onto our skeleton (`Tools/ImportCharacters.py`); a proof of concept first |
| **Stable Audio 3 Small SFX** (433M; replaced Stable Audio Open Small) | Sound effects and ambience, up to 120 s | Stability AI Community License: commercial use up to USD 1M revenue a year; its T5Gemma text encoder under the Gemma Terms | Yes, on the CPU: 60 s in 8.8 s, 4.5 GB | In use: `Tools/AudioGen` (footsteps per surface, doors, wind, rain, fire crackle) |
| **ACE-Step 1.5** | Music | MIT | Yes (MLX backend) | Ambient loops, tension and alarm cues |
| **Chatterbox Multilingual** | Character voices, 23 languages including Polish, with emotion | MIT | Yes | Guards' barks and lines. Output carries a PerTh watermark. Clone only the owner's own voice or a voice given with consent |

## General purpose

| Tool | Job | Licence | Notes |
|---|---|---|---|
| **Whisper large-v3-turbo** | Speech → text: subtitles for voiced lines | MIT | Pairs with Chatterbox: subtitles from the generated audio |
| **BiRefNet** | Background removal | MIT | Inventory icons, cut-outs for concept art |
| **Depth Anything V2 Small** | Depth / height maps from an image | Apache-2.0 (the larger variants are non-commercial: Small only) | Better height and normal maps for AssetGen than the albedo's brightness |
| **Real-ESRGAN** | Image upscaling | BSD-3-Clause | 1024 px textures to 2048 where needed |
| **Qwen3.5-9B** | A local language model | as published (check) | Little value next to Claude; listed for completeness |

## Rejected

| Tool | Why |
|---|---|
| **Hunyuan3D** | Its licence excludes the European Union |
| **AssetFurnace** (as a whole) | Recommends 32 GB of memory |
| **TRELLIS.2** | About 15 GB at peak: no room on a 16 GB machine |
| Non-commercial models (e.g. FLUX.2 klein 9B, larger Depth Anything V2 variants) | The game may be sold |

## Where they would fit

* **Props**: Pixal3D for a few hero props from photos; Blender MCP for everything built from boxes and planks.
* **Animation**: Kimodo for extra moves (leaning, climbing, carrying) on our skeleton, if 16 GB suffices.
* **Audio**: an AudioGen tool on the AssetGen model (`Tools/AudioGen`: Stable Audio 3 Small SFX; then ACE-Step 1.5, Chatterbox), with
  Whisper for subtitles.
* **Textures**: AssetGen add-ons (Depth Anything V2 Small, BiRefNet, Real-ESRGAN).
