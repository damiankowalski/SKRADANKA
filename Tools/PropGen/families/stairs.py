"""Stairs: straight stone flights between floors 4 m apart (25 risers of 0.16 m: the navmesh's climb limit, as in
the blockout), and short flights of a few steps. They climb towards +Z; the pivot is on the floor under the middle
of the run, centred across its width."""

FAMILY = "stairs"
BUDGET = "big"
VARIANTS = {
    "storey": {"steps": 25, "rise": 0.16, "tread": 0.28, "width": 1.2, "material": "stone_dark"},
    "short": {"steps": 4, "rise": 0.16, "tread": 0.32, "width": 1.6, "material": "stone"},
    "cellar": {"steps": 12, "rise": 0.16, "tread": 0.26, "width": 1.0, "material": "stone_dark"},
}


def build(prop, steps, rise, tread, width, material):
    run = steps * tread
    for n in range(steps):
        # Each step a solid block down to the floor: no gaps under the flight, and its collision follows.
        top = (n + 1) * rise
        prop.box((0, top / 2, -run / 2 + tread * (n + 0.5)), (width, top, tread), material, grain="y", lods=(0,),
                 name="step")
        prop.collide((0, top / 2, -run / 2 + tread * (n + 0.5)), (width / 2, top / 2, tread / 2))
    # Further: steps in pairs (a double riser reads the same from afar).
    for n in range(0, steps, 2):
        count = min(2, steps - n)
        top = (n + count) * rise
        z0 = -run / 2 + tread * n
        prop.box((0, top / 2, z0 + tread * count / 2), (width, top, tread * count), material, grain="y", lods=(1,),
                 name="steps")
    # Far away: a ramp-like stack of four blocks.
    blocks = max(1, min(4, steps // 3))
    for n in range(blocks):
        z0, z1 = -run / 2 + run * n / blocks, -run / 2 + run * (n + 1) / blocks
        top = steps * rise * (n + 1) / blocks
        prop.box((0, top / 2, (z0 + z1) / 2), (width, top, z1 - z0), material, grain="y", lods=(2,), name="block")
