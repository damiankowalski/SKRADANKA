---
name: penumbra-generate-audio
description: Generate sound effects (footsteps, doors, impacts) and ambience loops (wind, rain, night) locally with AudioGen (Stable Audio 3 Small SFX) on the owner's Apple Silicon Mac, run the listening room for the owner's verdicts and promote accepted sounds into DemoProject. Use when asked for a sound, an effect or an ambience, or to act on the owner's listening feedback.
---

# Generate sounds with AudioGen

The tool, its files, checks and limits: `Tools/AudioGen/README.md`. Read it first.

## When

* A sound effect or an ambience loop the game needs, or another try at one the owner asked to change.
* Only on the owner's Mac with Apple Silicon. On Windows or Linux, say that generation is Mac-only; the checks and
  their tests run anywhere.
* Not music (package B) or voices (package C): those have their own briefs. Not engine audio features (reverb,
  mixing): a list with estimates for the owner, not part of this tool.

## Housekeeping rules (the owner's Mac)

1. Ask the owner before any command that installs, downloads or deletes something on the Mac: `setup.sh install`,
   `login`, `download`, `uninstall` (without `--dry-run`), `uv sync`, `hf download`. `setup.sh check`,
   `uninstall --dry-run`, `generate.py`, `review.py` and `promote.py --dry-run` change nothing outside
   `Tools/AudioGen/out` and need no permission. `promote.py` writes into `DemoProject/Assets/Audio/Generated`.
2. Never `pip install` globally, touch the system Python or edit `~/.zshrc`. Everything lives in
   `Tools/AudioGen/.venv` (uv, `uv.lock`) and `HF_HOME` (default `~/AI/huggingface`).
3. The weights and the Hugging Face token are shared with the owner's other tools. If `check` sees them, do not
   log in or download again. Never print, copy or commit the token. Never remove the model unless the owner asks.
4. Only `stabilityai/stable-audio-3-small-sfx` at the revision in `config.json`. A different model, revision or
   library commit is a deliberate change in its own PR.
5. One model at a time: close `PenumbraEditor` (or ask the owner to) and do not run AssetGen at the same time.
6. Only accepted sounds and their provenance enter the repository: never `out/`, `.venv`, weights or the token.

## How

```sh
Tools/AudioGen/setup.sh check                                   # all "ok"? then generate
cd Tools/AudioGen
.venv/bin/python generate.py door_creak wind_loop --variants 4 --series <name>
.venv/bin/python review.py <name>                               # the owner listens (http://127.0.0.1:8765)
.venv/bin/python promote.py <name> --dry-run && .venv/bin/python promote.py <name>
```

* A sound is a template in `prompts/<sound>.json` (README.md, "Templates"); copy the closest one. Describe the
  sound plainly: source, material, place, distance, and end with "no music, no voices". Never name games, films,
  studios or living people.
* You cannot hear. Read the checks (`<sound>_<seed>.json`): a REJECTED variant has a reason (too quiet, a seam, a
  silence before the sound). Fix the processing or the template, `--reprocess` or regenerate, and never hand the
  owner a series with failures you could fix. Say what the checks cannot tell (timbre, realism, fitting the world).
* The owner's verdicts are in `out/<series>/feedback.json`. Carry them out with `promote.py`: accepted clips go to
  `DemoProject/Assets/Audio/Generated/` with provenance; for each "change", read the note in `changes.json`,
  rewrite the template's prompt or variations along it, and generate a new series with new seeds; rejected clips
  are dropped. Then wire the accepted sounds into the listening scene and the game (`Scripts/AI/Noises.lua`,
  `Tools/BuildDemoProject.py`), and commit them with their `.json`.
* Report to the owner in Polish: what was generated, the measures, what failed and why, the link to the room.
