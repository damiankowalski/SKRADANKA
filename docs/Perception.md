# NPC perception

Implementation of the "ShadowProto" silhouette-contrast model from `GAME ENGINE SKRADANKA.md`.
Code: `Penumbra/src/Penumbra/AI/` — `PerceptionModel` (pure formulas), `LuminanceProvider`
(brightness sources), `PerceptionSystem` (per-frame evaluation). Tests: `PenumbraTests/src/PerceptionTests.cpp`
(formulas plus the seven scenarios of section 8 as end-to-end tests) and `NavigationTests.cpp` (sound around
corners, the AI lab guard end to end).

## Pipeline (per guard, 10 Hz; suspicion integrates every frame)

1. **Silhouette sampling** — each `PerceptionTarget` has 5–9 control points (default 7). For every point
   inside the view cone and distance: a physics ray from the eye checks occlusion (`f` = visible fraction).
2. **Luminances** — `L_p`: the target is a rounded body, not a wall: every light contributes
   `E·(1 + cos θ)/2`, where θ is between the light and the direction to the guard (a figure lit from the
   side or behind is still partly visible), plus light bounced up from the floor it stands on
   (`π·L_floor·½`), × clothing reflectance. The light on the target's points is gathered once and shared
   by every guard, refreshed at 20 Hz.
   `L_b`: the sight line continues *through* the target to the first surface (or the sky); its albedo ×
   incoming light + emission. Both come from `AnalyticLuminanceProvider` (spec approach 1): all enabled
   lights with the renderer's falloff (`Scene/LightUtils.h` ≡ `shaders/Lighting.hlsli`) and physics shadow rays.
3. **Contrast** — Weber `C = |L_b − L_p| / max(L_b, L_p, ε)` with `ε = LuminanceFloor` (eye noise floor:
   two black values give ≈ 0 contrast). Averaged over visible points, then × `exp(−optical depth)` of fog
   (`Environment.FogDensity`) and smoke volumes along the line.
4. **Threshold** — see the deviation below.
5. **Factors** — size `S` (angular size vs. `ReferenceAngularSize`), peripheral `P(θ)` (1 → `PeripheralFactor`
   at the cone edge), motion `M = 1 + m·speed·(1 + peripherality)`.
6. **Stimulus** `B = (C_eff / C_t)·f·S·P·M`; suspicion `D` rises by `r_rise·(B−1)·dt` when `B ≥ 1`, else decays.
   From `SuspiciousThreshold` (0.3) a sighting is a stimulus of strength `10·D` (see Reactions).
7. **Hearing** — noise events on a 0–10 loudness scale (see Hearing) become stimuli of the strength that
   reaches the guard.

## Hearing: the loudness scale

Every noise has a **loudness** `L` from 0 to 10 — one table for designers (`Scripts/AI/Noises.lua` in the
demo; `Surface.ImpactLoudness` for physical objects; `AudioSource.NoiseLoudness`; the `EmitNoise` stim action).

| L | carries | example |
|---|---|---|
| 1 | ~2 m | sneaking on stone |
| 3 | ~4 m | walking, a quiet crack |
| 5 | ~8 m | running on stone |
| 7 | ~16 m | running on a metal grate |
| 9 | ~32 m | a metal bucket dropped on cobbles |
| 10 | ~45 m | an explosion |

A noise carries `1.4·2^(L/2)` m and loses 2 levels per doubling of distance (`HeardLevel`; full loudness
within 1.4 m). What reaches the guard:

* **open line** — `HeardLevel(L, d)`;
* **through a wall** — 4 levels less (`WallLoss`);
* **around it** — when the navmesh connects the noise and the guard, the walking distance with 1 level of
  corner loss (`CornerLoss`). Sound goes through doorways and along corridors like Thief's room graph,
  without authoring rooms. The louder of the two wins.

`HearingSensitivity` scales the distances; alert memory adds up to 50%. `Environment.AmbientNoise` (rain,
machinery, a storm) is subtracted from everything heard: it masks quiet sounds.

Footsteps add the floor's `Surface.NoiseModifier` (carpet −2, grass −1, moss −3, wood +0.5, metal grate
+2.5); dynamic objects with `Surface.ImpactLoudness` make `ImpactLoudness + 2·log2(speed / 4 m/s)` plus the
modifier of what they hit (silent below 1 m/s, 0.25 s cooldown).

## Reactions: strength sets the duration

A stimulus has a **strength** `s` (0–10): what was heard, `10·D` for what was seen, or the value a script
passes to `ai.notify`. It keeps the guard busy

```
T(s) = ReactionDuration · (s/10)²      (default 90 s: s = 3 → 8 s, 5 → 22 s, 7 → 44 s, 9 → 73 s)
```

and the strength *remaining* (`10·√(t_left / ReactionDuration)`) sets the state, so a reaction steps down
as it fades: **Alarmed** (≥ 9) → **Searching** (≥ 7) → **Investigating** (≥ 4) → **Suspicious** (> 0) →
**Unaware**. A new stimulus is taken only when it is at least as strong as what is left (a guard hunting
after a crash ignores a creaking board); while the intruder is in sight the stimulus position follows him.
A reaction ≥ `SearchLevel` leaves **alert memory** (`Alertness`, fading over `AlertMemoryTime` once calm):
up to 50% better hearing and up to twice as fast noticing.

The engine decides how long and how strongly; the guard's script decides what to do meanwhile
(`ai.reaction(e)` gives state, strength, time left, kind, cause and position). The demo's
`Scripts/Guard.lua`: patrol along the navmesh; Suspicious — stop and look, walk over if it lasts longer
than `glance_patience`; Investigating — go there, look around, check spots nearby; Searching — hurry there
and search the area, darkest spots first; Alarmed — chase while the player is seen, else search. Every
activity ends when the reaction does and the guard walks back to his route (`AILab` scene, end-to-end test
"The AI lab guard patrols…").

## Light gem (player HUD)

`PerceptionTarget.LightLevel` is the luminance of the body from the light it stands in, averaged over
all directions (independent of any guard); `LightGem` maps it to 0..1 on a log scale for the HUD. It tells
the player how lit they are; whether the silhouette stands out against what is behind them is left to the
player, as in the brief. `Exposure` (max stimulus B over guards) exists for debugging, not for the HUD.

## Cost

Guards evaluate vision at 10 Hz each, on staggered phases (no frame-time spikes). Benchmark
(`PenumbraTests -tc="Perception benchmark*" --no-skip`, release, 8 guards, 12 shadowed torches) is part of
the test sources; physics rays (Jolt) dominate.

`Scenes/PerceptionLab.pscene` (one guard, a campfire, the player; F1 shows every factor live) is the
place to tune vision by feel; `Scenes/AILab.pscene` (a patrolling guard, two rooms, floors of every
material, a bucket to knock off a table) is the place for hearing and behaviour. In both the player's
noise probe makes a test noise of any loudness: `-` / `=` pick the loudness, `G` makes it where you aim.

## Deliberate deviation: the contrast threshold

The brief proposes `C_t = C_min + k_dark / (L_a + ε)`. With that formula a *bright*-adapted eye has the
*lowest* threshold, which contradicts the brief's own consequences: a guard coming from a bright hall must
be briefly blind in a dark corridor, and a guard with a lantern must see worse outside the beam
(scenario 4 fails). Penumbra uses

```
C_t = C_min + k_dark / (L_obs + ε) + k_glare · max(0, log10(L_a / (L_obs + ε)))
```

where `L_obs = max(L_p, L_b)` is the brightness of the area being looked at. Darkness of the *observed*
area raises the threshold (need more contrast in the dark); an eye adapted brighter than what it looks at
pays a glare penalty that fades as `L_a` adapts (fast towards bright, slow towards dark). This reproduces
every scenario of section 8. Adaptation input is the average of the luminance where the guard stands and
of what it looks at (view probes plus the visible target samples).

## Tuning (PerceptionComponent)

| Field | Meaning | Default |
|---|---|---|
| `ContrastMin` | `C_min` | 0.1 |
| `DarkSensitivity` | `k_dark` | 0.004 |
| `GlareSensitivity` | `k_glare` per decade | 0.5 |
| `LuminanceFloor` | `ε` | 0.005 |
| `AdaptBrightTime` / `AdaptDarkTime` | `τ` (s) | 0.5 / 6 |
| `MotionFactor` | `m_motion` | 0.3 |
| `SuspicionRise` / `SuspicionDecay` | rates | 0.6 / 0.08 |
| `SuspiciousThreshold` | `D` from which a sighting is a stimulus | 0.3 |
| `HearingSensitivity` | distance multiplier for hearing | 1 |
| `ReactionDuration` | seconds a strength-10 stimulus lasts | 90 |
| `InvestigateLevel` / `SearchLevel` / `AlarmLevel` | state thresholds (strength) | 4 / 7 / 9 |
| `AlertMemoryTime` | seconds alert memory lasts once calm | 120 |
| `FieldOfView`, `PeripheralFactor`, `ViewDistance`, `EyeHeight` | cone | 120°, 0.2, 25 m, 1.65 m |

## Swapping the brightness source

`PerceptionSystem::SetLuminanceProvider` accepts any `LuminanceProvider`. A GPU "NPC eye" provider
(spec approach 3: render a 64×32 view with a player mask) can implement the same interface without
touching the perception logic.
