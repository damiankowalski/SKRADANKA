#!/usr/bin/env python3
"""Carries out the owner's verdicts from the listening room (Tools/AudioGen/README.md).

    .venv/bin/python promote.py first --dry-run     # what would happen
    .venv/bin/python promote.py first

out/<series>/feedback.json decides per clip:
  accept  the finished clip goes to DemoProject/Assets/Audio/Generated/<game name>_<NN>.wav with its provenance
          (<same>.json: prompt, seed, model, licence, checks, the owner's decision and note). A clip already promoted
          is not copied twice.
  change  nothing enters the game; the clip, its prompt and the owner's note are written to out/<series>/changes.json
          for the next series (the agent rewrites the prompt and generates new variants).
  reject  nothing.
A clip that failed the checks is never promoted. Clips without a verdict are listed and left alone.

    .venv/bin/python promote.py second --accept-as footstep_soft_1001=Footstep_Gravel

promotes a clip the owner asked to put to another use ("this sounds like gravel") under another game name, as
accepted, his note kept.
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = HERE / "out"
ASSETS = REPO / "DemoProject" / "Assets"
GENERATED = "Audio/Generated"  # relative to the project's asset folder: what scenes reference


def already_promoted(destination, series_name, file_name):
    """The promoted asset path of a clip from this series, if it is already there."""
    for meta in sorted(destination.glob("*.json")):
        try:
            data = json.loads(meta.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        source = data.get("source", {})
        if source.get("series") == series_name and source.get("file") == file_name:
            return meta.with_suffix(".wav")
    return None


def next_name(destination, game_name, taken):
    n = 1
    while True:
        name = f"{game_name}_{n:02d}"
        if name not in taken and not (destination / f"{name}.wav").exists():
            return name
        n += 1


def promote(series, assets=ASSETS, dry_run=False, accept_as=None):
    """Returns a report: {"promoted": [(clip, asset)], "kept": [...], "changes": [...], "rejected": [...],
    "refused": [(clip, why)], "unrated": [...]}."""
    with open(series / "series.json", encoding="utf-8") as f:
        clips = {c["file"][:-4]: c for c in json.load(f)["clips"]}
    feedback_file = series / "feedback.json"
    if not feedback_file.is_file():
        raise FileNotFoundError(f"{feedback_file} is missing: the owner has not listened yet (review.py {series.name})")
    with open(feedback_file, encoding="utf-8") as f:
        verdicts = json.load(f).get("clips", {})
    destination = assets / GENERATED
    accept_as = accept_as or {}
    unknown = sorted(set(accept_as) - set(clips))
    if unknown:
        raise ValueError(f"--accept-as names clips not in the series: {', '.join(unknown)}")
    report = {"promoted": [], "kept": [], "changes": [], "rejected": [], "refused": [], "unrated": []}
    taken = set()
    for stem, clip in clips.items():
        verdict = verdicts.get(stem)
        if verdict is None:
            report["unrated"].append(stem)
            continue
        decision, note = verdict.get("decision"), verdict.get("note", "")
        game_name = clip["game_name"]
        if stem in accept_as:
            decision, game_name = "accept", accept_as[stem]
        if decision == "reject":
            report["rejected"].append(stem)
        elif decision == "change":
            report["changes"].append({"clip": stem, "sound": clip["sound"], "seed": clip["seed"], "prompt": clip["prompt"],
                                      "note": note})
        elif decision == "accept":
            if not clip["checks"]["passed"]:
                report["refused"].append((stem, "failed the checks: " + "; ".join(clip["checks"]["failures"])))
                continue
            existing = already_promoted(destination, series.name, clip["file"]) if destination.is_dir() else None
            if existing:
                report["kept"].append((stem, f"{GENERATED}/{existing.name}"))
                continue
            name = next_name(destination, game_name, taken)
            taken.add(name)
            report["promoted"].append((stem, f"{GENERATED}/{name}.wav"))
            if dry_run:
                continue
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(series / clip["file"], destination / f"{name}.wav")
            provenance = {key: value for key, value in clip.items() if key not in ("file", "raw")}
            provenance["asset"] = f"{GENERATED}/{name}.wav"
            provenance["source"] = {"series": series.name, "file": clip["file"]}
            provenance["game_name"] = game_name
            provenance["owner"] = {"decision": "accepted" if game_name == clip["game_name"] else f"accepted as {game_name}",
                                   "note": note, "date": verdict.get("date")}
            (destination / f"{name}.json").write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + "\n",
                                                      encoding="utf-8")
        else:
            report["refused"].append((stem, f"unknown decision {decision!r}"))
    if report["changes"] and not dry_run:
        (series / "changes.json").write_text(json.dumps({"series": series.name, "changes": report["changes"]}, indent=2,
                                                        ensure_ascii=False) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("series", help="folder under out/")
    parser.add_argument("--dry-run", action="store_true", help="report only, copy nothing")
    parser.add_argument("--accept-as", action="append", default=[], metavar="CLIP=GAME_NAME",
                        help="promote a clip under another game name, as the owner's note asks")
    args = parser.parse_args()
    accept_as = {}
    for item in args.accept_as:
        clip, _, name = item.partition("=")
        if not clip or not name.replace("_", "").isalnum():
            parser.error(f"--accept-as wants CLIP=GAME_NAME, not {item!r}")
        accept_as[clip] = name
    try:
        report = promote(OUT / args.series, dry_run=args.dry_run, accept_as=accept_as)
    except (FileNotFoundError, ValueError) as error:
        sys.exit(str(error))
    verb = "would promote" if args.dry_run else "promoted"
    for clip, asset in report["promoted"]:
        print(f"{verb}  {clip} -> {asset}")
    for clip, asset in report["kept"]:
        print(f"already {clip} -> {asset}")
    for change in report["changes"]:
        print(f"change    {change['clip']}: {change['note']}")
    for clip in report["rejected"]:
        print(f"rejected  {clip}")
    for clip, why in report["refused"]:
        print(f"REFUSED   {clip}: {why}")
    if report["unrated"]:
        print(f"unrated   {', '.join(report['unrated'])}")
    if report["changes"] and not args.dry_run:
        print(f"Changes for the next series: {OUT / args.series / 'changes.json'}")
    sys.exit(1 if report["refused"] else 0)


if __name__ == "__main__":
    main()
