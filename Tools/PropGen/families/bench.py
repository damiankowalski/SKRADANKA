"""Benches: a thick plank on two trestle legs, a stretcher between them."""

FAMILY = "bench"
BUDGET = "furniture"
VARIANTS = {
    "short": {"length": 1.2, "width": 0.3, "height": 0.45, "material": "wood"},
    "long": {"length": 2.4, "width": 0.32, "height": 0.46, "material": "wood_dark"},
}

PLANK = 0.05


def build(prop, length, width, height, material):
    l, w, h = length, width, height
    prop.box((0, h - PLANK / 2, 0), (l, PLANK, w), material, grain="x", board=False, lods=(0, 1, 2), name="seat")
    legs = h - PLANK
    inset = 0.18 if l < 2 else 0.3
    for sign in (-1, 1):
        x = sign * (l / 2 - inset)
        prop.box((x, legs / 2, 0), (0.05, legs, w - 0.04), material, grain="y", board=True, lods=(0, 1, 2), name="trestle")
        prop.box((x, legs - 0.03, 0), (0.07, 0.06, w), material, grain="z", lods=(0,), name="cleat")
        prop.box((x, 0.03, 0), (0.08, 0.06, w + 0.04), material, grain="z", lods=(0, 1), name="foot")
    prop.box((0, 0.18, 0), (l - 2 * inset, 0.06, 0.04), material, grain="x", lods=(0, 1), name="stretcher")
    prop.collide((0, h / 2, 0), (l / 2, h / 2, w / 2))
