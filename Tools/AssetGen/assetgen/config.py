"""config.json: the pinned model, its revision, the mflux version and which models may run at all."""

import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = ROOT / "config.json"


class ModelNotAllowed(Exception):
    pass


def load_config(path: Path = CONFIG_PATH) -> dict:
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def check_model_allowed(config: dict, model_id: str) -> dict:
    """The allowed_models entry for model_id, or ModelNotAllowed saying why it may not run."""
    for blocked in config.get("blocked_models", []):
        if blocked["id"].lower() == model_id.lower():
            raise ModelNotAllowed(f"{model_id} is blocked: {blocked['license']} ({blocked.get('note', '')})")
    for allowed in config.get("allowed_models", []):
        if allowed["id"].lower() == model_id.lower():
            return allowed
    raise ModelNotAllowed(
        f"{model_id} is not on the allowed list in config.json; adding a model is a deliberate change in its own PR"
    )


def hf_home(config: dict) -> Path:
    """HF_HOME if set (the owner's shell sets it), else the configured default. Weights never live in the repo."""
    value = os.environ.get("HF_HOME") or config.get("hf_home_default", "~/AI/huggingface")
    return Path(os.path.expanduser(value))


def snapshot_dir(config: dict) -> Path:
    """Where the pinned revision of the model lives in the Hugging Face cache."""
    model = config["model"]
    folder = "models--" + model["id"].replace("/", "--")
    return hf_home(config) / "hub" / folder / "snapshots" / model["revision"]


# Files the pinned FLUX.2 klein 4B snapshot must hold to run offline.
REQUIRED_SNAPSHOT_FILES = (
    "transformer/config.json",
    "transformer/diffusion_pytorch_model.safetensors",
    "text_encoder/config.json",
    "text_encoder/model.safetensors.index.json",
    "vae/config.json",
    "vae/diffusion_pytorch_model.safetensors",
    "tokenizer/tokenizer.json",
)


def missing_snapshot_files(config: dict) -> list[str]:
    snapshot = snapshot_dir(config)
    return [name for name in REQUIRED_SNAPSHOT_FILES if not (snapshot / name).exists()]
