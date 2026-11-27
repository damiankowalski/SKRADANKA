"""Objective checks for generated sounds, and the processing that makes a clip fit the game (numpy only).

The agent cannot hear: these reject broken files before the owner listens. Loudness follows ITU-R BS.1770 (K-weighting,
400 ms blocks, absolute and relative gates), true peak is the sample peak of a 4x oversampled signal. numpy and scipy only.
"""

import io
import math
import wave

import numpy as np
from scipy.signal import lfilter

# What each category must meet (docs: Tools/AudioGen/README.md, "Checks"). Loudness in LUFS, peaks in dBFS / dBTP.
# Effects are short and sharp (a step, a door): integrated loudness says little about them, so they are brought to a
# true peak and only checked for a sane loudness range. Ambience is steady: brought to its loudness.
CATEGORIES = {
    "effect": {"normalise": "peak", "peak_target": -3.0, "lufs_range": (-36.0, -12.0), "true_peak": -1.0,
               "max_lead_silence": 0.05, "mono": True},
    "ambience": {"normalise": "loudness", "lufs_target": -30.0, "lufs_range": (-33.0, -27.0), "true_peak": -3.0,
                 "max_lead_silence": 0.5, "mono": False},
}
SILENCE_DB = -55.0      # below this a stretch counts as silence
CLIP_LEVEL = 0.999      # a sample this close to full scale counts as clipped
LOOP_SEAM_DB = 6.0      # how much louder the jump across a loop's seam may be than the clip's own sample-to-sample motion


# ------------------------------------------------------------------------------------------------ files

def read_wav(data):
    """(float32 array channels x samples, sample rate) from WAV bytes or a path; 16-bit PCM."""
    handle = wave.open(io.BytesIO(data) if isinstance(data, (bytes, bytearray)) else str(data), "rb")
    with handle:
        channels, width, rate, frames = handle.getnchannels(), handle.getsampwidth(), handle.getframerate(), handle.getnframes()
        raw = handle.readframes(frames)
    if width != 2:
        raise ValueError(f"{width * 8}-bit WAV: only 16-bit PCM is read")
    samples = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    return samples.reshape(-1, channels).T.copy(), rate


def write_wav(path, audio, rate):
    """16-bit PCM WAV from float audio (channels x samples, or one channel), clamped to full scale."""
    audio = np.atleast_2d(np.asarray(audio, dtype=np.float32))
    pcm = np.clip(np.round(audio.T * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(audio.shape[0])
        handle.setsampwidth(2)
        handle.setframerate(int(rate))
        handle.writeframes(pcm.tobytes())


# ------------------------------------------------------------------------------------------------ measures

def db(value):
    return -math.inf if value <= 0 else 20.0 * math.log10(value)


def _k_weighting(rate):
    """The two K-weighting biquads of BS.1770 for any sample rate (the 48 kHz design moved by the bilinear transform)."""
    # Shelving stage
    f0, gain, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = math.tan(math.pi * f0 / rate)
    vh = 10 ** (gain / 20)
    vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    shelf = ([(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0],
             [1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0])
    # High-pass stage
    f0, q = 38.13547087602444, 0.5003270373238773
    k = math.tan(math.pi * f0 / rate)
    a0 = 1 + k / q + k * k
    highpass = ([1.0, -2.0, 1.0], [1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0])
    return shelf, highpass


def _filter(audio, b, a):
    return np.stack([lfilter(b, a, channel.astype(np.float64)) for channel in audio])


def loudness_lufs(audio, rate):
    """Integrated loudness (LUFS), ITU-R BS.1770-4: K-weighted, 400 ms blocks with 75% overlap, -70 LUFS absolute
    gate and the relative gate 10 LU below. -inf for silence or clips shorter than one block."""
    audio = np.atleast_2d(audio)
    shelf, highpass = _k_weighting(rate)
    weighted = _filter(_filter(audio, *shelf), *highpass)
    block, hop = int(0.4 * rate), int(0.1 * rate)
    if weighted.shape[1] < block:
        # Shorter than one gating block (a footstep): the K-weighted power of the whole clip, ungated.
        power = float(np.sum(np.mean(weighted * weighted, axis=1))) if weighted.shape[1] else 0.0
        return -0.691 + 10 * math.log10(power) if power > 0 else -math.inf
    powers = []
    for start in range(0, weighted.shape[1] - block + 1, hop):
        segment = weighted[:, start:start + block]
        powers.append(float(np.sum(np.mean(segment * segment, axis=1))))
    powers = np.array(powers)
    loud = lambda p: -0.691 + 10 * np.log10(p) if p > 0 else -math.inf
    gated = powers[np.array([loud(p) > -70 for p in powers])]
    if gated.size == 0:
        return -math.inf
    relative = loud(float(np.mean(gated))) - 10
    final = gated[np.array([loud(p) > relative for p in gated])]
    return loud(float(np.mean(final))) if final.size else -math.inf


def true_peak(audio):
    """Peak of the signal oversampled 4x (FFT interpolation), dBTP."""
    audio = np.atleast_2d(audio).astype(np.float64)
    n = audio.shape[1]
    if n == 0:
        return -math.inf
    spectrum = np.fft.rfft(audio, axis=1)
    padded = np.zeros((audio.shape[0], (4 * n) // 2 + 1), dtype=complex)
    padded[:, :spectrum.shape[1]] = spectrum
    upsampled = np.fft.irfft(padded, n=4 * n, axis=1) * 4
    return db(float(max(np.max(np.abs(upsampled)), np.max(np.abs(audio)))))


def silent_edges(audio, rate):
    """Seconds of silence (below SILENCE_DB) at the start and at the end."""
    level = np.max(np.abs(np.atleast_2d(audio)), axis=0)
    loud = np.nonzero(level > 10 ** (SILENCE_DB / 20))[0]
    if loud.size == 0:
        return audio.shape[-1] / rate, audio.shape[-1] / rate
    return loud[0] / rate, (level.size - 1 - loud[-1]) / rate


def loop_seam(audio):
    """How abrupt the jump from the last sample back to the first is, compared with the clip's own sample-to-sample
    motion (dB above the 99th percentile of its differences; below LOOP_SEAM_DB a loop plays without a click)."""
    mono = np.mean(np.atleast_2d(audio), axis=0)
    steps = np.abs(np.diff(mono))
    typical = float(np.percentile(steps, 99)) if steps.size else 0.0
    jump = abs(float(mono[0] - mono[-1]))
    if typical <= 0:
        return 0.0 if jump == 0 else math.inf
    return db(jump / typical) if jump > 0 else -math.inf


# ------------------------------------------------------------------------------------------------ processing

def trim_silence(audio, rate, keep=0.01):
    """Cut the silent start and end, keeping `keep` seconds before the first and after the last sound."""
    lead, tail = silent_edges(audio, rate)
    n = audio.shape[-1]
    start = max(0, int((lead - keep) * rate))
    end = min(n, n - int(max(tail - keep, 0) * rate))
    return audio[..., start:end] if end > start else audio[..., :0]


def fade(audio, rate, seconds_in=0.003, seconds_out=0.02):
    audio = np.array(audio, dtype=np.float32, copy=True)
    n = audio.shape[-1]
    fi, fo = min(int(seconds_in * rate), n), min(int(seconds_out * rate), n)
    if fi:
        audio[..., :fi] *= np.linspace(0, 1, fi, dtype=np.float32)
    if fo:
        audio[..., n - fo:] *= np.linspace(1, 0, fo, dtype=np.float32)
    return audio


def make_loop(audio, rate, crossfade=2.0):
    """A seamless loop: the last `crossfade` seconds fade into the first ones, and the result is that much shorter."""
    audio = np.atleast_2d(np.asarray(audio, dtype=np.float32))
    x = min(int(crossfade * rate), audio.shape[1] // 3)
    if x <= 0:
        return audio
    head, body, tail = audio[:, :x], audio[:, x:-x], audio[:, -x:]
    t = np.linspace(0, 1, x, dtype=np.float32)
    blend = tail * np.sqrt(1 - t) + head * np.sqrt(t)  # equal power
    return np.concatenate([blend, body], axis=1)


def to_mono(audio):
    return np.mean(np.atleast_2d(audio), axis=0, keepdims=True)


def normalise(audio, rate, target_lufs, ceiling_dbtp):
    """Gain to the target loudness, then down if the true peak would pass the ceiling."""
    current = loudness_lufs(audio, rate)
    if not math.isfinite(current):
        return audio
    gain = 10 ** ((target_lufs - current) / 20)
    out = audio * gain
    peak = true_peak(out)
    if peak > ceiling_dbtp:
        out = out * 10 ** ((ceiling_dbtp - peak) / 20)
    return out.astype(np.float32)


def prepare(audio, rate, category, loop=False, mono=None):
    """The clip as the game plays it: trimmed (effects), looped (ambience loops), mono (3D effects), normalised."""
    rules = CATEGORIES[category]
    audio = np.atleast_2d(np.asarray(audio, dtype=np.float32))
    if (rules["mono"] if mono is None else mono):
        audio = to_mono(audio)
    if loop:
        audio = make_loop(audio, rate)
    else:
        audio = fade(trim_silence(audio, rate), rate)
    if rules["normalise"] == "peak":
        peak = true_peak(audio)
        return (audio * 10 ** ((rules["peak_target"] - peak) / 20)).astype(np.float32) if math.isfinite(peak) else audio
    return normalise(audio, rate, rules["lufs_target"], rules["true_peak"] - 0.2)


# ------------------------------------------------------------------------------------------------ the verdict

def check(audio, rate, category, seconds=None, loop=False, lufs_range=None):
    """Every check for one finished clip: a dict of measures, a list of failures (empty: it passes). `lufs_range`
    overrides the category's (a template's own, for impulsive loops such as a crackling fire: its pops reach the
    true peak ceiling before its body reaches the category's loudness)."""
    rules = CATEGORIES[category]
    audio = np.atleast_2d(audio)
    duration = audio.shape[1] / rate
    lead, tail = silent_edges(audio, rate)
    measures = {
        "seconds": round(float(duration), 3),
        "channels": int(audio.shape[0]),
        "lufs": round(float(loudness_lufs(audio, rate)), 2),
        "true_peak_dbtp": round(float(true_peak(audio)), 2),
        "clipped_samples": int(np.sum(np.abs(audio) >= CLIP_LEVEL)),
        "lead_silence": round(float(lead), 3),
        "tail_silence": round(float(tail), 3),
    }
    failures = []
    if duration <= 0 or float(np.max(np.abs(audio))) < 10 ** (SILENCE_DB / 20):
        failures.append("silent or empty")
    if measures["clipped_samples"]:
        failures.append(f"clipped ({measures['clipped_samples']} samples at full scale)")
    if measures["true_peak_dbtp"] > rules["true_peak"]:
        failures.append(f"true peak {measures['true_peak_dbtp']} dBTP over {rules['true_peak']}")
    low, high = lufs_range or rules["lufs_range"]
    if math.isfinite(measures["lufs"]) and not low <= measures["lufs"] <= high:
        failures.append(f"loudness {measures['lufs']} LUFS, not {low} to {high}")
    if rules["mono"] and audio.shape[0] != 1:
        failures.append("a 3D effect must be mono")
    if not loop and lead > rules["max_lead_silence"]:
        failures.append(f"{lead:.2f} s of silence before the sound")
    if seconds is not None:
        low, high = seconds
        if not low <= duration <= high:
            failures.append(f"{duration:.2f} s long, not {low}-{high} s")
    if loop:
        measures["loop_seam_db"] = round(float(loop_seam(audio)), 2)
        if measures["loop_seam_db"] > LOOP_SEAM_DB:
            failures.append(f"the loop's seam jumps ({measures['loop_seam_db']} dB over the clip's own motion)")
    return measures, failures


def check_file(path, category, seconds=None, loop=False, lufs_range=None):
    """check() for a WAV file on disk; a file that cannot be read as 16-bit PCM fails instead of raising."""
    try:
        audio, rate = read_wav(path)
    except (OSError, EOFError, ValueError, wave.Error) as error:
        return {}, [f"unreadable: {error}"]
    return check(audio, rate, category, seconds, loop, lufs_range)
