"""Pottery: jugs, mugs, bowls and a storage jar, turned on a wheel (lathes), unglazed clay with a glazed rim."""

import math

FAMILY = "pottery"
BUDGET = "clutter"
# Profiles: (radius, height) from the foot up, metres; the last ring is the rim.
VARIANTS = {
    "jug": {"profile": [(0.045, 0.0), (0.075, 0.06), (0.08, 0.12), (0.06, 0.2), (0.04, 0.24), (0.045, 0.27)], "segments": 14,
            "handle": True},
    "mug": {"profile": [(0.04, 0.0), (0.042, 0.05), (0.045, 0.1), (0.047, 0.11)], "segments": 12, "handle": True},
    "bowl": {"profile": [(0.05, 0.0), (0.1, 0.035), (0.13, 0.07), (0.135, 0.075)], "segments": 16, "handle": False},
    "storage_jar": {"profile": [(0.12, 0.0), (0.2, 0.12), (0.22, 0.3), (0.17, 0.48), (0.11, 0.56), (0.12, 0.6)],
                    "segments": 16, "handle": False},
}


def build(prop, profile, segments, handle):
    rim = profile[-2:]
    # LOD0 at full roundness, LOD1 at two thirds; the rim glazed and closed over (the inside is never seen at this size).
    for lods, sides in (((0,), segments), ((1,), max(6, segments * 2 // 3))):
        prop.lathe(profile[:-1], sides, "clay", caps=(True, False), lods=lods, name="body")
        prop.lathe(rim, sides, "glaze", caps=(False, True), lods=lods, name="rim")
    widest = max(profile, key=lambda p: p[0])
    coarse = [profile[0]] + ([widest] if widest not in (profile[0], profile[-1]) else []) + [profile[-1]]
    prop.lathe(coarse, 6, "clay", caps=(True, True), lods=(2,), name="body")
    if handle:
        # A strap handle on +X: three short bars from below the rim down the belly.
        height = profile[-1][1]
        top, bottom = height * 0.85, height * 0.35
        out = widest[0] + 0.03
        for (x0, y0), (x1, y1) in (((rim[1][0], top), (out, top - 0.01)), ((out, top - 0.01), (out, bottom + 0.01)),
                                   ((out, bottom + 0.01), (widest[0] * 0.95, bottom))):
            length = math.hypot(x1 - x0, y1 - y0)
            angle = math.degrees(math.atan2(y1 - y0, x1 - x0))
            prop.box(((x0 + x1) / 2, (y0 + y1) / 2, 0), (length + 0.01, 0.012, 0.022), "clay", rotate=(0, 0, angle),
                     lods=(0,), name="handle")
    prop.collide((0, profile[-1][1] / 2, 0), (widest[0], profile[-1][1] / 2, widest[0]))
