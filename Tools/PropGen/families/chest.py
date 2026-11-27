"""Chests: a box of boards with a lid, iron bands over it, a lock plate in front; the strongbox all iron-bound."""

FAMILY = "chest"
BUDGET = "furniture"
VARIANTS = {
    "travel": {"width": 0.9, "depth": 0.5, "height": 0.5, "bands": 2, "material": "wood"},
    "strongbox": {"width": 0.7, "depth": 0.45, "height": 0.45, "bands": 3, "material": "wood_dark"},
    "dowry": {"width": 1.2, "depth": 0.55, "height": 0.55, "bands": 2, "material": "wood"},
}

THICK = 0.025
LID = 0.11       # the lid's height (a flat box lid)
BAND = 0.045


def build(prop, width, depth, height, bands, material):
    w, d, h = width, depth, height
    body = h - LID
    rows = 3
    for n in range(rows):
        y = body * (n + 0.5) / rows
        for sign in (-1, 1):
            prop.box((0, y, sign * (d / 2 - THICK / 2)), (w, body / rows - 0.004, THICK), material, grain="x", board=True,
                     lods=(0,), name="side board")
        for sign in (-1, 1):
            prop.box((sign * (w / 2 - THICK / 2), y, 0), (THICK, body / rows - 0.004, d - 2 * THICK), material, grain="z",
                     board=True, lods=(0,), name="end board")
    prop.box((0, body / 2, 0), (w, body, d), material, grain="x", lods=(1,), name="body")
    prop.box((0, 0.01, 0), (w - 2 * THICK, 0.02, d - 2 * THICK), material, grain="x", lods=(0,), name="bottom")
    # The lid sits a hair proud of the body all round.
    prop.box((0, body + LID / 2 + 0.003, 0), (w + 0.01, LID, d + 0.01), material, grain="x", lods=(0, 1), name="lid")
    prop.box((0, h / 2, 0), (w + 0.01, h + 0.003, d + 0.01), material, grain="x", lods=(2,), name="block")
    # Iron bands round the body and over the lid.
    for n in range(bands):
        x = -w / 2 + w * (n + 1) / (bands + 1)
        prop.box((x, h / 2 + 0.0015, d / 2 + 0.008), (BAND, h + 0.006, 0.006), "iron", lods=(0, 1), name="band front")
        prop.box((x, h / 2 + 0.0015, -d / 2 - 0.008), (BAND, h + 0.006, 0.006), "iron", lods=(0, 1), name="band back")
        prop.box((x, h + 0.008, 0), (BAND, 0.006, d + 0.022), "iron", lods=(0, 1), name="band top")
    # The lock plate and the corner guards.
    prop.box((0, body - 0.02, -d / 2 - 0.01), (0.09, 0.11, 0.008), "iron", lods=(0, 1), name="lock")
    for sx in (-1, 1):
        for sz in (-1, 1):
            prop.box((sx * (w / 2 + 0.004), body / 2, sz * (d / 2 - 0.03)), (0.006, body - 0.02, 0.06), "iron", lods=(0,),
                     name="corner")
    prop.collide((0, h / 2, 0), (w / 2, h / 2, d / 2))
