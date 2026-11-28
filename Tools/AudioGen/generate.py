#!/usr/bin/env python3
"""Generates a series of sounds with Stable Audio 3 Small SFX (Tools/AudioGen/README.md).

    .venv/bin/python generate.py footstep_stone door_creak --variants 3 --series first
    .venv/bin/python generate.py footstep_stone --variants 3 --seed 2000 --series first   # more of one sound
    .venv/bin/python generate.py --reprocess --series first                                # checks again, no model

A sound is a template in prompts/<sound>.json. Per variant the series folder out/<series>/ gets <sound>_<seed>.wav (as
the game would play it: trimmed or looped, mono for 3D effects, normalised), <sound>_<seed>_raw.wav (the model's own
output) and <sound>_<seed>.json (provenance and the checks). The listening room (review.py) plays the series.
"""

import argparse
import datetime
import json
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from audiogen import checks  # noqa: E402
from audiogen.model import config  # noqa: E402

PROMPTS = HERE / "prompts"
OUT = HERE / "out"


def load_template(sound):
    path = PROMPTS / f"{sound}.json"
    if not path.is_file():
        sys.exit(f"no template {path}")
    with open(path, encoding="utf-8") as f:
        template = json.load(f)
    if template["category"] not in checks.CATEGORIES:
        sys.exit(f"{sound}: unknown category {template['category']}")
    return template


def first_event(audio, rate, gap=0.08):
    """The first sound of a clip that holds several (one footstep out of a few): up to the first silent gap."""
    level = np.max(np.abs(audio), axis=0)
    threshold = 10 ** (checks.SILENCE_DB / 20)  # the step's decay counts as the step
    loud = level > threshold
    onsets = np.nonzero(loud)[0]
    if onsets.size == 0:
        return audio
    start, quiet, window = onsets[0], 0, int(gap * rate)
    for i in range(start, level.size):
        quiet = quiet + 1 if not loud[i] else 0
        if quiet >= window:
            return audio[:, :i - window + int(0.02 * rate)]
    return audio


def finish(raw, rate, template):
    audio = raw
    if template.get("first_event"):
        audio = first_event(checks.trim_silence(audio, rate), rate)
    return checks.prepare(audio, rate, template["category"], loop=template.get("loop", False), mono=template.get("mono"),
                          limiter=template.get("limiter", False))


def record(template, sound, seed, variation, prompt, settings, rate, wav, measures, failures, seconds_taken):
    model = config()["model"]
    return {
        "sound": sound,
        "file": wav.name,
        "raw": wav.with_name(wav.stem + "_raw.wav").name,
        "category": template["category"],
        "loop": template.get("loop", False),
        "game_name": template.get("game_name", sound),
        "prompt": prompt,
        "variation": variation,
        "seed": seed,
        "steps": settings["steps"],
        "cfg_scale": settings["cfg_scale"],
        "generated_seconds": template["generate_seconds"],
        "sample_rate": rate,
        "model": model["id"],
        "revision": model["revision"],
        "license": model["license"],
        "library": config()["library"],
        "date": datetime.date.today().isoformat(),
        "ai_generated": True,
        "generation_seconds": round(seconds_taken, 2),
        "checks": {"measures": measures, "failures": failures, "passed": not failures},
    }


def run(args):
    defaults = config()["defaults"]
    series = OUT / args.series
    series.mkdir(parents=True, exist_ok=True)
    if args.reprocess:
        return reprocess(series)
    from audiogen.model import Generator
    started = time.time()
    generator = Generator()
    print(f"model loaded in {time.time() - started:.1f} s", flush=True)
    settings = {"steps": args.steps or defaults["steps"], "cfg_scale": args.cfg_scale or defaults["cfg_scale"]}
    failed = 0
    for sound in args.sounds:
        template = load_template(sound)
        variations = template.get("variations") or [""]
        for n in range(args.variants):
            if args.only is not None and n != args.only:
                continue
            seed = args.seed + n
            variation = variations[n % len(variations)]
            prompt = template["prompt"].replace("{variation}", variation).strip()
            t0 = time.time()
            raw = generator.generate(prompt, template["generate_seconds"], seed, settings["steps"], settings["cfg_scale"])
            taken = time.time() - t0
            rate = generator.sample_rate
            wav = series / f"{sound}_{seed}.wav"
            checks.write_wav(wav.with_name(wav.stem + "_raw.wav"), np.clip(raw, -1, 1), rate)
            audio = finish(raw, rate, template)
            checks.write_wav(wav, audio, rate)
            measures, failures = checks.check(audio, rate, template["category"], template.get("length"), template.get("loop", False),
                                           template.get("lufs_range"))
            data = record(template, sound, seed, variation, prompt, settings, rate, wav, measures, failures, taken)
            wav.with_suffix(".json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
            failed += bool(failures)
            verdict = "ok" if not failures else "REJECTED: " + "; ".join(failures)
            print(f"{wav.stem}: {taken:.1f} s, {measures['seconds']} s long, {measures['lufs']} LUFS, "
                  f"{measures['true_peak_dbtp']} dBTP: {verdict}", flush=True)
    write_index(series)
    print(f"Series: {series}  (listen: .venv/bin/python review.py {args.series})")
    return 1 if failed else 0


def reprocess(series):
    """Redo the finished clips and the checks from the raw files (after a change to the processing), no model."""
    for meta in sorted(series.glob("*.json")):
        if meta.name in ("series.json", "feedback.json"):
            continue
        data = json.loads(meta.read_text(encoding="utf-8"))
        template = load_template(data["sound"])
        raw, rate = checks.read_wav(series / data["raw"])
        audio = finish(raw, rate, template)
        checks.write_wav(series / data["file"], audio, rate)
        measures, failures = checks.check(audio, rate, template["category"], template.get("length"), template.get("loop", False),
                                           template.get("lufs_range"))
        data["checks"] = {"measures": measures, "failures": failures, "passed": not failures}
        meta.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        print(f"{meta.stem}: {'ok' if not failures else 'REJECTED: ' + '; '.join(failures)}")
    write_index(series)
    return 0


def write_index(series):
    clips = []
    for meta in sorted(series.glob("*.json")):
        if meta.name not in ("series.json", "feedback.json"):
            clips.append(json.loads(meta.read_text(encoding="utf-8")))
    (series / "series.json").write_text(json.dumps({"series": series.name, "clips": clips}, indent=2) + "\n", encoding="utf-8")


def main():
    defaults = config()["defaults"]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sounds", nargs="*", help="templates in prompts/ (without .json)")
    parser.add_argument("--series", default=datetime.datetime.now().strftime("%Y%m%d-%H%M"), help="folder under out/")
    parser.add_argument("--variants", type=int, default=defaults["variants"])
    parser.add_argument("--seed", type=int, default=defaults["seed"], help="the seed of variant n is seed + n")
    parser.add_argument("--only", type=int, default=None, help="only variant n")
    parser.add_argument("--steps", type=int, default=0)
    parser.add_argument("--cfg-scale", type=float, default=0.0)
    parser.add_argument("--reprocess", action="store_true", help="checks and finished clips again from the raw files")
    args = parser.parse_args()
    if not args.sounds and not args.reprocess:
        parser.error("name at least one sound")
    sys.exit(run(args))


if __name__ == "__main__":
    main()
