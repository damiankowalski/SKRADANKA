"""Post-processing tests: no model, no Apple Silicon (run by CTest as Tools.AssetGen, Tools/AssetGen/README.md)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from assetgen import config as cfg  # noqa: E402
from assetgen import processing as proc  # noqa: E402

SIZE = 512


def periodic_texture(seed: int = 1, size: int = SIZE) -> np.ndarray:
    """Stone-like noise that tiles by construction (filtered on the torus), in colour."""
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal((size, size, 3)).astype(np.float32)
    texture = proc.blur(noise, 1.5) * 0.6 + proc.blur(noise, 6.0) * 2.5
    return np.clip(0.5 + 0.25 * texture / texture.std(), 0.0, 1.0)


def cropped_texture(seed: int = 1, size: int = SIZE) -> np.ndarray:
    """The same kind of noise cut out of a larger field: its edges do not meet."""
    return periodic_texture(seed, size * 2)[:size, :size]


class SeamTests(unittest.TestCase):
    def test_a_tiling_texture_passes(self):
        report = proc.seam_report(periodic_texture())
        self.assertTrue(report["passed"], report)
        self.assertLess(report["x"]["wrap_step"], proc.SEAM_LIMIT)

    def test_a_cut_out_texture_has_a_seam(self):
        report = proc.seam_report(cropped_texture())
        self.assertFalse(report["passed"], report)
        self.assertGreater(max(report["x"]["wrap_step"], report["y"]["wrap_step"]), proc.SEAM_LIMIT)

    def test_a_blurred_cross_through_the_middle_fails(self):
        # What a naive half-tile offset-and-blend leaves: no step at the edge, a soft band through the centre.
        texture = periodic_texture()
        soft = proc.blur(texture, 4.0)
        band = slice(SIZE // 2 - 24, SIZE // 2 + 24)
        texture[:, band] = soft[:, band]
        texture[band, :] = soft[band, :]
        report = proc.seam_report(texture)
        self.assertFalse(report["passed"], report)
        self.assertLess(report["x"]["detail_at_centre"], proc.DETAIL_LIMIT)
        self.assertAlmostEqual(report["x"]["weakest_detail_at"], SIZE // 2, delta=30)

    def test_a_faint_tone_step_along_part_of_the_middle_fails(self):
        # What a cut left in a rolled copy looks like: one stone a shade lighter right of the centre line.
        texture = periodic_texture()
        texture[SIZE // 4: SIZE // 4 + 160, SIZE // 2: SIZE // 2 + 90] += 0.08
        report = proc.seam_report(np.clip(texture, 0.0, 1.0))
        self.assertFalse(report["passed"], report)
        self.assertGreater(report["x"]["line_at_centre"], report["x"]["line_limit"])
        self.assertLess(report["x"]["line_at_edge"], report["x"]["line_limit"])
        self.assertFalse(report["fallback_can_help"])

    def test_a_strong_joint_lying_on_the_cut_is_no_seam(self):
        # A wall whose own vertical joints are as strong as the one that falls on the centre line: no seam.
        texture = periodic_texture() * 0.6 + 0.3
        for x in range(0, SIZE, SIZE // 32):
            texture[:, x: x + 4] *= 0.4
        report = proc.seam_report(texture)
        self.assertTrue(report["passed"], report)
        self.assertFalse(report["fallback_can_help"])

    def test_the_fallback_makes_a_cut_out_texture_tile_without_a_soft_cross(self):
        fixed = proc.make_tileable(cropped_texture())
        report = proc.seam_report(fixed)
        self.assertTrue(report["passed"], report)
        self.assertGreater(report["x"]["detail_at_centre"], proc.DETAIL_LIMIT)
        self.assertGreater(report["y"]["detail_at_centre"], proc.DETAIL_LIMIT)

    def test_blur_wraps(self):
        texture = periodic_texture()
        self.assertTrue(proc.seam_report(proc.blur(texture, 2.0))["passed"])


class NormalMapTests(unittest.TestCase):
    def bump(self) -> np.ndarray:
        y, x = np.mgrid[0:64, 0:64].astype(np.float32)
        return np.exp(-((x - 32) ** 2 + (y - 32) ** 2) / 60.0)

    def test_a_flat_height_points_straight_out(self):
        normal = proc.normal_map(np.full((16, 16), 0.3, np.float32), strength=5.0)
        self.assertTrue(np.allclose(normal, [0.0, 0.0, 1.0]))
        self.assertTrue(np.allclose(proc.encode_normal(normal)[0, 0], [0.5, 0.5, 1.0]))

    def test_a_bump_leans_outwards_in_gltf_convention(self):
        # glTF / Penumbra: +X right, +Y up the image (green), +Z out. A bump's right flank faces right,
        # its upper flank (smaller rows) faces up.
        normal = proc.normal_map(self.bump(), strength=10.0)
        self.assertGreater(normal[32, 40, 0], 0.1)   # right flank: +X
        self.assertLess(normal[32, 24, 0], -0.1)     # left flank: -X
        self.assertGreater(normal[24, 32, 1], 0.1)   # upper flank: +Y
        self.assertLess(normal[40, 32, 1], -0.1)     # lower flank: -Y
        self.assertTrue(np.allclose(np.linalg.norm(normal, axis=-1), 1.0, atol=1e-5))

    def test_normals_tile_with_their_height(self):
        height = proc.height_from_albedo(periodic_texture())
        encoded = proc.encode_normal(proc.normal_map(height, strength=4.0))
        self.assertTrue(proc.seam_report(encoded)["passed"])

    def test_metallic_roughness_follows_gltf_channels(self):
        height = np.array([[0.0, 1.0]], np.float32)
        mr = proc.metallic_roughness(height, roughness=(0.6, 0.9), metallic=0.0)
        self.assertTrue(np.allclose(mr[0, 0], [1.0, 0.9, 0.0]))  # low (mortar): rough end
        self.assertTrue(np.allclose(mr[0, 1], [1.0, 0.6, 0.0]))  # high (stone face): smooth end


class ProvenanceTests(unittest.TestCase):
    def record(self) -> dict:
        return proc.build_provenance(
            material="stone_wall", template="prompts/stone_wall.json", prompt="a stone wall", seed=1, steps=4,
            size=1024, quantize=8,
            model={"id": "black-forest-labs/FLUX.2-klein-4B", "revision": "abc", "license": "Apache-2.0"},
            mflux_version="0.24.0", license="Apache-2.0", seamless={"method": "generation", "passed": True},
            maps={"normal_strength": 4.0}, timing={"seconds": 1.0},
            files={"albedo": "a.png", "normal": "n.png", "metallic_roughness": "mr.png", "preview": "p.png"},
        )

    def test_a_complete_record_validates_and_says_ai_generated(self):
        record = json.loads(json.dumps(self.record()))
        self.assertEqual(proc.validate_provenance(record), [])
        self.assertIs(record["ai_generated"], True)

    def test_missing_or_wrong_fields_are_reported(self):
        record = self.record()
        del record["seed"]
        record["model"].pop("revision")
        record["ai_generated"] = False
        problems = proc.validate_provenance(record)
        self.assertIn("missing seed", problems)
        self.assertIn("missing model.revision", problems)
        self.assertIn("ai_generated must be true", problems)


class ConfigTests(unittest.TestCase):
    def test_the_pinned_model_is_allowed_and_pinned_to_a_revision(self):
        config = cfg.load_config()
        allowed = cfg.check_model_allowed(config, config["model"]["id"])
        self.assertEqual(allowed["license"], "Apache-2.0")
        self.assertRegex(config["model"]["revision"], r"^[0-9a-f]{40}$")

    def test_the_non_commercial_9b_is_refused(self):
        with self.assertRaisesRegex(cfg.ModelNotAllowed, "Non-Commercial"):
            cfg.check_model_allowed(cfg.load_config(), "black-forest-labs/FLUX.2-klein-9B")

    def test_a_model_off_the_list_is_refused(self):
        with self.assertRaisesRegex(cfg.ModelNotAllowed, "not on the allowed list"):
            cfg.check_model_allowed(cfg.load_config(), "someone/another-model")

    def test_generate_refuses_a_model_off_the_list_before_loading_anything(self):
        import generate  # noqa: E402  (the CLI; must not import mflux to refuse)

        with tempfile.TemporaryDirectory() as folder:
            code = generate.main(["stone_wall", "--model", "black-forest-labs/FLUX.2-klein-9B", "--out", folder])
        self.assertNotEqual(code, 0)
        self.assertNotIn("mflux", sys.modules)

    def test_reprocess_rebuilds_maps_and_provenance_without_the_model(self):
        import generate  # noqa: E402
        from PIL import Image

        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / "stone_wall"
            out.mkdir()
            albedo = (periodic_texture(size=256) * 255).astype(np.uint8)
            Image.fromarray(albedo).save(out / "stone_wall_7_albedo.png")
            record = ProvenanceTests.record(None)
            record.pop("seamless"), record.pop("maps"), record.pop("files")
            record["files"] = {"albedo": "stone_wall_7_albedo.png"}
            (out / "stone_wall_7.json").write_text(json.dumps(record), encoding="utf-8")

            code = generate.main(["stone_wall", "--reprocess", "--out", folder, "--sheet", str(Path(folder) / "s.png")])
            self.assertEqual(code, 0)
            rebuilt = json.loads((out / "stone_wall_7.json").read_text(encoding="utf-8"))
            self.assertEqual(proc.validate_provenance(rebuilt), [])
            self.assertTrue(rebuilt["seamless"]["passed"])
            for key in ("normal", "metallic_roughness", "preview"):
                self.assertTrue((out / rebuilt["files"][key]).is_file(), key)
            self.assertTrue((Path(folder) / "s.png").is_file())
        self.assertNotIn("mflux", sys.modules)

    def test_hf_home_follows_the_environment(self):
        import os

        config = cfg.load_config()
        saved = os.environ.get("HF_HOME")
        elsewhere = Path(tempfile.gettempdir()) / "elsewhere"  # a real absolute path on every platform
        try:
            os.environ["HF_HOME"] = str(elsewhere)
            snapshot = cfg.snapshot_dir(config)
            self.assertEqual(cfg.hf_home(config), elsewhere)
            self.assertTrue(snapshot.is_relative_to(elsewhere / "hub"), snapshot)
            self.assertEqual(snapshot.name, config["model"]["revision"])
        finally:
            if saved is None:
                os.environ.pop("HF_HOME", None)
            else:
                os.environ["HF_HOME"] = saved


if __name__ == "__main__":
    unittest.main()
