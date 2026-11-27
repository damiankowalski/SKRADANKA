"""Shelving: two board sides, shelves between them, a back of boards (or open, for the cellar's racks)."""

FAMILY = "shelf"
BUDGET = "furniture"
VARIANTS = {
    "kitchen": {"width": 1.2, "depth": 0.35, "height": 1.8, "shelves": 4, "back": True, "material": "wood"},
    "cellar": {"width": 1.6, "depth": 0.45, "height": 2.0, "shelves": 4, "back": False, "material": "wood_dark"},
    "low": {"width": 1.0, "depth": 0.32, "height": 0.9, "shelves": 2, "back": True, "material": "wood"},
}

SIDE = 0.03
BOARD = 0.025


def build(prop, width, depth, height, shelves, back, material):
    w, d, h = width, depth, height
    for sign in (-1, 1):
        prop.box((sign * (w / 2 - SIDE / 2), h / 2, 0), (SIDE, h, d), material, grain="y", lods=(0, 1, 2), name="side")
    inner = w - 2 * SIDE
    levels = [0.08] + [0.08 + (h - 0.12) * (n + 1) / shelves for n in range(shelves)]
    for y in levels:
        prop.box((0, y, 0), (inner, BOARD, d - 0.01), material, grain="x", board=False, lods=(0, 1), name="shelf")
    prop.box((0, levels[-1], 0), (inner, BOARD, d - 0.01), material, grain="x", lods=(2,), name="top")
    if back:
        boards = max(3, round(inner / 0.2))
        for n in range(boards):
            x = -inner / 2 + inner * (n + 0.5) / boards
            prop.box((x, h / 2, d / 2 - 0.006), (inner / boards - 0.003, h - 0.02, 0.012), material, grain="y",
                     board=True, lods=(0,), name="back board")
        prop.box((0, h / 2, d / 2 - 0.006), (inner, h - 0.02, 0.012), material, grain="y", lods=(1, 2), name="back")
    else:
        # Two diagonal braces across the open back keep the rack square.
        import math
        angle = math.degrees(math.atan2(h - 0.2, inner))
        length = math.hypot(inner, h - 0.2)
        for sign in (-1, 1):
            prop.box((0, h / 2, d / 2 - 0.012 - (0.02 if sign > 0 else 0)), (length, 0.06, 0.018), material,
                     rotate=(0, 0, sign * angle), grain="x", lods=(0, 1), name="brace")
    prop.collide((0, h / 2, 0), (w / 2, h / 2, d / 2))
