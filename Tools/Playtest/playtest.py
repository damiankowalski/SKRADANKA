#!/usr/bin/env python3
"""The agent playtester's runner (docs/Playtest.md).

Plays a level headless along one or more routes, once per seed, each run in its own PenumbraRuntime process
(--no-render, fixed 60 Hz timestep), and writes a Markdown report next to the runs' JSONL:

    python Tools/Playtest/playtest.py --scene Scenes/Stealth.pscene --routes Playtest/Stealth --seeds 20
    python Tools/Playtest/playtest.py --scene Scenes/PerceptionLab.pscene --routes Playtest/PerceptionLab/LitFront
    python Tools/Playtest/playtest.py report out/playtest/Stealth/20261122-2300     # report again from runs

Routes: a folder under the project's Assets (every .lua in it), a single route module, or "baseline" (no file:
the shortest navmesh path, crouched, nearest loot first). Without --routes: baseline plus the level's folder
Playtest/<Level>. Standard library only; Windows and macOS.
"""

import argparse
import concurrent.futures
import datetime
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RANK = {"Unaware": 0, "Suspicious": 1, "Investigating": 2, "Searching": 3, "Alarmed": 4}
FRAME = "0.0166667"
MAX_FRAMES = 60 * 900  # the bot quits on its own; this only stops a run that never ends


# ---------------------------------------------------------------------------------------------- running

def runtime_path(config):
    exe = "PenumbraRuntime.exe" if os.name == "nt" else "PenumbraRuntime"
    return ROOT / "build" / config / "bin" / exe


def find_routes(project, spec, scene):
    """Route modules (asset paths without .lua) from --routes values."""
    if not spec:
        level = Path(scene).stem
        spec = ["baseline", f"Playtest/{level}"]
    routes = []
    assets = project / "Assets"
    for item in spec:
        if item == "baseline":
            routes.append("baseline")
            continue
        path = assets / item
        if path.is_dir():
            routes += sorted(str(p.relative_to(assets).with_suffix("")).replace("\\", "/") for p in path.glob("*.lua"))
        elif path.with_suffix(".lua").is_file():
            routes.append(item)
        else:
            sys.exit(f"route not found: {item} (looked for {path} and {path}.lua)")
    return routes


def run_one(args, project, route, seed, out_file):
    values = f"playtest={route},playtest_seed={seed}"
    if args.set:
        values += "," + args.set
    command = [str(runtime_path(args.config)), "--project", str(project), "--scene", args.scene, "--headless", "--no-render",
               "--fixed-timestep", FRAME, "--seed", str(seed), "--set", values, "--playtest-out", str(out_file),
               "--frames", str(MAX_FRAMES)]
    started = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    errors = [line for line in (result.stdout + result.stderr).splitlines() if "[error]" in line or "[critical]" in line]
    return {"route": route, "seed": seed, "file": out_file, "exit": result.returncode, "errors": errors[:5],
            "wall": time.monotonic() - started}


def run_series(args):
    project = Path(args.project).resolve()
    exe = runtime_path(args.config)
    if not exe.exists():
        sys.exit(f"{exe} not found: build it first (Tools/Build.sh {args.config} or Tools\\Build.bat {args.config})")
    routes = find_routes(project, args.routes, args.scene)
    level = Path(args.scene).stem
    out = Path(args.out) if args.out else ROOT / "out" / "playtest" / level / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    seeds = list(range(args.first_seed, args.first_seed + args.seeds))
    jobs = []
    for route in routes:
        folder = out / "runs" / route.replace("/", "_")
        folder.mkdir(parents=True, exist_ok=True)
        jobs += [(route, seed, folder / f"seed{seed:03d}.jsonl") for seed in seeds]

    print(f"{len(routes)} route(s) x {len(seeds)} seed(s) = {len(jobs)} runs on {level}, {args.jobs} at a time")
    started = time.monotonic()
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, args, project, route, seed, file) for route, seed, file in jobs]
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            results.append(future.result())
            print(f"\r  {done}/{len(jobs)}", end="", flush=True)
    wall = time.monotonic() - started
    print(f"\r  {len(jobs)} runs in {wall:.1f} s")

    meta = {"scene": args.scene, "level": level, "routes": routes, "seeds": seeds, "jobs": args.jobs, "config": args.config,
            "set": args.set, "wall_seconds": round(wall, 1), "platform": f"{platform.system()} {platform.machine()}",
            "commit": git_commit(), "date": datetime.datetime.now().isoformat(timespec="seconds"),
            "runs": [{k: (str(v) if isinstance(v, Path) else v) for k, v in r.items()} for r in results]}
    (out / "series.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    report = write_report(out)
    print(f"Report: {report}")


def git_commit():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    except OSError:
        return "?"


# ---------------------------------------------------------------------------------------------- reading runs

def load_run(path):
    run = {"file": path, "events": [], "segments": [], "samples": [], "noises": [], "exposures": [], "uses": [], "doors": [],
           "head": {}, "result": None}
    try:
        lines = Path(path).read_text(encoding="utf-8").splitlines()
    except OSError:
        return run
    for line in lines:
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        kind = item.get("type")
        if kind == "run":
            run["head"] = item
        elif kind == "result":
            run["result"] = item
        elif kind in ("event", "segment", "sample", "noise", "exposure", "use", "door"):
            run[{"event": "events", "segment": "segments", "sample": "samples", "noise": "noises", "exposure": "exposures",
                 "use": "uses", "door": "doors"}[kind]].append(item)
    return run


def clock(seconds):
    if seconds is None:
        return "-"
    seconds = float(seconds)
    return f"{int(seconds // 60)}:{seconds % 60:04.1f}"


def where(item):
    """The route step and the nearest light (its distance varies run to run: shown with the medians)."""
    label = item.get("label") or f"step {item.get('step')}"
    light = item.get("light")
    return f"{label} (nearest light: {light})" if light else label


def alarm_chain(run):
    """The events of the guard who raised the alarm, from his last Unaware state to Alarmed."""
    result = run["result"] or {}
    guard = result.get("alarmed")
    if not guard:
        return []
    chain = []
    for event in run["events"]:
        if event["guard"] != guard:
            continue
        if event["from"] == "Unaware":
            chain = []
        chain.append(event)
        if event["to"] == "Alarmed":
            break
    return chain


# Causes the thief makes himself (Scripts/AI/Noises.lua and the hands in Player.lua). World events (a pot in the
# kitchen, a storm, the dogs) and news passed on (shouts, bells, reports) are not.
THIEF_CAUSES = ("Seen", "Touch", "Footstep", "Landing", "Blackjack", "Rustle", "BodyDrop", "Bow", "Switch", "DoorOpen", "DoorShut",
                "Unlock", "Pick", "Theft", "Attack", "Body", "Impact")


def thief_caused(event):
    cause = event.get("cause") or ""
    return event.get("kind") == "Seen" or any(cause.startswith(prefix) for prefix in THIEF_CAUSES)


def root_cause(run, chain):
    """What set it off: the first guard in the run who noticed the thief himself (saw, heard or touched him), before
    the alarm. Shouts, bells and reports only pass the news on."""
    if not chain:
        return None
    alarm_time = chain[-1]["t"]
    for event in run["events"]:
        if event["t"] > alarm_time:
            break
        if RANK[event["to"]] > RANK[event["from"]] and thief_caused(event):
            return event
    return chain[0]


def pct(n, total):
    return f"{100.0 * n / total:.0f}%" if total else "-"


# ---------------------------------------------------------------------------------------------- report

def write_report(out):
    out = Path(out)
    meta = json.loads((out / "series.json").read_text(encoding="utf-8")) if (out / "series.json").exists() else {}
    by_route = defaultdict(list)
    for file in sorted((out / "runs").glob("*/*.jsonl")):
        run = load_run(file)
        by_route[run["head"].get("route_module") or file.parent.name].append(run)

    lines = [f"# Playtest: {meta.get('level', out.name)}", ""]
    lines.append(f"{meta.get('date', '')} · commit `{meta.get('commit', '?')}` · {meta.get('platform', '')} · build {meta.get('config', '')}"
                 f" · {len(meta.get('seeds', []))} seeds · {meta.get('wall_seconds', '?')} s wall clock with {meta.get('jobs', '?')} jobs")
    if meta.get("set"):
        lines.append(f"Extra game values: `{meta['set']}`")
    lines += ["", "Results are reproducible on the same machine and build (docs/Playtest.md). Succeeded: escaped through the exit",
              "(or, for a lab scenario, stood it out undetected). A route that succeeds in 10-90% of seeds is **luck-sensitive**:",
              "a methodical stealth game should not depend on the seed.", ""]

    # Overview.
    lines += ["## Overview", "", "| Route | Succeeded | Detected | Other | Time (escaped) min / mean / max | Verdict |", "|---|---|---|---|---|---|"]
    for route, runs in by_route.items():
        outcomes = Counter((r["result"] or {}).get("outcome", "no result") for r in runs)
        total = len(runs)
        escaped = outcomes.get("escaped", 0) + outcomes.get("undetected", 0)
        detected = outcomes.get("detected", 0)
        other = ", ".join(f"{k} {v}" for k, v in sorted(outcomes.items()) if k not in ("escaped", "undetected", "detected"))
        times = [r["result"]["t"] for r in runs if r["result"] and r["result"]["outcome"] == "escaped"]
        span = f"{clock(min(times))} / {clock(statistics.mean(times))} / {clock(max(times))}" if times else "-"
        rate = escaped / total if total else 0
        if any(k in outcomes for k in ("stuck", "error", "no result")):
            verdict = "tool problem: see below"
        elif rate >= 0.9:
            verdict = "passes reliably"
        elif rate <= 0.1:
            verdict = "fails reliably" if detected else "does not get through"
        else:
            verdict = "**luck-sensitive**"
        name = runs[0]["head"].get("route", route)
        lines.append(f"| {name} (`{route}`) | {escaped}/{total} | {detected}/{total} | {other or '-'} | {span} | {verdict} |")
    lines.append("")

    for route, runs in by_route.items():
        lines += route_section(route, runs)

    report = out / "report.md"
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report


def route_section(route, runs):
    total = len(runs)
    name = runs[0]["head"].get("route", route)
    lines = [f"## {name}", "", f"`{route}`, {total} runs.", ""]

    # Detections: where the alarm chain started and where it ended.
    detected = [r for r in runs if r["result"] and r["result"]["outcome"] == "detected"]
    if detected:
        places = Counter()
        examples = {}
        for run in detected:
            chain = alarm_chain(run)
            if not chain:
                continue
            first, last = root_cause(run, chain), chain[-1]
            key = (first["guard"], first.get("cause") or first.get("kind"), where(first), last["guard"])
            places[key] += 1
            examples.setdefault(key, []).append((run["head"].get("seed"), first, last))
        lines += ["### Detections", "",
                  "Who noticed the thief first in runs that ended in an alarm (by sight, hearing or touch), where and why, and who then",
                  "raised the alarm (the same guard, or one his shout or a bell reached):", "",
                  "| Noticed by | Cause | Where | Runs | Typical: distance, B, light gem, crouched | Alarm raised by | First notice → alarm |",
                  "|---|---|---|---|---|---|---|"]
        for key, count in places.most_common():
            guard, cause, place, alarmed = key
            sample = examples[key]
            dist = statistics.median(f["distance"] for _, f, _ in sample)
            b = statistics.median(f["b"] for _, f, _ in sample)
            gem = statistics.median(f["gem"] for _, f, _ in sample)
            crouched = sum(1 for _, f, _ in sample if f.get("crouched")) * 100 // len(sample)
            span = statistics.median(l["t"] - f["t"] for _, f, l in sample)
            seeds = ", ".join(str(s) for s, _, _ in sample[:8])
            lines.append(f"| {guard} | {cause} | {place} at {clock(statistics.median(f['t'] for _, f, _ in sample))} | {count} (seeds {seeds}) "
                         f"| {dist:.1f} m, B {b:.2f}, gem {gem:.2f}, {crouched}% crouched | {alarmed} | {span:.2f} s |")
        lines.append("")

    # Disturbances that did not end in an alarm (in every run).
    disturbances = Counter()
    for run in runs:
        chain_ids = {id(e) for e in alarm_chain(run)}
        seen = set()
        for event in run["events"]:
            if id(event) in chain_ids or RANK[event["to"]] <= RANK[event["from"]]:
                continue
            key = (event["guard"], event.get("cause") or event.get("kind"), event["to"], event.get("label") or "")
            if key not in seen:
                seen.add(key)
                disturbances[key] += 1
    if disturbances:
        lines += ["### Other alerts (no alarm from them)", "", "| Guard | Cause | Rose to | During | Runs |", "|---|---|---|---|---|"]
        for (guard, cause, to, label), count in disturbances.most_common(12):
            lines.append(f"| {guard} | {cause} | {to} | {label} | {count}/{total} |")
        lines.append("")

    # Margins per segment: how close each guard came, over all runs.
    margins = defaultdict(lambda: {"b": [], "strength": [], "noticed": 0, "heard": 0, "runs": 0})
    order = []
    for run in runs:
        for segment in run["segments"]:
            for guard, s in segment["guards"].items():
                key = (segment["index"], segment["label"], guard)
                if key not in margins:
                    order.append(key)
                m = margins[key]
                m["runs"] += 1
                m["b"].append(s["max_b"])
                m["strength"].append(s["max_strength"])
                m["noticed"] += RANK.get(s["peak_state"], 0) > 0
                m["heard"] += s["max_heard"] > 0
    close = [(k, m) for k, m in margins.items() if max(m["b"]) >= 0.5 or m["heard"] or m["noticed"]]
    close.sort(key=lambda km: (-max(km[1]["strength"]), -max(km[1]["b"])))
    if close:
        lines += ["### Narrowest margins", "",
                  "Per route step and guard. Visual margin: 1 - B (below 0 = registered). Awareness: how far the strongest reaction stayed",
                  "below the alarm (10 = never noticed). Heard: runs in which a footstep reached the guard.", "",
                  "| Step | Guard | Peak B (median / max) | Visual margin (worst) | Awareness margin (worst) | Noticed | Heard |",
                  "|---|---|---|---|---|---|---|"]
        for (index, label, guard), m in close[:10]:
            lines.append(f"| {index}. {label} | {guard} | {statistics.median(m['b']):.2f} / {max(m['b']):.2f} | {1 - max(m['b']):.2f} "
                         f"| {10 - max(m['strength']):.1f} | {m['noticed']}/{m['runs']} | {m['heard']}/{m['runs']} |")
        lines.append("")

    # The closest call of each run that got away.
    escaped = [r for r in runs if r["result"] and r["result"]["outcome"] in ("escaped", "undetected")]
    calls = []
    for run in escaped:
        best = None
        for sample in run["samples"]:
            for guard in sample["guards"]:
                if best is None or guard["b"] > best[0]:
                    best = (guard["b"], sample["t"], guard["name"], sample.get("step"), guard["state"])
        if best and best[0] > 0:
            calls.append((best, run["head"].get("seed")))
    if calls:
        calls.sort(key=lambda c: -c[0][0])
        lines += ["### Closest calls in runs that got away", "", "| Seed | Time | Guard | Step | Peak B | His state |", "|---|---|---|---|---|---|"]
        for (b, t, guard, step, state), seed in calls[:5]:
            lines.append(f"| {seed} | {clock(t)} | {guard} | {step} | {b:.2f} | {state} |")
        lines.append("")

    # Reaction time at the threshold.
    reactions = [e for r in runs for e in r["events"] if e.get("reaction_time") is not None]
    exposures = [x for r in runs for x in r["exposures"]]
    if reactions or exposures:
        lines += ["### Reaction time at the threshold", "",
                  "From the first frame a guard registers the thief (B >= 1, his suspicion at 0) to each change of state he makes from",
                  "what he saw. Exposures: B >= 1 that faded without any reaction.", ""]
        if reactions:
            lines += ["| B when it happened | State reached | Events | Reaction time min / median / max |", "|---|---|---|---|"]
            bins = [(1, 2, "1-2"), (2, 5, "2-5"), (5, 1e9, "5+"), (-1e9, 1, "below 1 (memory)")]
            for low, high, label in bins:
                for to in ("Suspicious", "Investigating", "Searching", "Alarmed"):
                    group = [e["reaction_time"] for e in reactions if low <= e["b"] < high and e["to"] == to]
                    if group:
                        lines.append(f"| {label} | {to} | {len(group)} | {min(group):.2f} / {statistics.median(group):.2f} / {max(group):.2f} s |")
            lines.append("")
        if exposures:
            durations = [x["duration"] for x in exposures]
            lines.append(f"Exposures without a reaction: {len(exposures)}, lasting {min(durations):.2f} / {statistics.median(durations):.2f} / "
                         f"{max(durations):.2f} s (min / median / max), peak B up to {max(x['max_b'] for x in exposures):.2f}.")
            lines.append("")

    # Seed spread.
    outcomes = [((r["result"] or {}).get("outcome", "no result"), r["head"].get("seed")) for r in runs]
    row = " ".join(f"{seed}:{o[0].upper()}" for o, seed in sorted(outcomes, key=lambda x: x[1] or 0))
    lines += ["### Per seed", "", f"`{row}` (E escaped, U undetected (lab), D detected, T timeout, S stuck, I incomplete, F failed)", ""]

    problems = [r for r in runs if not r["result"] or r["result"]["outcome"] in ("stuck", "error", "incomplete")]
    if problems:
        lines += ["### Tool problems", ""]
        for run in problems[:10]:
            result = run["result"] or {}
            lines.append(f"* seed {run['head'].get('seed')}: {result.get('outcome', 'no result line')} at {clock(result.get('t'))}: "
                         f"{result.get('reason', '')} (thief at {result.get('thief_at')})")
        lines.append("")
    failed_uses = Counter(u["target"] for r in runs for u in r["uses"] if not u["ok"])
    if failed_uses:
        lines += ["Uses that did not work: " + ", ".join(f"{k} ({v})" for k, v in failed_uses.items()), ""]
    return lines


# ---------------------------------------------------------------------------------------------- main

def main():
    if len(sys.argv) > 1 and sys.argv[1] == "report":
        if len(sys.argv) != 3:
            sys.exit("usage: playtest.py report <series folder>")
        print(write_report(sys.argv[2]))
        return
    parser = argparse.ArgumentParser(description="Play a level along routes, many seeds, and report (docs/Playtest.md).")
    parser.add_argument("--scene", required=True, help="scene asset path, e.g. Scenes/Stealth.pscene")
    parser.add_argument("--routes", nargs="*", help="route folders or modules under Assets, or 'baseline' (default: baseline + Playtest/<Level>)")
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--first-seed", type=int, default=1)
    parser.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 2) // 2)))
    parser.add_argument("--set", help="extra game values for every run, e.g. difficulty=Hard")
    parser.add_argument("--config", default="release", help="build to use: build/<config>/bin (default release)")
    parser.add_argument("--project", default=str(ROOT / "DemoProject"))
    parser.add_argument("--out", help="output folder (default out/playtest/<Level>/<date-time>)")
    run_series(parser.parse_args())


if __name__ == "__main__":
    main()
