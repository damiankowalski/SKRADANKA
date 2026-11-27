#!/usr/bin/env python3
"""Frame-time benchmark: the playtester's bot walks a route in a window while the runtime logs its frame statistics
(PenumbraRuntime --perf-out, twice a second); the report gives the frame time, the GPU time, the draw calls and the
lamps left without a shadow map for every step of the route (docs/Playtest.md, "Benchmark").

    python Tools/Playtest/benchmark.py --scene Scenes/Mission1.pscene --route Playtest/Mission1/BackLanes
    python Tools/Playtest/benchmark.py report out/benchmark/Mission1/20261127-0300      # the report again

Needs a display (the frames are measured in a window) and a development build (release by default), whose testing
tools write the log. Standard library only; Windows and macOS.
"""

import argparse
import datetime
import json
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from playtest import FRAME, MAX_FRAMES, ROOT, runtime_path  # noqa: E402


def commit():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    except OSError:
        return "?"


def run(args):
    level = Path(args.scene).stem
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = Path(args.out) if args.out else ROOT / "out" / "benchmark" / level / stamp
    out.mkdir(parents=True, exist_ok=True)
    values = f"playtest={args.route},playtest_seed={args.seed}" + (f",{args.set}" if args.set else "")
    command = [str(runtime_path(args.config)), "--project", str(ROOT / "DemoProject"), "--scene", args.scene,
               "--fixed-timestep", FRAME, "--seed", str(args.seed), "--set", values,
               "--playtest-out", str(out / "run.jsonl"), "--perf-out", str(out / "perf.jsonl"), "--frames", str(MAX_FRAMES)]
    if args.width:
        command += ["--width", str(args.width), "--height", str(args.height)]
    if not args.vsync:
        command.append("--no-vsync")
    if args.notarget:
        command += ["--debug-ui", "notarget"]
    print(f"Benchmarking {args.scene} along {args.route} in a window (the bot plays; this takes minutes)")
    started = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    info = {"scene": args.scene, "route": args.route, "notarget": args.notarget, "command": command, "exit": result.returncode, "wall": round(time.monotonic() - started, 1),
            "platform": platform.platform(), "machine": platform.machine(), "commit": commit(), "config": args.config}
    (out / "series.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    if result.returncode != 0:
        errors = [line for line in (result.stdout + result.stderr).splitlines() if "[error]" in line or "[critical]" in line]
        print(f"The runtime exited with {result.returncode}:", *errors[:5], sep="\n  ")
    report(out)


def read_lines(path):
    if not path.is_file():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def median(values):
    return statistics.median(values) if values else None


def fmt(value, digits=1):
    return "-" if value is None else f"{value:.{digits}f}"


def summarise(lines):
    """The numbers of a stretch of the run: medians of the half-second averages, the worst frame."""
    gpu = [line["gpu_ms"] for line in lines if line.get("gpu_ms") is not None]
    return {"frame": median([line["frame_ms"] for line in lines]), "worst": max((line["frame_ms_max"] for line in lines), default=None),
            "gpu": median(gpu), "update": median([line["update_ms"] for line in lines]),
            "render": median([line["render_ms"] for line in lines]), "draws": median([line["draws"] for line in lines]),
            "left_out": max((line["left_out_lamps"] for line in lines), default=None)}


def report(out):
    out = Path(out)
    perf = [line for line in read_lines(out / "perf.jsonl") if line.get("frames", 0) > 0]
    run_lines = read_lines(out / "run.jsonl")
    info = json.loads((out / "series.json").read_text(encoding="utf-8")) if (out / "series.json").is_file() else {}
    head = next((line for line in run_lines if line.get("type") == "run"), {})
    result = next((line for line in run_lines if line.get("type") == "result"), {})
    segments = [line for line in run_lines if line.get("type") == "segment"]
    if not perf:
        sys.exit(f"no frame statistics in {out / 'perf.jsonl'}")

    rows = []
    for segment in segments:
        inside = [line for line in perf if segment["t0"] <= line["t"] < segment["t1"]]
        if inside:
            rows.append((segment["index"], segment["label"], summarise(inside)))
    whole = summarise(perf)
    frames = sorted(line["frame_ms"] for line in perf)
    p95 = frames[min(len(frames) - 1, int(len(frames) * 0.95))]
    budget = sum(1 for value in frames if value > 1000.0 / 60.0)

    lines = [f"# Benchmark: {info.get('route', head.get('route', '?'))} on {info.get('scene', '?')}", "",
             f"{datetime.datetime.now().isoformat(timespec='seconds')} · commit `{info.get('commit', '?')}` · {info.get('platform', '?')} · "
             f"build {info.get('config', '?')} · {len(perf)} half-second samples over {fmt(perf[-1]['t'] - perf[0]['t'], 0)} s of game, "
             f"run {result.get('outcome', 'without a result')}", "",
             "Frame time is the wall clock between frames (no vsync), GPU the GPU's own time; values are medians of the "
             "half-second averages, \"worst\" the slowest single frame. Budget: 16.7 ms (60 FPS).", "",
             "## The whole run", "",
             "| Frame (median) | Frame (95%) | Worst frame | GPU | Game update | CPU render | Draw calls | Lamps left out (max) | Samples over budget |",
             "|---|---|---|---|---|---|---|---|---|",
             f"| {fmt(whole['frame'])} ms | {fmt(p95)} ms | {fmt(whole['worst'])} ms | {fmt(whole['gpu'])} ms | {fmt(whole['update'])} ms | "
             f"{fmt(whole['render'])} ms | {fmt(whole['draws'], 0)} | {fmt(whole['left_out'], 0)} | {budget} of {len(frames)} |", "",
             "## Step by step", "",
             "| Step | Frame | Worst | GPU | Draw calls | Lamps left out |", "|---|---|---|---|---|---|"]
    for index, label, numbers in rows:
        lines.append(f"| {index}. {label} | {fmt(numbers['frame'])} ms | {fmt(numbers['worst'])} ms | {fmt(numbers['gpu'])} ms | "
                     f"{fmt(numbers['draws'], 0)} | {fmt(numbers['left_out'], 0)} |")
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Frame {fmt(whole['frame'])} ms median, {fmt(p95)} ms at 95%, GPU {fmt(whole['gpu'])} ms, "
          f"{fmt(whole['draws'], 0)} draws; {budget} of {len(frames)} samples over 16.7 ms")
    print(f"Report: {out / 'report.md'}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "report":
        if len(sys.argv) != 3:
            sys.exit("usage: benchmark.py report <folder>")
        report(sys.argv[2])
        return
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", required=True, help="scene path under the project's Assets")
    parser.add_argument("--route", required=True, help="playtester route module, e.g. Playtest/Mission1/BackLanes")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--set", default="", help="more game values, e.g. difficulty=Hard")
    parser.add_argument("--config", default="release")
    parser.add_argument("--width", type=int, default=0, help="window width (default: the project's)")
    parser.add_argument("--height", type=int, default=0)
    parser.add_argument("--vsync", action="store_true", help="wait for vertical blank (default: off, to measure)")
    parser.add_argument("--notarget", action="store_true",
                        help="the guards ignore the thief: the route is walked to its end whatever the level's light")
    parser.add_argument("--out", default="", help="output folder (default: out/benchmark/<level>/<date-time>)")
    run(parser.parse_args())


if __name__ == "__main__":
    main()
