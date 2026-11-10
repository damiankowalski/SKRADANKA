# NPC perception

Implementation of the "ShadowProto" silhouette-contrast model from `GAME ENGINE SKRADANKA.md`.
Code: `Penumbra/src/Penumbra/AI/` — `PerceptionModel` (pure formulas), `LuminanceProvider`
(brightness sources), `PerceptionSystem` (per-frame evaluation). Tests: `PenumbraTests/src/PerceptionTests.cpp`
(formulas plus the seven scenarios of section 8 as end-to-end tests).

## Pipeline (per guard, 10 Hz; suspicion integrates every frame)

1. **Silhouette sampling** — each `PerceptionTarget` has 5–9 control points (default 7). For every point
   inside the view cone and distance: a physics ray from the eye checks occlusion (`f` = visible fraction).
2. **Luminances** — `L_p`: light falling on the side of the target facing the guard × clothing reflectance.
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
   States: Unaware < 0.3 ≤ Suspicious < 0.7 ≤ Searching < 1.0 = Alarmed.
7. **Hearing** — noise events (`ai.emit_noise`, footsteps, audio sources with `NoiseRadius`, explosions)
   add suspicion `HearingSuspicion·(1 − d/R)`; walls halve the radius.

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
| `FieldOfView`, `PeripheralFactor`, `ViewDistance`, `EyeHeight` | cone | 120°, 0.2, 25 m, 1.65 m |

## Swapping the brightness source

`PerceptionSystem::SetLuminanceProvider` accepts any `LuminanceProvider`. A GPU "NPC eye" provider
(spec approach 3: render a 64×32 view with a player mask) can implement the same interface without
touching the perception logic.
