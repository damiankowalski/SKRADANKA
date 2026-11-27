"""Candlesticks: an iron foot and stem with a drip pan, a candle of wax; the tall church pricket stands on the floor."""

FAMILY = "candlestick"
BUDGET = "clutter"
VARIANTS = {
    "table": {"foot": 0.06, "stem": 0.14, "candle": 0.12, "candle_radius": 0.012},
    "pricket": {"foot": 0.16, "stem": 1.1, "candle": 0.3, "candle_radius": 0.028},
}


def build(prop, foot, stem, candle, candle_radius):
    pan = 0.03 + stem
    top = pan + 0.012 + candle
    for lods, sides in (((0,), 10), ((1,), 6)):
        prop.lathe([(foot, 0.0), (foot, 0.012), (foot * 0.3, 0.03)], sides, "iron", caps=(True, True), lods=lods, name="foot")
        prop.lathe([(0.008 + foot * 0.06, 0.03), (0.008 + foot * 0.06, pan)], 6, "iron", lods=lods, name="stem")
        prop.lathe([(candle_radius * 2.4, pan), (candle_radius * 2.6, pan + 0.012)], sides, "iron", caps=(True, True),
                   lods=lods, name="pan")
        prop.lathe([(candle_radius, pan + 0.012), (candle_radius * 0.92, top)], sides - 2, "wax", caps=(False, True),
                   lods=lods, name="candle")
    # Far away: the foot as a cone up the stem, and the candle.
    prop.lathe([(foot, 0.0), (0.01, pan)], 4, "iron", caps=(True, False), lods=(2,), name="stand")
    prop.lathe([(candle_radius, pan), (candle_radius, top)], 4, "wax", caps=(False, True), lods=(2,), name="candle")
    prop.collide((0, top / 2, 0), (foot, top / 2, foot))
