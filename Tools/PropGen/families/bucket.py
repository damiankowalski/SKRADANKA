"""Buckets and a washtub: staves tapering to the bottom, iron hoops, an iron bail on the bucket."""

import math

FAMILY = "bucket"
BUDGET = "clutter"
VARIANTS = {
    "well": {"height": 0.32, "bottom": 0.12, "top": 0.15, "staves": 12, "bail": True, "material": "wood"},
    "washtub": {"height": 0.3, "bottom": 0.3, "top": 0.36, "staves": 18, "bail": False, "material": "wood_dark"},
}


def build(prop, height, bottom, top, staves, bail, material):
    prop.lathe([(bottom, 0.0), (top, height)], staves, material, board=True, caps=(True, False), lods=(0,), name="staves")
    prop.lathe([(bottom, 0.0), (top, height)], max(8, staves // 2), material, board=True, caps=(True, False), lods=(1,),
               name="staves")
    prop.lathe([(bottom - 0.01, height * 0.15), (bottom - 0.01, height * 0.15 + 0.01)], staves, material,
               caps=(False, True), lods=(0,), name="floor")
    prop.lathe([(bottom, 0.0), (top, height)], 6, material, caps=(True, False), lods=(2,), name="staves")
    for t in (0.15, 0.8):
        r = bottom + (top - bottom) * t + 0.004
        prop.lathe([(r, height * t - 0.012), (r + 0.002, height * t + 0.012)], staves, "iron", lods=(0,), name="hoop")
    if bail:
        # A half hoop of eight bars over the top, pinned at the sides.
        rise = top * 0.9
        for n in range(8):
            a0, a1 = math.pi * n / 8, math.pi * (n + 1) / 8
            x0, y0 = top * math.cos(a0), height + rise * math.sin(a0)
            x1, y1 = top * math.cos(a1), height + rise * math.sin(a1)
            length = math.hypot(x1 - x0, y1 - y0)
            prop.box(((x0 + x1) / 2, (y0 + y1) / 2, 0), (length + 0.004, 0.008, 0.008), "iron",
                     rotate=(0, 0, math.degrees(math.atan2(y1 - y0, x1 - x0))), lods=(0,), name="bail")
    prop.collide((0, height / 2, 0), (top, height / 2, top))
