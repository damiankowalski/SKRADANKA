"""Prop families: one module per family of objects (Tools/PropGen/README.md, "Families").

A family module has FAMILY (its name, the folder under Models/Props), BUDGET (a size class of config.json
"budgets"), VARIANTS ({variant: parameters}) and build(prop, **parameters), which adds the parts to a Prop with the
pivot at the middle of the base and may add collision boxes (one box around the prop when it adds none).
"""

from . import barrel, bench, chair, chest, crate, door, shelf, stool, table, wine_rack

FAMILIES = {module.FAMILY: module for module in (crate, barrel, chest, table, chair, stool, bench, shelf, wine_rack, door)}
