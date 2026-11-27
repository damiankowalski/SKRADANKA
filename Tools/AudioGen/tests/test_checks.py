"""AudioGen tests: the checks, the listening room's feedback, promotion and the model allow-list. No model, no
PyTorch, no Apple Silicon (run by CTest as Tools.AudioGen, Tools/AudioGen/README.md)."""

import json
import math
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))

import generate  # noqa: E402
import promote  # noqa: E402
import review  # noqa: E402
from audiogen import checks  # noqa: E402
from audiogen import model  # noqa: E402

RATE = 44100


def sine(freq=997.0, seconds=1.0, amplitude=1.0, rate=RATE, phase=0.0):
    t = np.arange(int(seconds * rate)) / rate
    return (amplitude * np.sin(2 * np.pi * freq * t + phase)).astype(np.float32)[None, :]


def noise(seconds=1.0, amplitude=0.3, seed=1, channels=1):
    rng = np.random.default_rng(seed)
    return (amplitude * rng.standard_normal((channels, int(seconds * RATE)))).astype(np.float32)


def step(seconds=0.3, seed=2):
    """A footstep-like burst: noise with a fast decay, after a short silence."""
    burst = noise(seconds, 0.6, seed) * np.exp(-np.arange(int(seconds * RATE)) / (RATE * 0.03))
    return np.concatenate([np.zeros((1, int(0.2 * RATE)), np.float32), burst.astype(np.float32)], axis=1)


class LoudnessTests(unittest.TestCase):
    def test_a_full_scale_997_hz_sine_is_minus_3_lufs(self):
        # The reference point of ITU-R BS.1770 (and EBU Tech 3341): one channel, 0 dBFS sine at 997 Hz.
        self.assertAlmostEqual(checks.loudness_lufs(sine(seconds=5), RATE), -3.01, delta=0.05)

    def test_loudness_follows_the_gain(self):
        quieter = checks.loudness_lufs(sine(seconds=5, amplitude=0.1), RATE)
        self.assertAlmostEqual(quieter, -23.01, delta=0.05)

    def test_silence_has_no_loudness(self):
        self.assertEqual(checks.loudness_lufs(np.zeros((1, RATE * 2), np.float32), RATE), -math.inf)

    def test_a_clip_shorter_than_one_block_still_has_a_loudness(self):
        self.assertAlmostEqual(checks.loudness_lufs(sine(seconds=0.2), RATE), -3.01, delta=0.3)

    def test_true_peak_sees_between_the_samples(self):
        # A sine at a quarter of the rate, 45 degrees off: every sample is at 0.707, the wave itself reaches 1.0.
        audio = sine(freq=RATE / 4, seconds=0.4, phase=math.pi / 4)  # a whole number of periods
        self.assertAlmostEqual(checks.db(float(np.max(np.abs(audio)))), -3.01, delta=0.05)
        self.assertAlmostEqual(checks.true_peak(audio), 0.0, delta=0.1)


class CheckTests(unittest.TestCase):
    def test_a_good_effect_passes(self):
        audio = checks.prepare(step(), RATE, "effect")
        measures, failures = checks.check(audio, RATE, "effect", (0.1, 1.0))
        self.assertEqual(failures, [], measures)
        self.assertAlmostEqual(measures["true_peak_dbtp"], -3.0, delta=0.05)
        self.assertLess(measures["lead_silence"], 0.05)

    def test_a_silent_clip_is_rejected(self):
        _, failures = checks.check(np.zeros((1, RATE), np.float32), RATE, "effect")
        self.assertIn("silent or empty", failures)

    def test_a_clipped_clip_is_rejected(self):
        _, failures = checks.check(np.clip(sine(amplitude=1.5), -1, 1), RATE, "effect")
        self.assertTrue(any(f.startswith("clipped") for f in failures), failures)

    def test_a_stereo_effect_is_rejected(self):
        _, failures = checks.check(np.repeat(sine(amplitude=0.3), 2, axis=0), RATE, "effect")
        self.assertIn("a 3D effect must be mono", failures)

    def test_silence_before_the_sound_is_rejected(self):
        _, failures = checks.check(step() * 0.5, RATE, "effect")
        self.assertTrue(any("silence before" in f for f in failures), failures)

    def test_the_length_is_checked(self):
        _, failures = checks.check(sine(seconds=2, amplitude=0.3), RATE, "effect", (0.1, 1.0))
        self.assertTrue(any("long, not" in f for f in failures), failures)

    def test_an_ambience_is_brought_to_its_loudness(self):
        audio = checks.prepare(noise(seconds=20, channels=2, amplitude=0.05), RATE, "ambience", loop=True)
        measures, failures = checks.check(audio, RATE, "ambience", (15, 20), loop=True)
        self.assertEqual(failures, [], measures)
        self.assertAlmostEqual(measures["lufs"], -30.0, delta=0.3)


class LoopTests(unittest.TestCase):
    def test_a_crossfaded_loop_has_no_seam(self):
        looped = checks.make_loop(noise(seconds=10, channels=2), RATE)
        self.assertLess(checks.loop_seam(looped), checks.LOOP_SEAM_DB)
        self.assertEqual(looped.shape[1], 8 * RATE)

    def test_a_cut_through_a_low_tone_jumps(self):
        # 10.25 periods of a slow wave: the end is at the crest, the start at zero.
        cut = sine(freq=41.0, seconds=0.25, amplitude=0.5)
        self.assertGreater(checks.loop_seam(cut), checks.LOOP_SEAM_DB)
        _, failures = checks.check(cut, RATE, "ambience", loop=True)
        self.assertTrue(any("seam" in f for f in failures), failures)


class FileTests(unittest.TestCase):
    def test_a_written_clip_reads_back(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "a.wav"
            checks.write_wav(path, sine(amplitude=0.5), RATE)
            audio, rate = checks.read_wav(path)
            self.assertEqual(rate, RATE)
            self.assertEqual(audio.shape, (1, RATE))
            self.assertAlmostEqual(float(np.max(audio)), 0.5, delta=0.001)

    def test_a_broken_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            good = Path(folder) / "good.wav"
            checks.write_wav(good, sine(amplitude=0.5), RATE)
            truncated = Path(folder) / "truncated.wav"
            truncated.write_bytes(good.read_bytes()[:30])
            garbage = Path(folder) / "garbage.wav"
            garbage.write_bytes(b"this is not a wave file at all" * 10)
            for path in (truncated, garbage, Path(folder) / "missing.wav"):
                measures, failures = checks.check_file(path, "effect")
                self.assertEqual(measures, {})
                self.assertTrue(failures and failures[0].startswith("unreadable"), (path.name, failures))


class FirstEventTests(unittest.TestCase):
    def test_the_first_of_two_steps_is_kept_with_its_decay(self):
        one = step(seed=3)[:, int(0.2 * RATE):]
        two = np.concatenate([one, np.zeros((1, int(0.4 * RATE)), np.float32), one], axis=1)
        first = generate.first_event(two, RATE)
        self.assertGreater(first.shape[1], int(0.15 * RATE))
        self.assertLess(first.shape[1], int(0.4 * RATE))


def make_series(folder, clips):
    """A fake series: finished clips (sines) and series.json with what generate.py writes."""
    series = Path(folder) / "out" / "test"
    series.mkdir(parents=True)
    records = []
    for name, game_name, passed in clips:
        checks.write_wav(series / f"{name}.wav", sine(amplitude=0.5, seconds=0.3), RATE)
        records.append({"sound": name.rsplit("_", 1)[0], "file": f"{name}.wav", "raw": f"{name}_raw.wav",
                        "game_name": game_name, "prompt": f"prompt of {name}", "seed": int(name.rsplit("_", 1)[1]),
                        "model": "m", "license": "l",
                        "checks": {"measures": {}, "failures": [] if passed else ["too quiet"], "passed": passed}})
    (series / "series.json").write_text(json.dumps({"series": "test", "clips": records}), encoding="utf-8")
    return series


class FeedbackTests(unittest.TestCase):
    def test_verdicts_are_saved_and_validated(self):
        with tempfile.TemporaryDirectory() as folder:
            series = make_series(folder, [("door_1000", "Door", True)])
            with self.assertRaisesRegex(ValueError, "no clip"):
                review.set_verdict(series, "door_9999", "accept", "")
            with self.assertRaisesRegex(ValueError, "decision"):
                review.set_verdict(series, "door_1000", "maybe", "")
            with self.assertRaisesRegex(ValueError, "needs a note"):
                review.set_verdict(series, "door_1000", "change", "  ")
            review.set_verdict(series, "door_1000", "change", "slower, less squeak")
            saved = json.loads((series / "feedback.json").read_text(encoding="utf-8"))
            self.assertEqual(saved["clips"]["door_1000"]["decision"], "change")
            self.assertEqual(saved["clips"]["door_1000"]["note"], "slower, less squeak")
            review.set_verdict(series, "door_1000", None, "")
            self.assertEqual(review.load_feedback(series)["clips"], {})

    def test_the_server_saves_verdicts_and_serves_only_the_series(self):
        with tempfile.TemporaryDirectory() as folder:
            series = make_series(folder, [("door_1000", "Door", True)])
            (Path(folder) / "out" / "secret.wav").write_bytes(b"RIFF")
            server = review.ThreadingHTTPServer(("127.0.0.1", 0), review.make_handler(series))
            threading.Thread(target=server.serve_forever, daemon=True).start()
            base = f"http://127.0.0.1:{server.server_address[1]}"
            try:
                page = urllib.request.urlopen(base + "/").read().decode("utf-8")
                self.assertIn("feedback", page)
                self.assertEqual(urllib.request.urlopen(base + "/audio/door_1000.wav").read()[:4], b"RIFF")
                for path in ("/audio/..%2Fsecret.wav", "/audio/series.json", "/audio/missing.wav"):
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(base + path)
                    self.assertEqual(error.exception.code, 404, path)
                request = urllib.request.Request(base + "/feedback", method="POST",
                                                 data=json.dumps({"clip": "door_1000", "decision": "accept",
                                                                  "note": "dobre"}).encode("utf-8"))
                self.assertEqual(json.loads(urllib.request.urlopen(request).read()), {"ok": True})
                bad = urllib.request.Request(base + "/feedback", method="POST",
                                             data=json.dumps({"clip": "door_1000", "decision": "change"}).encode())
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(bad)
                self.assertEqual(error.exception.code, 400)
            finally:
                server.shutdown()
                server.server_close()
            self.assertEqual(review.load_feedback(series)["clips"]["door_1000"]["note"], "dobre")


class PromoteTests(unittest.TestCase):
    def test_verdicts_are_carried_out(self):
        with tempfile.TemporaryDirectory() as folder:
            series = make_series(folder, [("door_1000", "Door", True), ("door_1001", "Door", True),
                                          ("step_1000", "Footstep", True), ("step_1001", "Footstep", False),
                                          ("wind_1000", "Wind", True), ("wind_1001", "Wind", True)])
            for clip, decision, note in [("door_1000", "accept", "good"), ("door_1001", "accept", ""),
                                         ("step_1000", "reject", ""), ("step_1001", "accept", ""),
                                         ("wind_1000", "change", "less whistling")]:
                review.set_verdict(series, clip, decision, note)
            assets = Path(folder) / "Assets"

            dry = promote.promote(series, assets, dry_run=True)
            self.assertEqual(len(dry["promoted"]), 2)
            self.assertFalse((assets / promote.GENERATED).exists())

            report = promote.promote(series, assets)
            self.assertEqual(report["promoted"], [("door_1000", "Audio/Generated/Door_01.wav"),
                                                  ("door_1001", "Audio/Generated/Door_02.wav")])
            self.assertEqual(report["rejected"], ["step_1000"])
            self.assertEqual([clip for clip, _ in report["refused"]], ["step_1001"])
            self.assertEqual(report["unrated"], ["wind_1001"])
            self.assertEqual([c["note"] for c in report["changes"]], ["less whistling"])

            generated = assets / promote.GENERATED
            self.assertEqual(sorted(p.name for p in generated.iterdir()),
                             ["Door_01.json", "Door_01.wav", "Door_02.json", "Door_02.wav"])
            provenance = json.loads((generated / "Door_01.json").read_text(encoding="utf-8"))
            self.assertEqual(provenance["owner"]["decision"], "accepted")
            self.assertEqual(provenance["owner"]["note"], "good")
            self.assertEqual(provenance["source"], {"series": "test", "file": "door_1000.wav"})
            self.assertEqual(provenance["prompt"], "prompt of door_1000")
            changes = json.loads((series / "changes.json").read_text(encoding="utf-8"))
            self.assertEqual(changes["changes"][0]["clip"], "wind_1000")

            again = promote.promote(series, assets)
            self.assertEqual(again["promoted"], [])
            self.assertEqual(len(again["kept"]), 2)
            self.assertEqual(len(list(generated.glob("*.wav"))), 2)

    def test_nothing_happens_before_the_owner_listened(self):
        with tempfile.TemporaryDirectory() as folder:
            series = make_series(folder, [("door_1000", "Door", True)])
            with self.assertRaisesRegex(FileNotFoundError, "has not listened"):
                promote.promote(series, Path(folder) / "Assets")


class ConfigTests(unittest.TestCase):
    def test_the_pinned_model_is_allowed_and_pinned_to_a_revision(self):
        settings = model.config()
        entry = model.check_model_allowed(settings, settings["model"]["id"])
        self.assertIn("Stability AI Community License", entry["license"])
        self.assertRegex(settings["model"]["revision"], r"^[0-9a-f]{40}$")

    def test_other_models_are_refused(self):
        with self.assertRaisesRegex(model.ModelNotAllowed, "not on the allowed list"):
            model.check_model_allowed(model.config(), "stabilityai/stable-audio-open-1.0")

    def test_every_template_is_valid(self):
        for path in sorted((HERE / "prompts").glob("*.json")):
            template = json.loads(path.read_text(encoding="utf-8"))
            self.assertIn(template["category"], checks.CATEGORIES, path.name)
            self.assertIn("{variation}", template["prompt"], path.name)
            self.assertTrue(template["variations"], path.name)
            self.assertTrue(template["game_name"].replace("_", "").isalnum(), path.name)
            low, high = template["length"]
            self.assertLess(low, high, path.name)


if __name__ == "__main__":
    unittest.main()
