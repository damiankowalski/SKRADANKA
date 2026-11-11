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

## Takedowns, bodies and shouts

* **Vitality** — `ai.knock_out(e, dead?)` (the player's blackjack, the `Knockout` stim action of sleeping gas)
  sets `Vitality.State` to Unconscious or Dead. A downed NPC perceives nothing, stops walking, loses its
  physics character (lies where it fell) and becomes a **body**: a `PerceptionTarget` of kind Body with low,
  long silhouette points and dark reflectance.
* **Bodies** are seen through the same contrast model as the player — a body in a dark corner goes
  unnoticed, one in torchlight does not. A body is found once per guard: a Seen stimulus of strength
  `BodyAlarm` (8: a search around it) that no storm can explain away. Carrying a body (Player.lua: E)
  removes its target until it is put down.
* **Shouts** — `ai.shout(e, loudness)`: every other awake NPC who would hear a noise of that loudness from the
  speaker's mouth (distance, walls, paths around them, ambient masking) gets a Reported stimulus at the
  speaker's stimulus position, one level weaker. The demo guard shouts when he starts a search or raises the
  alarm (not when he is only passing on a shout, so news spreads without echoing). The servant runs to the
  alarm bell instead; ringing it reports the trouble to every guard at strength 9.
* **Lines** — guards say a line on every change of mind ("Hm?", "I'd better take a look.", "Must have been
  rats."), shown above their heads with `ui.world_to_screen` (`Scripts/AI/Actor.lua`). Voices are a
  placeholder sound.

## Arrows

* **Harm** — `Vitality.Health` (100). The `Damage` stim action (and `ai.damage`) takes health; at 0 the NPC is
  Dead (a body). The demo's NPCs take 75 from fire (two fire arrows kill; a powder keg's blast hurts too).
* **Being hit** — the `Alarm` stim action gives a Seen stimulus (default strength 9.5, cause "Hit") at
  *where the stim came from*: 8 m back along a projectile's flight (`StimSystem::ProjectileBacktrack`), or
  the centre of a blast. A hit guard runs to search where the shot came from, not where it struck. Water
  arrows do no harm but raise the alarm the same way.
* **Impact noise** — an arrow striking something makes a noise of 3.5 plus the struck surface's
  `NoiseModifier` (metal 6, carpet 1.5: `Scripts/Arrow.lua`), so a near miss can give the archer away.
* **Seen in flight** — arrows carry a `PerceptionTarget` of kind Projectile: a thin shaft judged by the same
  contrast model as the player, *every frame* (they cross a view cone between two 10 Hz vision updates).
  Against a bright wall or sky a dark arrow is noticed; against darkness it is not; a burning arrow
  (`Emissive`) glows and is seen in the dark. The motion factor is capped (`MaxMotionFactor` 4) so speed
  does not make tiny arrows obvious, and the size factor keeps distant ones unseen. Seen once, an arrow
  is a Seen stimulus of strength `ProjectileAlarm` (8: a search) at where it was shot from
  (`PerceptionTarget.RuntimeOrigin`).

## Events and distractions

Things happen without the player and compete with him for the guards' attention (`Scripts/World/`, odds
and strengths in `Scripts/World/Events.lua`).

* **Area events** (`EventZone.lua`): while the player is near a zone, its events fire by chance (per minute,
  with cooldowns) from the zone's actor: in the kitchen the cook drops a knife (loudness 5), bangs a pot (6),
  cracks something (3). They are ordinary noises, so a guard reacts exactly as he would to the player.
* **World events** (`WorldEvents.lua`): a storm (the placeholder for any weather) rains
  (`Environment.AmbientNoise` 2: the quietest steps vanish), and each strike flashes the `Lightning` light
  and the windows — the player is lit up for a moment and the guards' eyes adapt upwards (dazzled) — then
  thunders: `AmbientNoise` 7 for 3 s masks almost every sound. Dogs howl beyond the walls.
* **Competition** — thunder and dogs also offer an innocent explanation: `ai.explain_away(strength, cause)`.
  For every guard reacting to something *heard or reported* (never *seen*) the distraction wins when

  ```
  strength + spread · roll  >  remaining reaction strength + Diligence        (roll in [-1, 1])
  ```

  and he puts it down to the weather (`ai.calm`: back to his route, alert memory kept). Thunder is 6 ± 2: a
  creak heard at 3.6 is always dismissed, a bucket heard at 8 never; in between it is luck. `Diligence` on a
  guard makes him harder to talk out of a search.
* **Randomness** comes from the scene's seed: Lua's `math.random` and engine rolls start from it when the
  game starts. It is fixed by default (tests, headless runs, `--fixed-timestep`); the interactive runtime and
  the editor's play mode use a new seed for every scene load; `PenumbraRuntime --seed N` replays one.

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
| `Diligence` | levels added to his reaction against distractions | 0 |
| `BodyAlarm` | strength of the reaction to finding a body | 8 |
| `ProjectileAlarm` | strength of the reaction to an arrow seen flying by | 8 |
| `FieldOfView`, `PeripheralFactor`, `ViewDistance`, `EyeHeight` | cone | 120°, 0.2, 25 m, 1.65 m |

## Swapping the brightness source

`PerceptionSystem::SetLuminanceProvider` accepts any `LuminanceProvider`. A GPU "NPC eye" provider
(spec approach 3: render a 64×32 view with a player mask) can implement the same interface without
touching the perception logic.
