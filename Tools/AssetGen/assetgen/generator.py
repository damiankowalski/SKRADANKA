"""Seamless sampling with FLUX.2 klein through mflux (Apple Silicon only; imported only by `generate`).

The transformer has no notion of wrapping: tokens at the left and right edges sit at distant positions,
so a plain sample does not tile. Here every denoising step predicts the noise for several copies of the
latent image rolled round the torus (the edges brought into the middle) and blends the predictions, each
copy trusted away from its own cut. The model therefore draws across the texture's edges as if they were
the middle of the picture. The VAE decodes the latent padded with its own wrapped edges, so its
convolutions see across the cut too. This reuses mflux's own steps (encoding, latents, scheduler, VAE)
and changes only the loop, so it is tied to the pinned mflux version (config.json)."""

from __future__ import annotations

import gc
from pathlib import Path

import mlx.core as mx
import numpy as np
from mflux.models.common.config.config import Config
from mflux.models.flux2.variants.txt2img.flux2_klein import Flux2Klein

# Copies of the latent per step, as (row, column) offsets in halves of the tile. Every point lies a
# quarter tile or more from the cuts of at least one copy.
FRAMES = ((0, 0), (0, 1), (1, 0), (1, 1))
# The single copy of the early steps, in turn: the cuts land in a new place each step.
ROLLING = ((1, 1), (0, 1), (1, 0), (0, 0))
# Latent tokens (16 pixels each) of wrapped context the VAE decodes round the tile.
DECODE_PAD = 4


class SeamlessKlein:
    def __init__(self, model_path: Path, quantize: int | None):
        self.model = Flux2Klein(quantize=quantize, model_path=str(model_path))
        # Freed buffers go back to the system instead of piling up between steps (as mflux's --low-ram).
        mx.set_cache_limit(1024 ** 3)

    def encode_prompts(self, prompts: list[str]) -> list[tuple[mx.array, mx.array]]:
        """Encode every prompt, then drop the text encoder (about 4 GB): denoising does not need it."""
        model = self.model
        encoded = []
        for prompt in prompts:
            embeds, ids, _, _ = model._encode_prompt_pair(prompt=prompt, negative_prompt=None, guidance=1.0)
            mx.eval(embeds, ids)
            encoded.append((embeds, ids))
        model.text_encoder = None
        gc.collect()
        mx.clear_cache()
        return encoded

    def generate(self, encoded_prompt: tuple[mx.array, mx.array], seed: int, steps: int, size: int,
                 seamless: bool = True, blend_steps: int = 1) -> np.ndarray:
        """An RGB float32 image in [0, 1], size x size, from an encode_prompts() result."""
        model = self.model
        config = Config(
            model_config=model.model_config,
            num_inference_steps=steps,
            height=size,
            width=size,
            guidance=1.0,
            scheduler="flow_match_euler_discrete",
        )
        prompt_embeds, text_ids = encoded_prompt
        latents, latent_ids, rows, columns = model._prepare_generation_latents(seed=seed, config=config)
        channels = latents.shape[-1]
        predict = model._predict(model.transformer)
        weights = _frame_weights(rows, columns) if seamless else None

        def roll(packed: mx.array, shift: tuple[int, int]) -> mx.array:
            grid = packed.reshape(1, rows, columns, channels)
            return mx.roll(grid, shift=shift, axis=(1, 2)).reshape(1, rows * columns, channels)

        def frame_noise(shift: tuple[int, int], timestep: mx.array) -> mx.array:
            noise = predict(latents=roll(latents, shift), latent_ids=latent_ids, prompt_embeds=prompt_embeds,
                            text_ids=text_ids, negative_prompt_embeds=None, negative_text_ids=None, guidance=1.0,
                            timestep=timestep)
            return roll(noise, (-shift[0], -shift[1]))

        for index, t in enumerate(config.time_steps):
            timestep = config.scheduler.timesteps[t]
            if weights is None:
                noise = frame_noise((0, 0), timestep)
            elif index < steps - blend_steps:
                # Early steps: one copy, its cut somewhere else every step (noise rolling).
                frame_row, frame_column = ROLLING[index % len(ROLLING)]
                noise = frame_noise((frame_row * rows // 2, frame_column * columns // 2), timestep)
            else:
                # Last steps: every copy, blended away from its own cut, so no cut survives into the image.
                noise = mx.zeros_like(latents)
                for (frame_row, frame_column), weight in zip(FRAMES, weights):
                    noise = noise + frame_noise((frame_row * rows // 2, frame_column * columns // 2), timestep) * weight
            latents = config.scheduler.step(noise=noise, timestep=t, latents=latents, sigmas=config.scheduler.sigmas)
            mx.eval(latents)
        del predict

        grid = latents.reshape(1, rows, columns, channels).transpose(0, 3, 1, 2)
        pad = DECODE_PAD if seamless else 0
        if pad:
            grid = mx.concatenate([grid[:, :, -pad:, :], grid, grid[:, :, :pad, :]], axis=2)
            grid = mx.concatenate([grid[:, :, :, -pad:], grid, grid[:, :, :, :pad]], axis=3)
        decoded = model.vae.decode_packed_latents(grid, tiling_config=model.tiling_config)
        image = mx.clip(decoded[0] / 2.0 + 0.5, 0.0, 1.0).transpose(1, 2, 0).astype(mx.float32)
        mx.eval(image)
        image = np.array(image)
        del decoded, grid, latents
        gc.collect()
        mx.clear_cache()
        if pad:
            border = pad * image.shape[0] // (rows + 2 * pad)
            image = image[border:-border, border:-border]
        return image


def _frame_weights(rows: int, columns: int) -> list[mx.array]:
    """Per frame, a (1, rows * columns, 1) weight: how far each token is from that frame's cuts."""

    def ramp(count: int, offset: int) -> np.ndarray:
        # The frame rolled by `offset` is cut between original indices count - offset - 1 and count - offset.
        cut = (count - offset) % count
        centres = np.arange(count, dtype=np.float32) + 0.5
        distance = np.abs(centres - cut)
        distance = np.minimum(distance, count - distance)
        return np.clip(distance / (count / 4.0), 0.0, 1.0)

    raw = []
    for frame_row, frame_column in FRAMES:
        row_ramp = ramp(rows, frame_row * rows // 2)
        column_ramp = ramp(columns, frame_column * columns // 2)
        raw.append(row_ramp[:, None] * column_ramp[None, :])
    total = np.sum(raw, axis=0)
    return [mx.array((weight / total).reshape(1, rows * columns, 1)) for weight in raw]


def peak_memory_gb() -> float:
    return mx.get_peak_memory() / 1024 ** 3


def reset_peak_memory() -> None:
    mx.reset_peak_memory()
