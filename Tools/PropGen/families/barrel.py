"""Barrels: staves bulging round the middle, iron hoops, a head of boards at each end."""

FAMILY = "barrel"
BUDGET = "furniture"
VARIANTS = {
    "wine": {"height": 0.95, "radius": 0.30, "bulge": 0.055, "staves": 18, "hoops": 4, "material": "wood"},
    "water": {"height": 0.75, "radius": 0.26, "bulge": 0.04, "staves": 16, "hoops": 3, "material": "wood_dark"},
    "keg": {"height": 0.5, "radius": 0.18, "bulge": 0.03, "staves": 14, "hoops": 2, "material": "wood"},
}

RINGS = 6        # profile rings from the bottom to the top (the bulge's smoothness)
CHIME = 0.025    # the staves stand this far above and below the heads


def radius_at(t, radius, bulge):
    """Radius at height fraction t (0 bottom, 1 top): a parabola, widest in the middle."""
    return radius + bulge * (1 - (2 * t - 1) ** 2)


def build(prop, height, radius, bulge, staves, hoops, material):
    profile = [(radius_at(i / RINGS, radius, bulge), height * i / RINGS) for i in range(RINGS + 1)]
    prop.lathe(profile, staves, material, board=True, lods=(0,), name="staves")
    coarse = [profile[0], profile[RINGS // 2], profile[-1]]
    prop.lathe(coarse, max(8, staves * 2 // 3), material, board=True, lods=(1,), name="staves")
    prop.lathe(coarse, 8, material, lods=(2,), name="staves", caps=(False, True))
    # The heads, set into the staves (the chime).
    for y in (CHIME, height - CHIME):
        r = radius_at(y / height, radius, bulge) - 0.012
        top = y > height / 2
        prop.lathe([(r, y - 0.01), (r, y)] if top else [(r, y), (r, y + 0.01)], staves, material, lods=(0, 1),
                   name="head", caps=(not top, top))
    # Iron hoops: thin bands just outside the staves.
    places = {2: (0.12, 0.88), 3: (0.1, 0.5, 0.9), 4: (0.08, 0.3, 0.7, 0.92)}[hoops]
    for t in places:
        band = 0.03 if t in (places[0], places[-1]) else 0.04
        r0 = radius_at(max(0.0, t - band / height / 2), radius, bulge) + 0.004
        r1 = radius_at(min(1.0, t + band / height / 2), radius, bulge) + 0.004
        y = t * height
        prop.lathe([(r0, y - band / 2), (r1, y + band / 2)], staves, "iron", lods=(0, 1), name="hoop")
    widest = radius + bulge
    prop.collide((0, height / 2, 0), (widest * 0.85, height / 2, widest * 0.85))
