"""Plank doors (docs/art/World.md: planks with iron bands and nails): vertical boards, ledges and a brace behind,
strap hinges and a ring pull in front. The front faces -Z; the hinges are on the left (-X) seen from the front.
Sizes fit the blockout's doorways (1.4 m, Tools/BuildDemoProject.py door_in_plan)."""

import math

FAMILY = "door"
BUDGET = "furniture"
VARIANTS = {
    "house": {"width": 1.38, "height": 2.18, "boards": 7, "material": "wood"},
    "cellar": {"width": 1.18, "height": 1.98, "boards": 6, "material": "wood_dark"},
    "leaf": {"width": 0.74, "height": 2.38, "boards": 4, "material": "wood"},
}

THICK = 0.035    # the boards
LEDGE = 0.025    # ledges and brace behind


def build(prop, width, height, boards, material):
    w, h = width, height
    for n in range(boards):
        x = -w / 2 + w * (n + 0.5) / boards
        prop.box((x, h / 2, 0), (w / boards - 0.004, h, THICK), material, grain="y", board=True, lods=(0,), name="board")
    prop.box((0, h / 2, 0), (w, h, THICK), material, grain="y", lods=(1, 2), name="leaf")
    # Behind: two ledges and the brace between them, rising towards the hinge side.
    back = THICK / 2 + LEDGE / 2
    low, high = 0.3, h - 0.3
    for y in (low, high):
        prop.box((0, y, back), (w - 0.08, 0.14, LEDGE), material, grain="x", board=True, lods=(0, 1), name="ledge")
    span, rise = w - 0.2, high - low - 0.14
    angle = math.degrees(math.atan2(rise, span))
    prop.box((0, (low + high) / 2, back), (math.hypot(span, rise), 0.13, LEDGE), material, rotate=(0, 0, -angle),
             grain="x", board=True, lods=(0,), name="brace")
    # In front: strap hinges over the ledges, nails along them, a ring pull and its plate by the latch side.
    front = -THICK / 2 - 0.004
    for y in (low, high):
        prop.box((-w / 2 + w * 0.32, y, front), (w * 0.62, 0.05, 0.008), "iron", lods=(0, 1), name="strap")
        prop.box((-w / 2 + 0.02, y, front + 0.006), (0.05, 0.08, 0.02), "iron", lods=(0,), name="knuckle")
        for n in range(4):
            prop.box((-w / 2 + 0.1 + n * w * 0.16, y, front - 0.005), (0.016, 0.016, 0.006), "iron", lods=(0,), name="nail")
    plate_x = w / 2 - 0.16
    prop.box((plate_x, 1.0, front), (0.08, 0.12, 0.008), "iron", lods=(0, 1), name="plate")
    # The ring hangs in front of the plate: eight short bars round an octagon.
    for n in range(8):
        angle = 2 * math.pi * (n + 0.5) / 8
        cx, cy = plate_x + 0.045 * math.cos(angle), 0.93 + 0.045 * math.sin(angle)
        prop.box((cx, cy, front - 0.014), (0.012, 0.038, 0.012), "iron", rotate=(0, 0, math.degrees(angle)), lods=(0,),
                 name="ring")
    prop.collide((0, h / 2, 0), (w / 2, h / 2, THICK / 2 + LEDGE))
