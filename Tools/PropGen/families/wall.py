"""The architecture kit's walls: a straight run of stone, plain or with a doorway or a window, its opening framed
in timber (a lintel, posts, a sill). Sizes follow the blockout (Tools/BuildDemoProject.py): walls 0.3 m thick,
storeys 4 m, doorways 1.4 m wide so the navmesh passes. The wall runs along X, its faces towards -Z and +Z; the
pivot is the middle of its foot. Stone and plaster keep their textures upright (`grain="y"`: V up the wall)."""

FAMILY = "wall"
BUDGET = "big"
VARIANTS = {
    "stone_2m": {"length": 2.0, "opening": None, "material": "stone"},
    "stone_4m": {"length": 4.0, "opening": None, "material": "stone"},
    "stone_door": {"length": 4.0, "opening": "door", "material": "stone"},
    "stone_window": {"length": 2.0, "opening": "window", "material": "stone"},
    "plaster_4m": {"length": 4.0, "opening": None, "material": "plaster"},
    "plaster_window": {"length": 2.0, "opening": "window", "material": "plaster"},
}

HEIGHT = 4.0
THICK = 0.3
DOOR = (1.4, 2.3)          # width, height of a doorway
WINDOW = (0.8, 1.0, 1.0)   # width, height, sill height
TIMBER = 0.14              # the frame's section
PLINTH = 0.35              # a band of dark stone at the foot


def build(prop, length, opening, material):
    l = length
    # Pieces of wall as boxes around the opening: (x0, x1, y0, y1).
    pieces = []
    if opening is None:
        pieces.append((-l / 2, l / 2, 0.0, HEIGHT))
    else:
        if opening == "door":
            w, h = DOOR
            low, high = 0.0, h
        else:
            w, h, sill = WINDOW
            low, high = sill, sill + h
        pieces += [(-l / 2, -w / 2, 0.0, HEIGHT), (w / 2, l / 2, 0.0, HEIGHT), (-w / 2, w / 2, high, HEIGHT)]
        if low > 0:
            pieces.append((-w / 2, w / 2, 0.0, low))
    for x0, x1, y0, y1 in pieces:
        bottom = max(y0, PLINTH) if material == "plaster" else y0
        if y1 > bottom:
            prop.box(((x0 + x1) / 2, (bottom + y1) / 2, 0), (x1 - x0, y1 - bottom, THICK), material, grain="y", aligned=True,
                     lods=(0, 1, 2) if opening is None else (0, 1), name="wall")
        if material == "plaster" and y0 < PLINTH:
            prop.box(((x0 + x1) / 2, PLINTH / 2, 0), (x1 - x0, PLINTH, THICK + 0.02), "stone_dark", grain="y", aligned=True,
                     lods=(0, 1), name="plinth")
    if opening is not None:
        # Far away the opening is still there: the same pieces in one material (keeps the silhouette, and the light
        # through a window).
        for x0, x1, y0, y1 in pieces:
            prop.box(((x0 + x1) / 2, (y0 + y1) / 2, 0), (x1 - x0, y1 - y0, THICK), material, grain="y", aligned=True, lods=(2,),
                     name="wall")
        # The timber frame: a lintel over the opening, posts at its sides, a sill under a window.
        prop.box((0, high + TIMBER / 2, 0), (w + 2 * TIMBER, TIMBER, THICK + 0.04), "wood_dark", grain="x", lods=(0, 1),
                 name="lintel")
        for sign in (-1, 1):
            prop.box((sign * (w / 2 - TIMBER / 4), (low + high) / 2, 0), (TIMBER / 2, high - low, THICK + 0.02), "wood_dark",
                     grain="y", lods=(0,), name="post")
        if low > 0:
            prop.box((0, low - 0.03, -0.02), (w + 0.12, 0.06, THICK + 0.08), "wood_dark", grain="x", lods=(0, 1), name="sill")
    for x0, x1, y0, y1 in pieces:
        prop.collide(((x0 + x1) / 2, (y0 + y1) / 2, 0), ((x1 - x0) / 2, (y1 - y0) / 2, THICK / 2))
