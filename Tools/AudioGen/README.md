# AudioGen: local sound effects and ambience

Generates sound effects (footsteps, doors) and ambience loops (night wind) on an Apple Silicon Mac with
**Stable Audio 3 Small SFX** (Stability AI, 433M parameters, Stability AI Community License) through the
`stable-audio-3` library (MIT) on PyTorch. One command loads the model, generates a series, checks it and exits.
The agent cannot hear: objective checks reject broken clips, and the owner judges the rest in a listening room.
Agents: `.claude/skills/penumbra-generate-audio`. The brief: `docs/research/AudioGenBrief.md`.

## What lives where

| What | Where | Size | In git |
|---|---|---|---|
| The tool: `generate.py`, `review.py`, `promote.py`, `audiogen/`, `prompts/`, `config.json`, `setup.sh`, tests | `Tools/AudioGen/` | small | yes |
| Pinned dependencies | `pyproject.toml`, `uv.lock` | small | yes |
| Python environment (Python 3.12 from uv, stable-audio-3 at a pinned commit, PyTorch 2.7.1) | `Tools/AudioGen/.venv` | ~0.6 GB | no (ignored) |
| Series: finished clips, raw model output, provenance, the owner's feedback | `Tools/AudioGen/out/<series>/` | ~1 MB per effect, ~20 MB per 60 s loop | no (ignored) |
| Accepted sounds and their provenance | `DemoProject/Assets/Audio/Generated/` | 16-bit WAV | yes |
| Model weights, pinned revision | `$HF_HOME/hub` (default `~/AI/huggingface`) | ~3.5 GB | never |
| Hugging Face token | `$HF_HOME/token` | - | never |

`HF_HOME` is shared with AssetGen and other tools. The model is gated: the Hugging Face account behind the token
must have accepted its terms on huggingface.co.

## Licence

The Stability AI Community License: the generated sounds are the owner's; commercial use is free up to USD 1M
revenue a year, above that an enterprise licence is needed (stability.ai/license). Its text encoder (T5Gemma) comes
with the Gemma Terms of Use. Every promoted sound carries the model, revision and licence in its provenance file.

## Setup

```sh
Tools/AudioGen/setup.sh check        # changes nothing: Apple Silicon, uv, .venv, token, model, disk, the editor
Tools/AudioGen/setup.sh install      # .venv from uv.lock (asks first)
Tools/AudioGen/setup.sh login        # hf auth login into HF_HOME; skipped if a token is there
Tools/AudioGen/setup.sh download     # the pinned revision into HF_HOME; skipped if it is there (asks first)
```

On Windows or Linux `setup.sh` says that generation needs Apple Silicon and exits with 1; the tests still run.
Close `PenumbraEditor` before generating on a 16 GB Mac.

## The cycle

1. **Generate** a series: 3-4 variants per sound, each a different seed and a different line of the template's
   `variations`.

   ```sh
   cd Tools/AudioGen
   .venv/bin/python generate.py footstep_stone footstep_wood door_creak wind_loop --variants 4 --series first
   .venv/bin/python generate.py door_creak --variants 3 --seed 2000 --series second   # new seeds
   ```

   Per variant `out/<series>/` gets `<sound>_<seed>.wav` (as the game plays it), `<sound>_<seed>_raw.wav` (the
   model's own output) and `<sound>_<seed>.json` (provenance and the checks), plus `series.json`. A variant that
   fails a check is marked REJECTED and the run exits with 1; it stays in the series so the owner can hear why.
   After a change to the processing, `generate.py --reprocess --series first` redoes the finished clips and the
   checks from the raw files without the model.

2. **Listen**: `.venv/bin/python review.py first` opens `http://127.0.0.1:8765` (local only). Per clip: the
   finished clip and the raw one, the waveform (click to play from there), prompt, seed, the checks; ✓ accept,
   ✗ reject or ↻ change with a note (a change needs one). Every click saves `out/<series>/feedback.json`:

   ```json
   {"series": "first", "updated": "2026-11-28T08:10:00",
    "clips": {"door_creak_1001": {"decision": "change", "note": "slower, less squeak", "date": "..."}}}
   ```

   Without a browser: the files in Finder, and `feedback.json` written by hand in the same shape.

3. **Carry out** the verdicts: `.venv/bin/python promote.py first --dry-run`, then without `--dry-run`.
   * accept: the finished clip goes to `DemoProject/Assets/Audio/Generated/<game name>_<NN>.wav` with
     `<same>.json` (prompt, seed, steps, model + revision, licence, date, `ai_generated`, the checks, the source
     series and the owner's decision and note). Promoting twice copies nothing twice. A clip that failed the
     checks is never promoted.
   * change: `out/<series>/changes.json` lists the clip, its prompt and the note. The agent rewrites the template's
     prompt or variations along the note and generates a new series.
   * reject: nothing.

4. **Use** the sounds: scenes and scripts reference `Audio/Generated/...` (the listening scene first,
   `Scenes/Listening.pscene`), and the commit carries the WAV and its provenance.

## Templates

A sound is `prompts/<sound>.json`: `category` (`effect` or `ambience`), `prompt` with `{variation}`, `variations`
(one per variant), `generate_seconds` (what the model is asked for), `length` (the finished clip's allowed range),
`game_name` (the asset's name), and for effects `mono` and `first_event` (keep only the first sound of a clip:
one footstep out of the few the model makes), for ambience `loop`. Describe sounds plainly (material, place,
distance, "no music, no voices"); never name games, films, studios or living people.

## Checks

The finished clip must pass, per category (`audiogen/checks.py`):

| | effect (3D, `audio.play_at`) | ambience (2D loop) |
|---|---|---|
| Channels | mono | stereo |
| Level | peak-normalised to -3 dBTP; loudness -36 to -12 LUFS | -30 LUFS (-33 to -27) |
| True peak (4x oversampled) | ≤ -1 dBTP | ≤ -3 dBTP |
| Silence before the sound | ≤ 0.05 s (trimmed, 3 ms fade in, 20 ms fade out) | ≤ 0.5 s |
| Loop | - | 2 s equal-power crossfade; the seam's jump ≤ 6 dB over the clip's own motion |

Always: no clipped samples, not silent (peak above -55 dBFS), the template's length range, 16-bit PCM WAV at
44.1 kHz (the engine plays any rate; the old procedural sounds are 22.05 kHz). Loudness is ITU-R BS.1770-4
(K-weighting, 400 ms blocks, absolute and relative gates); clips shorter than one block are measured ungated.
Effects are levelled by their peak because integrated loudness says little about a 0.1 s step; the game's
`AudioSource.Volume` and `audio.play_at` volume set the mix.

## Measurements (M1 Pro, 16 GB, CPU)

| | |
|---|---|
| Loading the model | 11.5 s |
| A 10 s clip, 8 steps | 3.9 s |
| A 60 s clip, 8 steps | 8.8 s |
| Peak memory | 4.5 GB |

The model runs on the CPU (`Generator(device="cpu")`); a series of 16 clips takes a couple of minutes.

## Tests

`Tools/Test.sh` / `Tools\Test.bat` run `Tools.AudioGen` (CTest) when `uv` is installed: BS.1770 loudness (a full
scale 997 Hz sine is -3.01 LUFS), true peak between samples, every rejection (silent, clipped, stereo effect,
leading silence, length, loop seam), broken and missing files, the first-event cut, the listening room's server
(saves verdicts, serves nothing outside the series), promotion (accept, change, reject, refused, twice) and the
model allow-list. numpy and scipy only, no model (CI runs them on Windows and macOS). By hand:
`uv run --project Tools/AudioGen --frozen python -m unittest discover -s Tools/AudioGen/tests`.

## Updating

Only deliberately, in a PR of its own: the library's commit in `pyproject.toml` and `config.json`, `uv lock`,
`setup.sh install`, rerun a series and compare. `audiogen/model.py` loads the pinned snapshot itself (the
library's own `from_pretrained` fetches the latest revision) and points the text encoder at the same snapshot with
the Hub offline, so a new library version can break it. A new model revision: change `revision`, `setup.sh
download`, compare. A new model: add it to `allowed_models` with its licence; non-commercial models never.

## Removing everything

```sh
Tools/AudioGen/setup.sh uninstall --dry-run            # lists everything with sizes, removes nothing
Tools/AudioGen/setup.sh uninstall                      # .venv and out/ only (asks first)
Tools/AudioGen/setup.sh uninstall --with-model         # also the model in HF_HOME (shared: other tools lose it)
```

uv's cache and Python 3.12 are shared with AssetGen: `Tools/AssetGen/setup.sh uninstall --with-uv-cache`. The token
is never removed by the script (Tools/AssetGen/README.md, "Removing everything").
