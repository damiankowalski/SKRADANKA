"""Chairs: a board seat on four legs, the back legs rising into a back of rails (or slats)."""

import math

FAMILY = "chair"
BUDGET = "furniture"
VARIANTS = {
    "plain": {"seat": 0.44, "seat_height": 0.45, "back_height": 0.92, "slats": 0, "material": "wood"},
    "slatted": {"seat": 0.46, "seat_height": 0.46, "back_height": 1.0, "slats": 4, "material": "wood_dark"},
}

LEG = 0.04
SEAT = 0.03
LEAN = 8.0  # degrees the back leans back


def build(prop, seat, seat_height, back_height, slats, material):
    s, sh = seat, seat_height
    half = s / 2 - LEG / 2
    for n in range(3):
        z = -s / 2 + s * (n + 0.5) / 3
        prop.box((0, sh - SEAT / 2, z), (s, SEAT, s / 3 - 0.004), material, grain="x", board=True, lods=(0,), name="seat board")
    prop.box((0, sh - SEAT / 2, 0), (s, SEAT, s), material, grain="x", lods=(1, 2), name="seat")
    legs = sh - SEAT
    for sx in (-1, 1):
        prop.box((sx * half, legs / 2, -half), (LEG, legs, LEG), material, grain="y", lods=(0, 1), name="front leg")
    # The back legs run from the floor through the seat to the top rail, leaning back above the seat.
    for sx in (-1, 1):
        prop.box((sx * half, legs / 2, half), (LEG, legs, LEG), material, grain="y", lods=(0, 1), name="back leg")
        # Far away a side's two legs read as one thin frame.
        prop.box((sx * half, legs / 2, 0), (LEG, legs, s - 0.01), material, grain="y", lods=(2,), name="side frame")
    upper = back_height - sh
    lean = math.radians(LEAN)
    top_z = half + math.sin(lean) * upper
    for sx in (-1, 1):
        prop.box((sx * half, sh + upper / 2, half + math.sin(lean) * upper / 2), (LEG, upper, LEG), material,
                 rotate=(LEAN, 0, 0), grain="y", lods=(0, 1), name="back post")
    prop.box((0, sh + upper / 2, half + math.sin(lean) * upper / 2), (s, upper, LEG), material, rotate=(LEAN, 0, 0),
             grain="x", lods=(2,), name="back")
    rails = [(back_height - 0.06, 0.08)] + ([(sh + 0.18, 0.05)] if not slats else [(sh + 0.12, 0.04)])
    for y, tall in rails:
        z = half + math.sin(lean) * (y - sh)
        prop.box((0, y, z), (2 * half - LEG, tall, 0.022), material, rotate=(LEAN, 0, 0), grain="x", board=True,
                 lods=(0, 1), name="rail")
    if slats:
        low, high = sh + 0.14, back_height - 0.1
        for n in range(slats):
            x = -half + LEG / 2 + (2 * half - LEG) * (n + 0.5) / slats
            y = (low + high) / 2
            z = half + math.sin(lean) * (y - sh)
            prop.box((x, y, z), (0.035, high - low, 0.016), material, rotate=(LEAN, 0, 0), grain="y", lods=(0,), name="slat")
    for sx in (-1, 1):
        for y in (0.15,):
            prop.box((sx * half, y, 0), (0.025, 0.03, 2 * half - LEG), material, grain="z", lods=(0,), name="stretcher")
    prop.collide((0, sh / 2, 0), (s / 2, sh / 2, s / 2))
    prop.collide((0, (sh + back_height) / 2, (half + top_z) / 2), (s / 2, (back_height - sh) / 2, LEG + (top_z - half) / 2))
