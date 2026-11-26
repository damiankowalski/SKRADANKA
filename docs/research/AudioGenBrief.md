# Brief: AudioGen, package A: effects and ambience, with a listening room

The owner's brief of 2026-11-26, translated from Polish (the Polish original goes into the package's PR description,
as the brief asks). **Approved; it starts once the first mission's world stands** (the owner, 2026-11-26:
`docs/research/MissionOne.md`, "F. Sound"). That supersedes the brief's own line "approved to start now, before F1 and
the materials": both are done. The model named here (Stable Audio 3 Small SFX) replaces Stable Audio Open Small in
`AITools.md`; stage 1 re-checks its model card.

Estimate: M (confirmed in stage 1) · one PR · branch `feature/audiogen` · Mac only.
Packages B (music: ACE-Step 1.5 against Stable Audio 3 Small Music) and C (voices: Chatterbox Multilingual + Whisper)
separately, after A.

## Goal

Local sound effects and ambience loops from Stable Audio 3 Small SFX (`stabilityai/stable-audio-3-small-sfx`, 433M,
up to 120 s, Stability AI Community License: the results are the owner's, commercial use up to USD 1M revenue a
year), with a review loop by the owner. The agent cannot hear: it runs objective checks, the owner judges the sound.

## From the owner's side

1. The agent generates a series: 3-4 variants per sound.
2. Automatic checks reject broken files before listening.
3. A listening room: a local page (localhost) with a player, the waveform, prompt, seed and check results; per
   variant ✓ accept / ✗ reject / ↻ change, with a note field. Ratings are saved to a feedback file in the series'
   folder. Fallback: the files in Finder.
4. The agent reads the feedback: accepted → `DemoProject/Assets/Audio` with a provenance file; "change" → new variants
   following the note; rejected → nothing.
5. A listening scene in the engine: footsteps on surfaces, doors from a distance, the night ambience with fog.

## House rules (as AssetGen)

* `Tools/AudioGen/`: pyproject + uv.lock, `.venv` ignored by git; no global pip, no shell changes.
* Weights in `HF_HOME` (`~/AI/huggingface`, shared); model and revision pinned; an allow-list.
* Ask the owner before every install, download and removal. `uninstall` as in AssetGen (by default `.venv` and
  `out/`; weights only with `--with-model`).
* Provenance file: prompt, seed, steps, model + revision, licence, date, "AI-generated", the owner's decision and
  notes. Only accepted sounds and their provenance enter the repository.
* One model at a time; a warning when PenumbraEditor is running.

## Automatic checks (thresholds set in stage 1)

Format matching `Assets/Audio`, length, clipping / true peak, loudness (LUFS) to the category's level (effect /
ambience), trimmed silence, mono for 3D sounds; for loops: seam smoothness (a measure + crossfade).

## Stage 1: proposal (no implementation), then STOP

Review `Tools/AssetGen` (the pattern), `Penumbra/src/Penumbra/Audio`, `Assets/Audio`, the Lua that plays sounds.
Bring:

* how to run the model on Apple Silicon (library, backend), time for 10 s and 60 s, peak RAM;
* the listening room's design (server, feedback format, how the agent reads it);
* check thresholds and target loudness levels;
* the starting list of sounds (proposed: footsteps on stone and wood, a door, a 60 s wind loop) and the listening
  scene;
* what the audio engine lacks (reverb, mixing etc.): a list with estimates only;
* the estimate confirmed (M) or a new one.

Wait for the owner's approval.

## Stage 2 (after approval)

* `Tools/AudioGen`: `setup.sh` (check/install/download/uninstall), generate, checks, review, promote.
* Skill `.claude/skills/penumbra-generate-audio` (when, how, the house rules, the feedback cycle).
* The listening scene in `DemoProject`.
* The first series and one full cycle with the owner's feedback.
* The tool's README; Roadmap: A done, B and C next.

## Not in this package

Music and voices (B, C); changes to the audio engine (a list only); changes to `NoiseLoudness` for the AI; models
outside the list; weights, tokens, `.venv` or `out/` in the repository.

## Acceptance criteria

1. `check` is readable; on Windows a message and an exit code other than 0.
2. A series works; the PR gives the time for 10 s and 60 s and the peak RAM on the M1 Pro 16 GB.
3. The checks have tests without the model (a broken test file is rejected).
4. The owner's ✓/✗/↻ with notes are saved and carried out correctly (the whole cycle once in the PR).
5. Accepted sounds play in the listening scene and in the game; tests green.
6. `uninstall --dry-run` shows everything with sizes; `git status` clean.
7. In the PR, in Polish: what was generated, what the owner accepted, measurements, limits, proposals with estimates.
