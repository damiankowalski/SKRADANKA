"""Stable Audio 3 Small SFX at the revision pinned in config.json, loaded from the local Hugging Face cache only.

The library's own `from_pretrained` downloads the latest revision; this loader reads the pinned snapshot instead and
points the text encoder at the same snapshot, with the Hub offline, so a run never fetches anything.
"""

import json
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent


def config():
    with open(HERE / "config.json", encoding="utf-8") as f:
        return json.load(f)


def hf_home():
    return Path(os.environ.get("HF_HOME") or os.path.expanduser(config()["hf_home_default"]))


def snapshot_dir():
    model = config()["model"]
    return hf_home() / "hub" / ("models--" + model["id"].replace("/", "--")) / "snapshots" / model["revision"]


class ModelNotAllowed(Exception):
    pass


def check_model_allowed(settings, model_id):
    """The allow-list entry for a model; raises ModelNotAllowed for anything else (a new model is a deliberate PR)."""
    for entry in settings.get("allowed_models", []):
        if entry["id"] == model_id:
            if not settings["model"].get("revision"):
                raise ModelNotAllowed(f"{model_id} has no pinned revision in config.json")
            return entry
    raise ModelNotAllowed(f"{model_id} is not on the allowed list in config.json")


class Generator:
    """The model, loaded once; `generate` makes one clip (float32 numpy array, channels x samples)."""

    def __init__(self, device="cpu"):
        check_model_allowed(config(), config()["model"]["id"])
        os.environ["HF_HOME"] = str(hf_home())
        os.environ["HF_HUB_OFFLINE"] = "1"
        os.environ["TRANSFORMERS_OFFLINE"] = "1"
        import torch  # noqa: F401  (imported here: the checks and tests never need it)
        from stable_audio_3.loading_utils import load_diffusion_cond
        from stable_audio_3.model import StableAudioModel

        snapshot = snapshot_dir()
        if not (snapshot / "model.safetensors").exists():
            raise FileNotFoundError(f"the pinned model is not in {snapshot}: run setup.sh download")
        with open(snapshot / "model_config.json", encoding="utf-8") as f:
            model_config = json.load(f)
        for conditioner in model_config["model"]["conditioning"]["configs"]:
            settings = conditioner.get("config", {})
            if conditioner.get("type") == "t5gemma":
                settings["model_path"] = str(snapshot / settings.pop("subfolder", "t5gemma-b-b-ul2"))
                settings.pop("repo_id", None)
        self.sample_rate = model_config["sample_rate"]
        model = load_diffusion_cond(model_config, str(snapshot / "model.safetensors"), device=device, model_half=False)
        model.use_lora = False
        model.lora_names = []
        self.model = StableAudioModel(model, model_config, device, False)

    def generate(self, prompt, seconds, seed, steps=8, cfg_scale=1.0):
        audio = self.model.generate(prompt=prompt, duration=float(seconds), seed=int(seed), steps=int(steps),
                                    cfg_scale=float(cfg_scale))
        array = audio.detach().to("cpu").float().numpy()
        if array.ndim == 3:
            array = array[0]
        return array
