"""Post-processing without the model: seam measurement, the masking fallback, height, normal and
metallic/roughness maps, previews and the provenance record. Images are float32 arrays in [0, 1],
shape (H, W, 3) for colour and (H, W) for single channels. Everything wraps: a texture's left edge
is its right edge's neighbour."""

from __future__ import annotations

import datetime as _datetime

import numpy as np

# ---------------------------------------------------------------------------------------------------
# Basics


def luminance(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def blur(image: np.ndarray, sigma: float) -> np.ndarray:
    """Gaussian blur on the torus (FFT): no edge effects, and a tileable image stays tileable."""
    if sigma <= 0.0:
        return image.copy()
    height, width = image.shape[:2]
    fy = np.fft.fftfreq(height)[:, None]
    fx = np.fft.rfftfreq(width)[None, :]
    kernel = np.exp(-2.0 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy))
    if image.ndim == 3:
        kernel = kernel[..., None]
    spectrum = np.fft.rfft2(image, axes=(0, 1))
    return np.fft.irfft2(spectrum * kernel, s=(height, width), axes=(0, 1)).astype(np.float32)


def normalise(values: np.ndarray, low_percentile: float = 1.0, high_percentile: float = 99.0) -> np.ndarray:
    low, high = np.percentile(values, [low_percentile, high_percentile])
    if high - low < 1e-6:
        return np.zeros_like(values)
    return np.clip((values - low) / (high - low), 0.0, 1.0).astype(np.float32)


def tile(image: np.ndarray, count: int = 2) -> np.ndarray:
    reps = (count, count) + (1,) * (image.ndim - 2)
    return np.tile(image, reps)


# ---------------------------------------------------------------------------------------------------
# Seams
#
# Two ways a texture fails to tile, both measured along each axis on the luminance:
# * a step: the wrap (last column next to the first) changes more than neighbouring columns do;
# * a blurred band: fixing the edge by blending a half-shifted copy leaves a soft "cross" through the
#   middle (or a soft frame), where the detail drops. Checked at every position, so the middle counts.

# * a line: a faint step in tone that runs straight along part of the cut (the edge, or the middle,
#   where the generator's rolled copies and the fallback's shifted copy are cut), too faint to move
#   the average over the whole column.

SEAM_LIMIT = 1.6       # the wrap's step / the median step between neighbouring columns
DETAIL_LIMIT = 0.55    # the weakest band's detail / the median band's detail
DETAIL_WINDOW = 24     # pixels averaged into one band
LINE_LIMIT = 2.5       # a cut line's coherent step / the median column's, at least; and no stronger than the
LINE_OWN_QUANTILE = 0.95  # texture's own lines (a wall's mortar joints can fall on the cut: they are no seam),
LINE_OWN_MARGIN = 1.25    # with a margin over their 95th percentile
LINE_WINDOW = 96       # pixels along the line a step must hold to count


def _step_profile(lum: np.ndarray, axis: int) -> np.ndarray:
    """Mean absolute change from each column (axis=1) or row (axis=0) to the next, wrapping."""
    step = np.abs(np.roll(lum, -1, axis=axis) - lum)
    return step.mean(axis=1 - axis)


def _detail_profile(lum: np.ndarray, axis: int) -> np.ndarray:
    """Fine detail (|Laplacian|) per column / row, averaged over DETAIL_WINDOW, wrapping."""
    laplacian = np.abs(
        4.0 * lum
        - np.roll(lum, 1, axis=0) - np.roll(lum, -1, axis=0)
        - np.roll(lum, 1, axis=1) - np.roll(lum, -1, axis=1)
    )
    profile = laplacian.mean(axis=1 - axis)
    kernel = np.ones(DETAIL_WINDOW, dtype=np.float32) / DETAIL_WINDOW
    padded = np.concatenate([profile[-DETAIL_WINDOW:], profile, profile[:DETAIL_WINDOW]])
    return np.convolve(padded, kernel, mode="same")[DETAIL_WINDOW:-DETAIL_WINDOW]


def _line_profile(lum: np.ndarray, axis: int) -> np.ndarray:
    """Per column (axis=1) or row (axis=0): how strongly the signed step to the next one holds along it
    over LINE_WINDOW pixels (the 90th percentile along the line), relative to the median column."""
    step = np.roll(lum, -1, axis=axis) - lum
    if axis == 0:
        step = step.T
    half = LINE_WINDOW // 2
    padded = np.concatenate([step[-half:], step, step[:half]], axis=0)
    running = np.cumsum(padded, axis=0, dtype=np.float64)
    smoothed = (running[2 * half:] - running[:-2 * half]) / (2 * half)
    strength = np.quantile(np.abs(smoothed), 0.9, axis=0)
    return strength / (np.median(strength) + 1e-8)


def seam_report(rgb: np.ndarray) -> dict:
    """Numbers for the seam test; `passed` is the verdict (see SEAM_LIMIT, DETAIL_LIMIT)."""
    lum = luminance(rgb)
    report: dict = {"seam_limit": SEAM_LIMIT, "detail_limit": DETAIL_LIMIT}
    passed = True
    for name, axis in (("x", 1), ("y", 0)):
        steps = _step_profile(lum, axis)
        median_step = float(np.median(steps)) + 1e-8
        wrap = float(steps[-1]) / median_step
        # A wrap no stronger than the texture's own strongest lines (a mortar joint can sit there) is no seam.
        interior = float(np.quantile(steps[:-1], 0.99)) / median_step
        seam_ok = wrap <= max(SEAM_LIMIT, interior)

        detail = _detail_profile(lum, axis)
        median_detail = float(np.median(detail)) + 1e-8
        size = detail.shape[0]
        weakest = float(detail.min()) / median_detail
        lines = _line_profile(lum, axis)
        line_at_edge, line_at_centre = float(lines[-1]), float(lines[size // 2 - 1])
        line_limit = max(LINE_LIMIT, LINE_OWN_MARGIN * float(np.quantile(lines, LINE_OWN_QUANTILE)))
        report[name] = {
            "wrap_step": round(wrap, 3),
            "interior_step_p99": round(interior, 3),
            "weakest_detail": round(weakest, 3),
            "weakest_detail_at": int(detail.argmin()),
            "detail_at_edge": round(float(detail[0]) / median_detail, 3),
            "detail_at_centre": round(float(detail[size // 2]) / median_detail, 3),
            "line_at_edge": round(line_at_edge, 3),
            "line_at_centre": round(line_at_centre, 3),
            "line_limit": round(line_limit, 3),
        }
        steps_ok = seam_ok and weakest >= DETAIL_LIMIT
        lines_ok = max(line_at_edge, line_at_centre) <= line_limit
        report[name]["steps_ok"], report[name]["lines_ok"] = bool(steps_ok), bool(lines_ok)
        passed = passed and steps_ok and lines_ok
    report["passed"] = bool(passed)
    # The masking fallback mends steps and soft bands, not lines: shifting by half a tile swaps edge and centre.
    report["fallback_can_help"] = not all(report[axis]["steps_ok"] for axis in ("x", "y"))
    return report


# ---------------------------------------------------------------------------------------------------
# Masking fallback: when generation did not tile, blend the image with itself shifted by half a tile.
# Band by band (a Laplacian stack): coarse bands cross over widely, fine bands over a few pixels, so
# the crossover stays sharp instead of leaving a soft cross.

FALLBACK_LEVELS = 7


def make_tileable(rgb: np.ndarray) -> np.ndarray:
    # One axis at a time: shifting both at once puts the original's seam (an edge) and the copy's seam
    # (a centre line) through the same points at the edges' midpoints, where no mask can hide both.
    return _tileable_along(_tileable_along(rgb, axis=1), axis=0)


def _tileable_along(rgb: np.ndarray, axis: int) -> np.ndarray:
    size = rgb.shape[axis]
    shifted = np.roll(rgb, size // 2, axis=axis)
    # The original keeps the middle half (its seam is at the edges); the shifted copy, whose seam runs
    # through the middle, fills the outer quarters.
    position = np.abs(np.arange(size, dtype=np.float32) - size / 2.0) / (size / 2.0)
    band = (position < 0.5).astype(np.float32)
    hard = np.broadcast_to(band[None, :] if axis == 1 else band[:, None], rgb.shape[:2]).astype(np.float32)

    result = np.zeros_like(rgb)
    previous_a, previous_b = rgb, shifted
    for level in range(FALLBACK_LEVELS + 1):
        sigma = 2.0 ** level
        if level < FALLBACK_LEVELS:
            blurred_a, blurred_b = blur(rgb, sigma), blur(shifted, sigma)
            band_a, band_b = previous_a - blurred_a, previous_b - blurred_b
        else:
            band_a, band_b = previous_a, previous_b
        mask = blur(hard, sigma * 1.5)[..., None]
        result += band_a * mask + band_b * (1.0 - mask)
        if level < FALLBACK_LEVELS:
            previous_a, previous_b = blurred_a, blurred_b
    return np.clip(result, 0.0, 1.0)


# ---------------------------------------------------------------------------------------------------
# Material maps. Height comes from the albedo's brightness (light stone faces stand out, dark mortar
# and cracks sink): an approximation, not measured height.


def height_from_albedo(rgb: np.ndarray, fine_sigma: float = 1.0, coarse_sigma: float = 6.0) -> np.ndarray:
    lum = luminance(rgb)
    return normalise(0.4 * blur(lum, fine_sigma) + 0.6 * blur(lum, coarse_sigma))


def normal_map(height: np.ndarray, strength: float) -> np.ndarray:
    """Tangent-space normals from a height field, glTF convention (as Penumbra's Mesh.hlsl reads them):
    +X right, +Y up the image (green = up), +Z out. strength: height range in pixels' units."""
    d_dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    d_drow = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    # Rows grow downwards, so the slope upwards is -d_drow and the normal's Y is +d_drow.
    normal = np.stack([-strength * d_dx, strength * d_drow, np.ones_like(height)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return normal.astype(np.float32)


def encode_normal(normal: np.ndarray) -> np.ndarray:
    return np.clip(normal * 0.5 + 0.5, 0.0, 1.0)


def metallic_roughness(height: np.ndarray, roughness: tuple[float, float], metallic: float) -> np.ndarray:
    """glTF layout: G = roughness, B = metallic, R unused (1). Raised parts are the smoother end of the range."""
    smooth, rough = roughness
    green = rough - (rough - smooth) * height
    return np.stack([np.ones_like(height), green, np.full_like(height, metallic)], axis=-1).astype(np.float32)


# ---------------------------------------------------------------------------------------------------
# Provenance: the .json beside every generated material.

PROVENANCE_FIELDS = {
    "tool": str,
    "material": str,
    "template": str,
    "prompt": str,
    "seed": int,
    "steps": int,
    "size": int,
    "quantize": (int, type(None)),
    "model": dict,
    "mflux_version": str,
    "license": str,
    "generated_at": str,
    "ai_generated": bool,
    "seamless": dict,
    "maps": dict,
    "files": dict,
    "timing": dict,
}
MODEL_FIELDS = ("id", "revision", "license")
FILE_FIELDS = ("albedo", "normal", "metallic_roughness", "preview")


def build_provenance(**fields) -> dict:
    record = {"tool": "Penumbra AssetGen", "ai_generated": True}
    record["generated_at"] = _datetime.datetime.now(_datetime.timezone.utc).isoformat(timespec="seconds")
    record.update(fields)
    return record


def validate_provenance(record: dict) -> list[str]:
    """Problems with a provenance record; empty when it is complete."""
    problems = []
    for name, kind in PROVENANCE_FIELDS.items():
        if name not in record:
            problems.append(f"missing {name}")
        elif not isinstance(record[name], kind) or (kind is int and isinstance(record[name], bool)):
            problems.append(f"{name} has the wrong type")
    if record.get("ai_generated") is not True:
        problems.append("ai_generated must be true")
    for name in MODEL_FIELDS:
        if not isinstance(record.get("model"), dict) or not record["model"].get(name):
            problems.append(f"missing model.{name}")
    for name in FILE_FIELDS:
        if not isinstance(record.get("files"), dict) or not record["files"].get(name):
            problems.append(f"missing files.{name}")
    if "generated_at" in record:
        try:
            _datetime.datetime.fromisoformat(record["generated_at"])
        except (TypeError, ValueError):
            problems.append("generated_at is not an ISO date")
    return problems
