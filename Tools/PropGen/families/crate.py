"""Board crates: four walls of horizontal boards nailed to corner posts, a lid of boards, a diagonal brace on the
long sides, iron corner straps on the banded ones."""

FAMILY = "crate"
BUDGET = "furniture"
VARIANTS = {
    "small": {"width": 0.5, "depth": 0.5, "height": 0.45, "boards": 3, "material": "wood"},
    "large": {"width": 0.8, "depth": 0.7, "height": 0.7, "boards": 4, "material": "wood"},
    "long": {"width": 1.2, "depth": 0.6, "height": 0.55, "boards": 3, "material": "wood_dark"},
    "banded": {"width": 0.7, "depth": 0.7, "height": 0.6, "boards": 4, "material": "wood", "iron": True},
}

THICK = 0.022   # boards
POST = 0.045    # corner posts
GAP = 0.006     # between boards


def build(prop, width, depth, height, boards, material, iron=False):
    w, d, h = width, depth, height
    pitch = h / boards
    for n in range(boards):
        y = pitch * (n + 0.5)
        for sign in (-1, 1):
            # Front and back run along X, the ends along Z between them.
            prop.box((0, y, sign * (d / 2 - THICK / 2)), (w, pitch - GAP, THICK), material, grain="x", board=True,
                     lods=(0,), name="side board")
            prop.box((sign * (w / 2 - THICK / 2), y, 0), (THICK, pitch - GAP, d - 2 * THICK), material, grain="z",
                     board=True, lods=(0,), name="end board")
    for sign in (-1, 1):
        prop.box((0, h / 2, sign * (d / 2 - THICK / 2)), (w, h, THICK), material, grain="x", lods=(1,), name="side")
        prop.box((sign * (w / 2 - THICK / 2), h / 2, 0), (THICK, h, d - 2 * THICK), material, grain="z", lods=(1,),
                 name="end")
    for sx in (-1, 1):
        for sz in (-1, 1):
            prop.box((sx * (w / 2 - POST / 2 + 0.004), h / 2, sz * (d / 2 - POST / 2 + 0.004)), (POST, h, POST), material,
                     grain="y", lods=(0, 1), name="post")
    # The lid: boards across the width; one slab further away.
    lid = max(2, round(d / 0.16))
    for n in range(lid):
        z = -d / 2 + d * (n + 0.5) / lid
        prop.box((0, h + THICK / 2, z), (w, THICK, d / lid - GAP), material, grain="x", board=True, lods=(0,), name="lid")
    prop.box((0, h + THICK / 2, 0), (w, THICK, d), material, grain="x", lods=(1,), name="lid")
    # A diagonal brace on the front and the back.
    import math
    span, rise = w - 2 * POST, h - 2 * 0.05
    angle = math.degrees(math.atan2(rise, span))
    length = math.hypot(span, rise)
    for sign in (-1, 1):
        prop.box((0, h / 2, sign * (d / 2 + THICK / 2)), (length, 0.07, THICK), material, rotate=(0, 0, sign * angle),
                 grain="x", board=True, lods=(0,), name="brace")
    if iron:
        for sx in (-1, 1):
            for sz in (-1, 1):
                for y in (0.06, h - 0.06):
                    # An L of two thin straps round each corner, top and bottom.
                    x, z = sx * (w / 2 + 0.003), sz * (d / 2 + 0.003)
                    prop.box((x - sx * 0.06, y, z), (0.12, 0.05, 0.006), "iron", lods=(0,), name="strap")
                    prop.box((x, y, z - sz * 0.06), (0.006, 0.05, 0.12), "iron", lods=(0,), name="strap")
    prop.box((0, (h + THICK) / 2, 0), (w, h + THICK, d), material, grain="x", lods=(2,), name="block")
    prop.collide((0, (h + THICK) / 2, 0), (w / 2, (h + THICK) / 2, d / 2))
