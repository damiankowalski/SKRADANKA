"""Wine racks: a frame of posts with rows of cradles, the bottles left out (props of their own later)."""

FAMILY = "wine_rack"
BUDGET = "furniture"
VARIANTS = {
    "wall": {"width": 1.4, "depth": 0.36, "height": 1.8, "rows": 6, "columns": 5, "material": "wood_dark"},
    "barrel": {"width": 2.2, "depth": 0.8, "height": 0.42, "rows": 1, "columns": 3, "material": "wood"},
}

POST = 0.06


def build(prop, width, depth, height, rows, columns, material):
    w, d, h = width, depth, height
    xs = [-w / 2 + POST / 2 + (w - POST) * c / columns for c in range(columns + 1)]
    for x in xs:
        for sign in (-1, 1):
            prop.box((x, h / 2, sign * (d / 2 - POST / 2)), (POST, h, POST), material, grain="y",
                     lods=(0, 1) if x not in (xs[0], xs[-1]) else (0, 1, 2), name="post")
    # Rails front and back on every row; a cradle of two slats across each bay.
    for r in range(rows + 1):
        y = 0.05 + (h - 0.1) * r / max(1, rows)
        for sign in (-1, 1):
            prop.box((0, y, sign * (d / 2 - POST / 2)), (w, 0.04, 0.03), material, grain="x", lods=(0, 1), name="rail")
        if r < rows or rows == 1:
            for x0, x1 in zip(xs, xs[1:]):
                for t in (0.3, 0.7):
                    prop.box((x0 + (x1 - x0) * t, y + 0.025, 0), (0.035, 0.02, d - 2 * POST), material, grain="z",
                             lods=(0,), name="cradle")
    prop.box((0, h - 0.02, 0), (w, 0.04, d), material, grain="x", lods=(2,), name="top")
    prop.collide((0, h / 2, 0), (w / 2, h / 2, d / 2))
