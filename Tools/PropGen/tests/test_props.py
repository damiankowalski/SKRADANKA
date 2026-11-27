"""PropGen tests: the mesh kit, the .glb writer and reader, the checks, and the committed props matching their
scripts. Python's standard library only (run by CTest as Tools.PropGen, Tools/PropGen/README.md)."""

import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))

import generate  # noqa: E402
from families import FAMILIES  # noqa: E402
from propgen import check, glb  # noqa: E402
from propgen.mesh import Prop  # noqa: E402

SETTINGS = generate.config()
ASSETS = generate.REPO / "DemoProject" / "Assets"


def one_box(size=(1.0, 0.5, 0.4), centre=(0, 0.25, 0), rotate=(0, 0, 0)):
    prop = Prop(SETTINGS["materials"], 1)
    prop.box(centre, size, "wood", rotate=rotate)
    return prop


def facing_out(prop):
    """Every face's points wind counter-clockwise around its normal, and the normal points away from the middle."""
    low, high = prop.bounds(0)
    middle = [(low[i] + high[i]) / 2 for i in range(3)]
    for part in prop.parts:
        for face in part.faces:
            a, b, c = face.points[:3]
            ab, ac = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
            n = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
            if sum(n[i] * face.normal[i] for i in range(3)) <= 0:
                return False
            centre = [sum(p[i] for p in face.points) / len(face.points) for i in range(3)]
            if sum((centre[i] - middle[i]) * face.normal[i] for i in range(3)) <= 0:
                return False
    return True


class MeshTests(unittest.TestCase):
    def test_a_box_has_six_outward_faces_and_twelve_triangles(self):
        prop = one_box()
        self.assertEqual(prop.triangles(), 12)
        self.assertTrue(facing_out(prop))
        low, high = prop.bounds()
        self.assertEqual([round(v, 6) for v in low], [-0.5, 0.0, -0.2])
        self.assertEqual([round(v, 6) for v in high], [0.5, 0.5, 0.2])

    def test_a_turned_box_still_faces_out(self):
        prop = one_box(rotate=(20, 35, -10))
        self.assertTrue(facing_out(prop))

    def test_uvs_are_in_metres_along_the_grain(self):
        prop = Prop(SETTINGS["materials"], 2)
        part = prop.box((0, 0, 0), (2.0, 0.1, 0.3), "wood", grain="x")
        top = next(f for f in part.faces if f.normal == (0.0, 1.0, 0.0))
        us = [uv[0] for uv in top.uvs]
        vs = [uv[1] for uv in top.uvs]
        self.assertAlmostEqual(max(vs) - min(vs), 2.0)       # V runs along the 2 m grain
        self.assertAlmostEqual(max(us) - min(us), 0.3)

    def test_a_board_shows_one_of_the_textures_boards(self):
        prop = Prop(SETTINGS["materials"], 3)
        part = prop.box((0, 0, 0), (0.9, 0.12, 0.02), "wood", grain="x", board=True)
        front = next(f for f in part.faces if f.normal == (0.0, 0.0, 1.0))
        us = sorted({round(uv[0], 6) for uv in front.uvs})
        boards = SETTINGS["materials"]["wood"]["boards_per_repeat"]
        self.assertAlmostEqual(us[1] - us[0], 1 / boards)
        self.assertAlmostEqual((us[0] * boards) % 1, 0.0, places=5)

    def test_a_lathe_faces_out_with_its_caps(self):
        prop = Prop(SETTINGS["materials"], 4)
        prop.lathe([(0.3, 0.0), (0.35, 0.4), (0.3, 0.8)], 12, "wood", caps=(True, True))
        self.assertTrue(facing_out(prop))
        self.assertEqual(prop.triangles(), 12 * 2 * 2 + 2 * 10)

    def test_the_same_seed_builds_the_same_prop(self):
        a, b = (generate.build(FAMILIES["barrel"], "wine", SETTINGS)[0] for _ in range(2))
        self.assertEqual(a.primitives(0), b.primitives(0))


class GlbTests(unittest.TestCase):
    def test_a_prop_round_trips(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "Models" / "box.glb"
            path.parent.mkdir()
            prop = one_box()
            glb.write(path, "box", prop.primitives(0), SETTINGS["materials"], folder)
            gltf, binary = glb.read(path)
            self.assertEqual(gltf["asset"]["version"], "2.0")
            primitive = gltf["meshes"][0]["primitives"][0]
            positions = glb.read_accessor(gltf, binary, primitive["attributes"]["POSITION"])
            self.assertEqual(len(positions), 24)
            self.assertEqual(len(glb.read_accessor(gltf, binary, primitive["indices"])), 36)
            accessor = gltf["accessors"][primitive["attributes"]["POSITION"]]
            self.assertEqual(accessor["min"], [-0.5, 0.0, -0.20000000298023224])
            # Textures are referenced, not embedded: relative to the .glb, inside the assets.
            self.assertEqual(gltf["images"][0]["uri"], "../" + SETTINGS["materials"]["wood"]["albedo"])
            self.assertEqual(gltf["samplers"][0]["wrapS"], glb.REPEAT)

    def test_writing_twice_gives_the_same_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            prop = generate.build(FAMILIES["chair"], "slatted", SETTINGS)[0]
            a = glb.write(Path(folder) / "a.glb", "c", prop.primitives(0), SETTINGS["materials"], folder)
            b = glb.write(Path(folder) / "b.glb", "c", prop.primitives(0), SETTINGS["materials"], folder)
            self.assertEqual(a, b)

    def test_broken_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            for name, data in (("short.glb", b"glTF"), ("text.glb", b"not a glb at all, just some words here"),
                               ("version.glb", struct.pack("<III", 0x46546C67, 1, 12))):
                path = Path(folder) / name
                path.write_bytes(data)
                with self.assertRaises(ValueError, msg=name):
                    glb.read(path)
                result, problems = check.inspect(path, folder)
                self.assertTrue(problems and "unreadable" in problems[0], name)


class CheckTests(unittest.TestCase):
    def write_prop(self, folder, prop, budget="furniture", stem="thing_one"):
        root = Path(folder) / "thing"
        root.mkdir(parents=True, exist_ok=True)
        files = {}
        for lod in range(3):
            name = f"{stem}.glb" if lod == 0 else f"{stem}_lod{lod}.glb"
            glb.write(root / name, stem, prop.primitives(lod), SETTINGS["materials"], ASSETS)
            files[f"lod{lod}"] = name
        meta = root / f"{stem}.json"
        meta.write_text(json.dumps({"budget": budget, "files": files, "collision": [{"center": [0, 0, 0], "half_extents": [1, 1, 1]}],
                                    "triangles": [prop.triangles(l) for l in range(3)]}), encoding="utf-8")
        return meta

    def test_a_good_prop_passes(self):
        with tempfile.TemporaryDirectory() as folder:
            prop = Prop(SETTINGS["materials"], 5)
            prop.box((0, 0.5, 0), (1, 1, 1), "wood", lods=(0, 1, 2))
            self.assertEqual(check.check_prop(self.write_prop(folder, prop), SETTINGS, ASSETS), [])

    def test_a_floating_prop_off_its_pivot_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            prop = Prop(SETTINGS["materials"], 6)
            prop.box((0.6, 0.7, 0), (1, 1, 1), "wood")
            problems = check.check_prop(self.write_prop(folder, prop), SETTINGS, ASSETS)
            self.assertTrue(any("not on the pivot" in p for p in problems), problems)
            self.assertTrue(any("off the pivot along x" in p for p in problems), problems)

    def test_inside_out_faces_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            prop = Prop(SETTINGS["materials"], 7)
            part = prop.box((0, 0.5, 0), (1, 1, 1), "wood")
            for face in part.faces:
                face.points.reverse()
                face.uvs.reverse()
            problems = check.check_prop(self.write_prop(folder, prop), SETTINGS, ASSETS)
            self.assertTrue(any("face away" in p for p in problems), problems)

    def test_over_budget_and_heavy_lods_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            prop = Prop(SETTINGS["materials"], 8)
            for n in range(50):  # 600 triangles in every LOD
                prop.box((0, 0.5, 0), (1, 1, 1 - n / 100), "wood")
            problems = check.check_prop(self.write_prop(folder, prop, budget="clutter"), SETTINGS, ASSETS)
            self.assertTrue(any("over the clutter budget" in p for p in problems), problems)
            self.assertTrue(any("LOD1 has 600" in p for p in problems), problems)

    def test_a_missing_texture_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            materials = {"ghost": {"albedo": "Textures/Nowhere/none_albedo.png"}}
            prop = Prop(materials, 9)
            prop.box((0, 0.5, 0), (1, 1, 1), "ghost")
            path = Path(folder) / "ghost.glb"
            glb.write(path, "ghost", prop.primitives(0), materials, ASSETS)
            _, problems = check.inspect(path, ASSETS)
            self.assertTrue(any("is missing" in p for p in problems), problems)


class CommittedPropsTests(unittest.TestCase):
    def test_every_committed_prop_passes_the_checks(self):
        self.assertEqual(check.check_folder(generate.REPO / SETTINGS["output"], SETTINGS, ASSETS), [])

    def test_the_committed_props_are_what_the_scripts_make(self):
        # Like the demo's scenes: the scripts are the source, the files must be regenerated after a change.
        out = generate.REPO / SETTINGS["output"]
        with tempfile.TemporaryDirectory() as folder:
            fresh = Path(folder) / "Assets" / "Models" / "Props"
            generate.generate(list(FAMILIES), SETTINGS, ASSETS, fresh)
            for path in sorted(fresh.rglob("*")):
                if path.is_dir():
                    continue
                committed = out / path.relative_to(fresh)
                self.assertTrue(committed.is_file(), f"{committed} is missing: run Tools/PropGen/generate.py")
                if path.suffix == ".glb":
                    # Image URIs differ (relative to another folder); everything else must be the same.
                    a, _ = glb.read(path)
                    b, _ = glb.read(committed)
                    for gltf in (a, b):
                        gltf.pop("images", None)
                    self.assertEqual(a, b, f"{committed.name} is stale: run Tools/PropGen/generate.py")
                else:
                    self.assertEqual(json.loads(path.read_text()), json.loads(committed.read_text()),
                                     f"{committed.name} is stale: run Tools/PropGen/generate.py")

    def test_every_family_is_well_formed(self):
        for name, module in FAMILIES.items():
            self.assertEqual(name, module.FAMILY)
            self.assertIn(module.BUDGET, SETTINGS["budgets"])
            self.assertTrue(module.VARIANTS, name)
            for variant in module.VARIANTS:
                self.assertRegex(variant, r"^[a-z][a-z0-9_]*$")


if __name__ == "__main__":
    unittest.main()
