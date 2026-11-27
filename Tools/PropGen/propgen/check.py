"""Checks a generated prop the way the engine will meet it (Tools/PropGen/README.md, "Checks"); stdlib only.

A prop passes when its three .glb files read as glTF 2.0, every triangle faces the way its normal says, its pivot
sits at the middle of its base (metres, Y up), its full model stays within its size class's triangle budget, LOD1
and LOD2 are lighter (config.json "lods"), every texture it names exists, and its provenance agrees with the files.
"""

import json
import math
import os
from pathlib import Path

from . import glb

PIVOT_TOLERANCE = 0.002    # metres: the base's height
CENTRE_TOLERANCE = 0.15    # of the prop's width / depth: how far off the middle the footprint may sit (a chair's
                           # leaning back)
LOD_SLACK = 1.4            # a LOD may be this much heavier than its ratio in config.json


def inspect(path, assets):
    """Triangles, bounds and problems of one .glb."""
    problems = []
    try:
        gltf, binary = glb.read(path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        return {"triangles": 0, "min": None, "max": None}, [f"{Path(path).name}: unreadable: {error}"]
    triangles, low, high = 0, [math.inf] * 3, [-math.inf] * 3
    for image in gltf.get("images", []):
        target = (Path(path).resolve().parent / image.get("uri", "")).resolve()
        if not target.is_file():
            problems.append(f"{Path(path).name}: texture {image.get('uri')} is missing")
        elif Path(os.path.commonpath([target, Path(assets).resolve()])) != Path(assets).resolve():
            problems.append(f"{Path(path).name}: texture {image.get('uri')} is outside the project's assets")
    for mesh in gltf.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            attributes = primitive["attributes"]
            positions = glb.read_accessor(gltf, binary, attributes["POSITION"])
            normals = glb.read_accessor(gltf, binary, attributes["NORMAL"]) if "NORMAL" in attributes else None
            uvs = glb.read_accessor(gltf, binary, attributes["TEXCOORD_0"]) if "TEXCOORD_0" in attributes else None
            indices = glb.read_accessor(gltf, binary, primitive["indices"])
            if len(indices) % 3:
                problems.append(f"{Path(path).name}: an index count that is not whole triangles")
            if uvs is None:
                problems.append(f"{Path(path).name}: no UVs")
            elif any(not math.isfinite(c) for uv in uvs for c in uv):
                problems.append(f"{Path(path).name}: UVs that are not numbers")
            for p in positions:
                for i in range(3):
                    low[i], high[i] = min(low[i], p[i]), max(high[i], p[i])
            backwards = 0
            for t in range(0, len(indices) - 2, 3):
                a, b, c = (positions[indices[t + n]] for n in range(3))
                ab, ac = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
                face = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
                if normals is not None and sum(face[i] * normals[indices[t]][i] for i in range(3)) <= 0:
                    backwards += 1
            if backwards:
                problems.append(f"{Path(path).name}: {backwards} triangle(s) face away from their normals")
            triangles += len(indices) // 3
    return {"triangles": triangles, "min": low, "max": high}, problems


def check_prop(meta_path, settings, assets):
    """Every check for one prop (its provenance .json and the files it names); a list of problems."""
    meta_path = Path(meta_path)
    try:
        provenance = json.loads(meta_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [f"{meta_path.name}: unreadable provenance: {error}"]
    problems = []
    results = []
    for lod in range(len(settings["lods"])):
        name = provenance.get("files", {}).get(f"lod{lod}")
        if not name or not (meta_path.parent / name).is_file():
            problems.append(f"{meta_path.stem}: LOD{lod} file missing")
            return problems
        result, found = inspect(meta_path.parent / name, assets)
        problems += found
        results.append(result)
    full = results[0]
    if full["min"] is None or full["triangles"] == 0:
        return problems + [f"{meta_path.stem}: an empty model"]
    if provenance.get("triangles") != [r["triangles"] for r in results]:
        problems.append(f"{meta_path.stem}: the provenance's triangles {provenance.get('triangles')} do not match "
                        f"the files' {[r['triangles'] for r in results]}")
    low, high = settings["budgets"][provenance["budget"]]
    if full["triangles"] > high:
        problems.append(f"{meta_path.stem}: {full['triangles']} triangles, over the {provenance['budget']} budget of {high}")
    for lod in range(1, len(results)):
        # A light prop's LODs only need to stay under a floor: a table far away is still its top and four legs.
        allowed = max(settings["lods"][lod] * LOD_SLACK * full["triangles"], settings.get("lod_floor", [0] * 3)[lod])
        if results[lod]["triangles"] > allowed or results[lod]["triangles"] > results[lod - 1]["triangles"]:
            problems.append(f"{meta_path.stem}: LOD{lod} has {results[lod]['triangles']} triangles, more than "
                            f"{allowed:.0f} ({settings['lods'][lod]:.0%} of {full['triangles']}) or than LOD{lod - 1}")
    if abs(full["min"][1]) > PIVOT_TOLERANCE:
        problems.append(f"{meta_path.stem}: the base is at y = {full['min'][1]:.3f}, not on the pivot")
    for axis, label in ((0, "x"), (2, "z")):
        middle, extent = (full["min"][axis] + full["max"][axis]) / 2, full["max"][axis] - full["min"][axis]
        if abs(middle) > CENTRE_TOLERANCE * extent:
            problems.append(f"{meta_path.stem}: the footprint's middle is {middle:.3f} m off the pivot along {label}")
    if not provenance.get("collision"):
        problems.append(f"{meta_path.stem}: no collision boxes")
    return problems


def check_folder(root, settings, assets):
    root = Path(root)
    problems = []
    metas = sorted(root.glob("*/*.json")) if root.is_dir() else []
    if not metas:
        return [f"no props in {root}"]
    for meta in metas:
        problems += check_prop(meta, settings, assets)
    return problems
