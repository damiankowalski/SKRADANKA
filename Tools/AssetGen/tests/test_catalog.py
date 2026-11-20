"""The texture catalog and regenerating a variant from its provenance: no model, runs anywhere (CTest Tools.AssetGen)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import catalog  # noqa: E402
import generate  # noqa: E402
from assetgen import config as cfg  # noqa: E402
from assetgen import processing as proc  # noqa: E402


def record(config: dict) -> dict:
    result = proc.build_provenance(
        material="cobblestone", template="prompts/cobblestone.json", prompt="old cobbles", seed=1001, steps=4,
        size=1024, quantize=8,
        model={"id": config["model"]["id"], "revision": config["model"]["revision"], "license": "Apache-2.0"},
        mflux_version=config["mflux_version"], license="Apache-2.0", seamless={"method": "generation", "passed": True},
        maps={"normal_strength": 5.0, "roughness": [0.6, 0.95], "metallic": 0.0}, timing={"image_seconds": 1.0},
        files={"albedo": "c_albedo.png", "normal": "c_normal.png", "metallic_roughness": "c_mr.png",
               "preview": "c_preview.png"},
    )
    result["blend_steps"] = 4
    return result


class RegenerateTests(unittest.TestCase):
    def setUp(self):
        self.config = cfg.load_config()

    def test_a_record_gives_back_everything_that_decides_the_pixels(self):
        plan = generate.plan_from_record(record(self.config), self.config, "cobblestone_1001")
        self.assertEqual((plan["material"], plan["name"], plan["seed"], plan["prompt"]),
                         ("cobblestone", "cobblestone_1001", 1001, "old cobbles"))
        self.assertEqual((plan["steps"], plan["size"], plan["quantize"], plan["blend_steps"]), (4, 1024, 8, 4))
        self.assertTrue(plan["seamless"])
        self.assertEqual(plan["maps"], {"normal_strength": 5.0, "roughness": [0.6, 0.95], "metallic": 0.0})

    def test_another_model_revision_or_mflux_version_is_refused(self):
        other = record(self.config)
        other["model"]["revision"] = "0" * 40
        with self.assertRaisesRegex(generate.RecordMismatch, "config.json pins"):
            generate.plan_from_record(other, self.config, "x")
        other = record(self.config)
        other["mflux_version"] = "0.1.0"
        with self.assertRaisesRegex(generate.RecordMismatch, "mflux"):
            generate.plan_from_record(other, self.config, "x")
        other = record(self.config)
        other.pop("maps")
        with self.assertRaisesRegex(generate.RecordMismatch, "maps"):
            generate.plan_from_record(other, self.config, "x")

    def test_from_refuses_a_mismatched_record_before_loading_the_model(self):
        other = record(self.config)
        other["model"]["revision"] = "0" * 40
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "cobblestone_1001.json"
            path.write_text(json.dumps(other), encoding="utf-8")
            self.assertEqual(generate.main(["--from", str(path), "--out", folder]), 2)
        self.assertNotIn("mflux", sys.modules)

    def test_every_catalog_record_can_be_regenerated(self):
        for path, entry in catalog.entries():
            with self.subTest(path.name):
                generate.plan_from_record(entry, self.config, path.stem)


class CatalogTests(unittest.TestCase):
    def make_variant(self, folder: Path) -> Path:
        out = folder / "out"
        out.mkdir()
        Image.fromarray(np.full((1024, 1024, 3), 90, np.uint8)).save(out / "c_preview.png")
        path = out / "cobblestone_1001.json"
        path.write_text(json.dumps(record(cfg.load_config())), encoding="utf-8")
        return path

    def test_add_keeps_the_provenance_a_note_and_a_small_preview(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            added = catalog.add(self.make_variant(root), "dark setts", root / "catalog")
            kept = json.loads(added.read_text(encoding="utf-8"))
            self.assertEqual(proc.validate_provenance(kept), [])
            self.assertEqual(kept["catalog"]["note"], "dark setts")
            with Image.open(added.with_suffix(".png")) as preview:
                self.assertEqual(preview.size, (catalog.PREVIEW_SIZE, catalog.PREVIEW_SIZE))

    def test_add_wants_a_note_and_complete_provenance(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = self.make_variant(root)
            with self.assertRaisesRegex(catalog.CatalogError, "note"):
                catalog.add(path, "  ", root / "catalog")
            broken = json.loads(path.read_text(encoding="utf-8"))
            del broken["seed"]
            path.write_text(json.dumps(broken), encoding="utf-8")
            with self.assertRaisesRegex(catalog.CatalogError, "missing seed"):
                catalog.add(path, "dark setts", root / "catalog")

    def test_the_index_says_where_the_game_uses_a_variant_and_check_sees_a_stale_one(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            shelf, assets = root / "catalog", root / "Assets"
            catalog.add(self.make_variant(root), "dark setts", shelf)
            (shelf / "README.md").write_text(catalog.render_index(shelf, assets), encoding="utf-8")
            self.assertIn("| no |", catalog.render_index(shelf, assets))
            self.assertEqual(catalog.check(shelf, assets), [])

            (assets / "Textures" / "Cobblestone").mkdir(parents=True)
            (assets / "Textures" / "Cobblestone" / "cobblestone_1001_albedo.png").write_bytes(b"")
            self.assertIn("`Textures/Cobblestone`", catalog.render_index(shelf, assets))
            self.assertTrue(any("stale" in problem for problem in catalog.check(shelf, assets)))

            (shelf / "cobblestone" / "cobblestone_1001.png").unlink()
            self.assertTrue(any("no preview" in problem for problem in catalog.check(shelf, assets)))

    def test_the_repository_catalog_is_complete_and_its_readme_current(self):
        self.assertTrue(catalog.entries(), "the catalog is empty")
        self.assertEqual(catalog.check(), [])


if __name__ == "__main__":
    unittest.main()
