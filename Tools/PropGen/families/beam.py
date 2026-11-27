"""Timber: ceiling beams, posts and a post with knee braces under a beam, in dark oak."""

import math

FAMILY = "beam"
BUDGET = "furniture"
VARIANTS = {
    "ceiling_4m": {"kind": "beam", "length": 4.0, "section": 0.22},
    "ceiling_6m": {"kind": "beam", "length": 6.0, "section": 0.25},
    "post": {"kind": "post", "length": 3.6, "section": 0.22},
    "post_braced": {"kind": "braced", "length": 3.6, "section": 0.22},
}


def build(prop, kind, length, section):
    s = section
    if kind == "beam":
        # Lying along X on its pivot (hung from a ceiling, its top at the pivot's height + section).
        prop.box((0, s / 2, 0), (length, s, s * 0.9), "wood_dark", grain="x", lods=(0, 1, 2), name="beam")
        for sign in (-1, 1):
            # Pegs where the beam was joined.
            for x in (-length / 2 + 0.3, length / 2 - 0.3):
                prop.box((x, s / 2, sign * (s * 0.45 + 0.004)), (0.035, 0.035, 0.012), "wood", lods=(0,), name="peg")
        return
    prop.box((0, length / 2, 0), (s, length, s), "wood_dark", grain="y", lods=(0, 1, 2), name="post")
    prop.box((0, 0.04, 0), (s + 0.1, 0.08, s + 0.1), "stone_dark", lods=(0, 1), name="pad")
    if kind == "braced":
        # A head plate and two knee braces at 45 degrees, carrying a beam along X.
        prop.box((0, length + s / 2, 0), (1.6, s, s * 0.9), "wood_dark", grain="x", lods=(0, 1, 2), name="head")
        reach = 0.55
        for sign in (-1, 1):
            cx, cy = sign * (s / 2 + reach / 2), length - reach / 2
            prop.box((cx, cy, 0), (math.hypot(reach, reach) + 0.06, 0.12, 0.12), "wood_dark",
                     rotate=(0, 0, -sign * 45), grain="x", lods=(0, 1), name="brace")
    prop.collide((0, length / 2, 0), (s / 2, length / 2, s / 2))
