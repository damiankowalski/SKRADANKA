"""Generate tileable PBR materials with FLUX.2 klein 4B (Tools/AssetGen/README.md).

    uv run --project Tools/AssetGen --extra gen python Tools/AssetGen/generate.py stone_wall --variants 3

Per variant: <name>_albedo.png (tileable), _normal.png (glTF convention), _mr.png (G roughness, B metallic),
_preview.png (2x2 tiled) and <name>.json (provenance). Output goes to Tools/AssetGen/out/ unless --out says
otherwise; nothing is written into the game's assets. The model loads for this command only."""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from assetgen import config as cfg  # noqa: E402
from assetgen import processing as proc  # noqa: E402

PROMPTS = ROOT / "prompts"


def parse_args(argv: list[str], defaults: dict) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate tileable PBR materials with FLUX.2 klein 4B.")
    parser.add_argument("material", help="a template in prompts/ (e.g. stone_wall)")
    parser.add_argument("--variants", type=int, default=defaults["variants"], help="how many (seeds seed, seed+1, ...)")
    parser.add_argument("--seed", type=int, default=defaults["seed"])
    parser.add_argument("--only", type=int, default=None, metavar="N",
                        help="just variant N of the set (its seed and variation), e.g. to redo one")
    parser.add_argument("--steps", type=int, default=defaults["steps"])
    parser.add_argument("--size", type=int, default=defaults["size"], help="pixels per side (a multiple of 64)")
    parser.add_argument("--quantize", type=int, default=defaults["quantize"], choices=(0, 4, 8),
                        help="bits for the weights; 0 = full precision (needs about twice the memory)")
    parser.add_argument("--model", default=None, help="must be on config.json's allowed list")
    parser.add_argument("--out", type=Path, default=ROOT / "out", help="output folder (default Tools/AssetGen/out)")
    parser.add_argument("--sheet", type=Path, default=None, help="also write a contact sheet of all variants here")
    parser.add_argument("--no-seamless", action="store_true", help="plain sampling (for comparison)")
    parser.add_argument("--blend-steps", type=int, default=defaults["blend_steps"],
                        help="final steps that blend all four rolled copies (4 transformer passes each); earlier "
                             "steps roll one copy (1 pass). Equal to --steps: blend on every step")
    parser.add_argument("--reprocess", action="store_true",
                        help="no model: redo maps, seam test, previews and --sheet from the albedos in --out")
    parser.add_argument("--no-fallback", action="store_true", help="keep the raw result even if the seam test fails")
    return parser.parse_args(argv)


def load_template(name: str) -> dict:
    path = PROMPTS / f"{name}.json"
    if not path.exists():
        names = sorted(p.stem for p in PROMPTS.glob("*.json"))
        raise FileNotFoundError(f"no template {path.name} in prompts/ (have: {', '.join(names)})")
    with open(path, encoding="utf-8") as file:
        template = json.load(file)
    template["style"] = (PROMPTS / "style.txt").read_text(encoding="utf-8").strip()
    return template


def prompt_for(template: dict, variant: int) -> str:
    variations = template.get("variations") or [""]
    return template["prompt"].format(style=template["style"], variation=variations[variant % len(variations)])


def editor_running() -> bool:
    try:
        return subprocess.run(["pgrep", "-x", "PenumbraEditor"], capture_output=True).returncode == 0
    except OSError:
        return False


def save_png(image: np.ndarray, path: Path) -> None:
    Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)).save(path, optimize=True)


def preview(albedo: np.ndarray) -> np.ndarray:
    """2x2 tiles at the albedo's own size: every seam (edges and the middle) is in view."""
    tiled = Image.fromarray((proc.tile(albedo) * 255.0 + 0.5).astype(np.uint8))
    size = albedo.shape[0]
    return np.asarray(tiled.resize((size, size), Image.LANCZOS), dtype=np.float32) / 255.0


def contact_sheet(rows: list[list[np.ndarray]], path: Path, cell: int = 384) -> None:
    sheet = Image.new("RGB", (cell * len(rows[0]), cell * len(rows)))
    for y, row in enumerate(rows):
        for x, image in enumerate(row):
            picture = Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8))
            sheet.paste(picture.resize((cell, cell), Image.LANCZOS), (x * cell, y * cell))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, optimize=True)


def process_rss_gb() -> float:
    import resource  # POSIX only; generation runs on macOS

    # macOS reports bytes, Linux kibibytes.
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return peak / 1024 ** 3 if sys.platform == "darwin" else peak / 1024 ** 2


def finish_variant(raw: np.ndarray, method: str, record: dict, template: dict, out: Path, name: str,
                   no_fallback: bool) -> tuple[list[np.ndarray] | None, bool]:
    """Seam test (and the masking fallback), maps, preview and provenance for one generated albedo. Writes the
    files and fills record in; returns the contact sheet row (None if the record is incomplete) and the verdict."""
    report = proc.seam_report(raw)
    albedo = raw
    if not report["passed"] and report["fallback_can_help"] and not no_fallback:
        albedo = proc.make_tileable(raw)
        fixed = proc.seam_report(albedo)
        fixed["before_fallback"] = report
        report, method = fixed, method + "+masking"

    height = proc.height_from_albedo(albedo)
    normal = proc.encode_normal(proc.normal_map(height, template["normal_strength"]))
    mr = proc.metallic_roughness(height, tuple(template["roughness"]), template["metallic"])
    tiled = preview(albedo)

    files = {
        "albedo": f"{name}_albedo.png",
        "normal": f"{name}_normal.png",
        "metallic_roughness": f"{name}_mr.png",
        "preview": f"{name}_preview.png",
    }
    if albedo is not raw:
        files["raw_albedo"] = f"{name}_raw.png"  # what the model drew, before the masking fallback
        save_png(raw, out / files["raw_albedo"])
    else:
        (out / f"{name}_raw.png").unlink(missing_ok=True)
    for key, image in (("albedo", albedo), ("normal", normal), ("metallic_roughness", mr), ("preview", tiled)):
        save_png(image, out / files[key])
    record["seamless"] = {"method": method, **report}
    record["maps"] = {
        "height": "albedo luminance (approximation)",
        "normal_convention": "glTF: +X right, +Y up (green), +Z out",
        "normal_strength": template["normal_strength"],
        "metallic_roughness": "glTF: G roughness, B metallic",
        "roughness": template["roughness"],
        "metallic": template["metallic"],
    }
    record["files"] = files
    problems = proc.validate_provenance(record)
    if problems:
        print(f"Provenance incomplete: {problems}", file=sys.stderr)
        return None, False
    with open(out / f"{name}.json", "w", encoding="utf-8") as file:
        json.dump(record, file, indent=2)
        file.write("\n")
    return [albedo, normal, mr, tiled], report["passed"]


def describe(record: dict) -> str:
    seamless = record["seamless"]
    verdict = "tiles" if seamless["passed"] else "SEAM TEST FAILED"
    x, y = seamless["x"], seamless["y"]
    return (f"{seamless['method']}, {verdict} (wrap {x['wrap_step']}/{y['wrap_step']}, weakest detail "
            f"{x['weakest_detail']}/{y['weakest_detail']}, lines {max(x['line_at_edge'], x['line_at_centre'])}/"
            f"{max(y['line_at_edge'], y['line_at_centre'])})")


def reprocess(args: argparse.Namespace, template: dict) -> int:
    """Redo everything after generation from the albedos already in --out (no model): after changing the
    post-processing or a template's maps settings. The model's raw albedo is used where the fallback ran."""
    out = args.out / args.material
    records = sorted(out.glob("*.json"))
    if not records:
        print(f"Nothing to reprocess in {out}", file=sys.stderr)
        return 2
    sheet_rows, failures = [], 0
    for path in records:
        with open(path, encoding="utf-8") as file:
            record = json.load(file)
        files = record.get("files", {})
        source = out / files.get("raw_albedo", files.get("albedo", ""))
        if not source.is_file():
            print(f"{path.name}: no albedo to reprocess", file=sys.stderr)
            failures += 1
            continue
        raw = np.asarray(Image.open(source).convert("RGB"), dtype=np.float32) / 255.0
        method = record.get("seamless", {}).get("method", "generation").split("+")[0]
        row, passed = finish_variant(raw, method, record, template, out, path.stem, args.no_fallback)
        if row is None:
            return 1
        failures += 0 if passed else 1
        print(f"{path.stem}: {describe(record)}")
        sheet_rows.append(row)
    if args.sheet:
        contact_sheet(sheet_rows, args.sheet)
        print(f"Contact sheet: {args.sheet}")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    config = cfg.load_config()
    args = parse_args(sys.argv[1:] if argv is None else argv, config["defaults"])

    model_id = args.model or config["model"]["id"]
    try:
        allowed = cfg.check_model_allowed(config, model_id)
    except cfg.ModelNotAllowed as error:
        print(f"Refusing: {error}", file=sys.stderr)
        return 3
    if model_id.lower() != config["model"]["id"].lower():
        print(f"Refusing: {model_id} is allowed but not the pinned model in config.json", file=sys.stderr)
        return 3
    try:
        template = load_template(args.material)
    except (FileNotFoundError, json.JSONDecodeError, KeyError) as error:
        print(f"Template problem: {error}", file=sys.stderr)
        return 2
    if args.reprocess:
        return reprocess(args, template)
    if args.size % 64 != 0 or args.size < 256:
        print("--size must be a multiple of 64, at least 256", file=sys.stderr)
        return 2

    if sys.platform != "darwin" or platform.machine() != "arm64":
        print("Generation needs a Mac with Apple Silicon (MLX). The post-processing and its tests run anywhere.",
              file=sys.stderr)
        return 2
    if editor_running():
        print("Warning: PenumbraEditor is running. The model needs most of 16 GB; close the editor if generation "
              "slows down or the Mac starts swapping.", file=sys.stderr)

    snapshot = cfg.snapshot_dir(config)
    missing = cfg.missing_snapshot_files(config)
    if missing:
        print(f"The pinned model is not downloaded ({snapshot}; missing {', '.join(missing)}). "
              "Run Tools/AssetGen/setup.sh check, then download.", file=sys.stderr)
        return 2

    # Offline from here: the pinned snapshot only, nothing fetched.
    os.environ["HF_HOME"] = str(cfg.hf_home(config))
    os.environ["HF_HUB_OFFLINE"] = "1"
    from importlib.metadata import version

    mflux_version = version("mflux")
    if mflux_version != config["mflux_version"]:
        print(f"mflux {mflux_version} is installed, config.json pins {config['mflux_version']}: "
              "run Tools/AssetGen/setup.sh install", file=sys.stderr)
        return 2
    from assetgen import generator

    quantize = args.quantize or None
    out = args.out / args.material
    out.mkdir(parents=True, exist_ok=True)

    started = time.perf_counter()
    model = generator.SeamlessKlein(snapshot, quantize)
    load_seconds = time.perf_counter() - started
    print(f"Model loaded in {load_seconds:.1f} s ({'full precision' if quantize is None else f'{quantize}-bit'})")

    variants = [args.only] if args.only is not None else list(range(args.variants))
    prompts = {variant: prompt_for(template, variant) for variant in variants}
    encoded_prompts = dict(zip(variants, model.encode_prompts([prompts[v] for v in variants])))

    sheet_rows = []
    failures = 0
    for variant in variants:
        seed = args.seed + variant
        prompt = prompts[variant]
        name = f"{args.material}_{seed}"
        if args.no_seamless:
            name += "_plain"
        elif args.blend_steps != config["defaults"]["blend_steps"]:
            name += f"_blend{args.blend_steps}"
        if quantize is None:
            name += "_full"
        generator.reset_peak_memory()
        began = time.perf_counter()
        raw = model.generate(encoded_prompts[variant], seed=seed, steps=args.steps, size=args.size,
                             seamless=not args.no_seamless, blend_steps=args.blend_steps)
        seconds = time.perf_counter() - began
        peak_gb = generator.peak_memory_gb()

        record = proc.build_provenance(
            material=args.material,
            template=f"prompts/{args.material}.json",
            prompt=prompt,
            seed=seed,
            steps=args.steps,
            blend_steps=None if args.no_seamless else args.blend_steps,
            size=args.size,
            quantize=quantize,
            model={"id": config["model"]["id"], "revision": config["model"]["revision"], "license": allowed["license"]},
            mflux_version=mflux_version,
            license=allowed["license"],
            timing={"model_load_seconds": round(load_seconds, 1), "image_seconds": round(seconds, 1),
                    "mlx_peak_gb": round(peak_gb, 2), "process_peak_rss_gb": round(process_rss_gb(), 2)},
        )
        method = "plain" if args.no_seamless else "generation"
        row, passed = finish_variant(raw, method, record, template, out, name, args.no_fallback)
        if row is None:
            return 1
        failures += 0 if passed else 1
        print(f"{name}: {seconds:.1f} s, MLX peak {peak_gb:.1f} GB, {describe(record)}")
        sheet_rows.append(row)

    if args.sheet:
        contact_sheet(sheet_rows, args.sheet)
        print(f"Contact sheet: {args.sheet}")
    print(f"Done in {time.perf_counter() - started:.1f} s; files in {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
