"""Stools: a round or square seat on three or four splayed legs."""

import math

FAMILY = "stool"
BUDGET = "clutter"
VARIANTS = {
    "milking": {"height": 0.32, "radius": 0.15, "legs": 3, "round": True, "material": "wood"},
    "tavern": {"height": 0.62, "radius": 0.17, "legs": 4, "round": False, "material": "wood_dark"},
}

SEAT = 0.04
SPLAY = 8.0  # degrees the legs lean out


def build(prop, height, radius, legs, round, material):
    if round:
        prop.lathe([(radius, height - SEAT), (radius, height)], 12, material, caps=(True, True), lods=(0, 1), name="seat")
        prop.lathe([(radius, height - SEAT), (radius, height)], 6, material, caps=(True, True), lods=(2,), name="seat")
    else:
        prop.box((0, height - SEAT / 2, 0), (2 * radius, SEAT, 2 * radius), material, grain="x", lods=(0, 1, 2), name="seat")
    leg_height = height - SEAT
    reach = radius * 0.6
    for n in range(legs):
        angle = 2 * math.pi * n / legs + (math.pi / 4 if legs == 4 else math.pi / 2)
        tilt = math.radians(SPLAY)
        # The leg's middle sits halfway down, pushed out by half its lean.
        out = reach + math.tan(tilt) * leg_height / 2
        x, z = out * math.cos(angle), out * math.sin(angle)
        yaw = -math.degrees(angle)
        # Tilted, the leg's lower corner would dip below the floor: lift it by that much and shorten it to match.
        lift = 0.035 / 2 * math.sin(tilt)
        prop.box((x, leg_height / 2 + lift / 2, z), (0.035, (leg_height - lift) / math.cos(tilt), 0.035), material,
                 rotate=(0, yaw, -SPLAY), grain="y", lods=(0, 1, 2), name="leg")
    prop.collide((0, height / 2, 0), (radius, height / 2, radius))
