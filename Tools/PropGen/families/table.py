"""Tables: a top of boards on four legs joined by aprons; a stretcher low between the legs on the long ones."""

FAMILY = "table"
BUDGET = "furniture"
VARIANTS = {
    "kitchen": {"length": 1.6, "width": 0.8, "height": 0.76, "material": "wood", "stretcher": True},
    "small": {"length": 0.9, "width": 0.6, "height": 0.74, "material": "wood_dark", "stretcher": False},
    "feast": {"length": 2.6, "width": 0.95, "height": 0.78, "material": "wood", "stretcher": True},
}

TOP = 0.045      # top thickness
LEG = 0.075      # leg section
APRON = 0.09     # apron height
OVERHANG = 0.07  # the top beyond the legs


def build(prop, length, width, height, material, stretcher):
    l, w, h = length, width, height
    boards = max(3, round(w / 0.2))
    for n in range(boards):
        z = -w / 2 + w * (n + 0.5) / boards
        prop.box((0, h - TOP / 2, z), (l, TOP, w / boards - 0.004), material, grain="x", board=True, lods=(0,), name="top board")
    prop.box((0, h - TOP / 2, 0), (l, TOP, w), material, grain="x", lods=(1, 2), name="top")
    lx, lz = l / 2 - OVERHANG - LEG / 2, w / 2 - OVERHANG - LEG / 2
    legs = h - TOP
    for sx in (-1, 1):
        for sz in (-1, 1):
            prop.box((sx * lx, legs / 2, sz * lz), (LEG, legs, LEG), material, grain="y", lods=(0, 1, 2), name="leg")
    for sign in (-1, 1):
        prop.box((0, legs - APRON / 2, sign * lz), (2 * lx - LEG, APRON, 0.025), material, grain="x", board=True,
                 lods=(0, 1), name="apron")
        prop.box((sign * lx, legs - APRON / 2, 0), (0.025, APRON, 2 * lz - LEG), material, grain="z", board=True,
                 lods=(0, 1), name="apron")
    if stretcher:
        for sign in (-1, 1):
            prop.box((sign * lx, 0.16, 0), (0.045, 0.06, 2 * lz - LEG), material, grain="z", lods=(0,), name="stretcher")
        prop.box((0, 0.16, 0), (2 * lx - LEG, 0.06, 0.045), material, grain="x", lods=(0,), name="stretcher")
    # Collision: the top only, so a body or a dropped bottle lands on it; the legs are thin.
    prop.collide((0, h - TOP / 2, 0), (l / 2, TOP / 2, w / 2))
    for sx in (-1, 1):
        for sz in (-1, 1):
            prop.collide((sx * lx, legs / 2, sz * lz), (LEG / 2, legs / 2, LEG / 2))
