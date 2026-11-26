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
   two black values give ≈ 0 contrast). Points outside the focus cone (`FocusFieldOfView`, 90°) but inside
   the full cone (`FieldOfView`, 130°: 20° bands on each side) count with `PeripheralContrast` (0.5) of
   their contrast: the corner of the eye notices the thief, only less readily. `L_p` and `L_b` are taken as the
   eye gets them through the fog (see Fog below). Each point counts the larger of this and the body's own shading
   (see Self-shading below). Averaged over visible points, then × `exp(−optical depth)` of smoke volumes along the
   line.
4. **Threshold** — see the deviation below.
5. **Factors** — size `S` (angular size vs. `ReferenceAngularSize`), peripheral `P(θ)` (1 → `PeripheralFactor`
   at the cone edge), motion `M = 1 + m·speed·(1 + peripherality)`.
6. **Stimulus** `B = (C_eff / C_t)·f·S·P·M`; suspicion `D` rises by `r_rise·(B−1+floor)·dt` when `B ≥ 1`, else decays
   (see Suspicion tempo below).
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
* **through a closed door** — the door's `Surface.SoundLoss` less (2.5 for the demo's wooden doors; a
  body without one, or with a negative one, is a wall). Several doors add up, never beyond a wall;
* **around it** — when the navmesh connects the noise and the guard, the walking distance with 1 level of
  corner loss (`CornerLoss`). Sound goes through doorways and along corridors like Thief's room graph,
  without authoring rooms. Doors are kinematic, so the navmesh runs through them: each closed door on the
  way costs its `SoundLoss` too. The louder wins.

Closed doors block sight and light like walls; open, they swing out of the way (`Scripts/World/Door.lua`).

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

## Suspicion tempo

`B ≥ 1` means the guard registers the thief; how fast he reacts follows from `D += r_rise·(B − 1 + floor)·dt`
(`SuspicionRise` 0.6, `SuspicionFloor` 0.25). Without the floor the rise vanishes at the threshold itself: a thief
at B 1.05 took 10 s to a mutter and 30 s to an alarm, and one at exactly 1 was registered and never reacted to. With
it, whatever is registered is reacted to within seconds if it stays in sight, while a clearly seen thief changes
little. Suspicion `D` 0.3 is a mutter (Suspicious), 0.4 Investigating, 0.7 Searching, 0.9 Alarmed (strength `10·D`
against the levels below); alert memory multiplies the rise.

| B | mutter (D 0.3) | alarm (D 0.9) | without the floor |
|---|---|---|---|
| 1.0 | 2.0 s | 6.0 s | never |
| 1.05 | 1.7 s | 5.0 s | 10 s / 30 s |
| 1.2 | 1.1 s | 3.3 s | 2.5 s / 7.5 s |
| 1.5 | 0.7 s | 2.0 s | 1.0 s / 3.0 s |
| 2 | 0.4 s | 1.2 s | 0.5 s / 1.5 s |
| 5 | 0.12 s | 0.35 s | 0.13 s / 0.38 s |

Glimpses shorter than that still fade without a reaction (suspicion decays 0.08/s). Measured with the playtester's
PhysicsLab routes (`Assets/Playtest/PhysicsLab`, the thief still against the torch-lit haze, B 1.1-1.5) and the
`Suspicion tempo report` test (every level's spots, floor 0 against 0.25).

## Touch and close range

Shadow hides the player from eyes, not from hands.

* **Touch** — an intruder whose capsule is within `TouchRange` (0.15 m) of the NPC's (the character
  controllers, or the target's `Radius`; their heights must overlap) is a Seen stimulus of strength
  `TouchAlarm` (10: alarmed) at the intruder, every frame they touch, whatever the light. It works both
  ways: a guard walking into the player and the player bumping into a guard's back. Suspicion jumps to 1.
  Characters collide, so a guard never walks into or through the player. Bodies, arrows and the
  unconscious feel nothing. `PerceptionSystem::IsTouching` tells whether two are in contact.
* **Close range** — an intruder closer than `CloseRange` (1.2 m, horizontally) and in view is made out
  even in pitch darkness: the sight is at least `CloseRangeStimulus` (2) × visible fraction × peripheral
  factor. That is slow on purpose — suspicion rises 0.75/s, so a guard face to face with the player in
  the dark mutters after about 0.4 s and raises the alarm after about 1.2 s, time enough to back
  off. Behind his back (outside the cone) close range does nothing; touch still does.

## Takedowns, bodies and shouts

* **Vitality** — `ai.knock_out(e, dead?)` (the player's blackjack, the `Knockout` stim action of sleeping gas)
  sets `Vitality.State` to Unconscious or Dead. A downed NPC perceives nothing, stops walking, loses its
  physics character and becomes a **body**: a `PerceptionTarget` of kind Body with low, long silhouette
  points and dark reflectance. With a `Ragdoll` it goes limp and the seven points sit on its parts (hips,
  chest, head, forearms, shins) wherever they lie: a body folded behind a crate shows less of itself.
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
  and the windows — the player is lit up for a moment and the guards' eyes adapt upwards (dazzled); the flash
  has the shadow map while it lasts, so it lights only what it reaches through windows, doorways and open
  ground, for the player and the guards alike — then
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

## Self-shading: a lit body is never flat

A figure is not a flat patch: lit from one side it shows a bright side and a dark side, and an eye makes it out by
that even against a background exactly as bright as he is. For each visible point the light on the body (the
lights gathered at that point, `PointLighting`) gives the mean brightness of the visible disc's half towards each
light and the half away (`PerceptionModel::SphereShadingHalves`, a Lambert sphere, tabulated once from the exact
integral: light from behind the eye 2/3 and 2/3, flat; from the side 4/(3π) and 0; from behind the body 0 and 0).
The lights' bright sides add as vectors across the view (two lamps on opposite sides cancel), ambient light and the
floor's bounce lift both halves alike. Then

    C_own = SelfShading · D · (L_bright' − L_dark') / max(L_bright', ε)

with `L'` through the fog like `L_p`, `SelfShading` (0.5) a weight (a body's own shading says less than its outline
against the background) and `D` the detail factor: the size factor with a reference three times larger
(`SelfShadingDetail`): shading has to be resolved, it fades with distance before the outline does. Each point
counts `max(C_background, C_own)`.

* A thief lit from the side in front of a wall as bright as he is: seen in 0.8 s instead of 2.0 s (`Self-shading 1`
  in PerceptionTests). Lit from the front (flat) or in the dark: as before.
* In the demo levels it changes little: guards there rarely face a thief lit from the side against an equally
  bright background (the self-shading report: `PenumbraTests -tc="Self-shading report*" --no-skip`).
* Simplified: one number per half of a round body; no colours, patterns or the outline of a person.
* Debug: the F4 label's "own shading"; Lua `ai.visibility(...).self_contrast`; `SelfShading` 0 turns it off.

## Fog: seen lit, as the player sees it

Guards see the fog the player sees (`Scene/FogUtils.h`, `Fog.md`): its density (the environment's fog, thinning with
height, and fog volumes) and the light it scatters towards the eye (lamps, their falloff, `FogScattering`,
`FogAnisotropy`, the fog's own `FogColor`). Along each sight line, `T1` and `S1` are the transmittance and the glow
from the eye to the thief's point, `T2` and `S2` from the point on to the background (as seen from the point):

    L_p' = L_p·T1 + S1
    L_b' = (L_b·T2 + S2)·T1 + S1

Contrast and threshold use `L_p'` and `L_b'`. So a dark thief against fog lit by a lamp behind him stands out (`S2`
brightens his background, he stays dark); bright fog between guard and thief hides him (`S1` adds the same light to
both and the contrast falls); dark fog changes little; without fog `T = 1` and `S = 0` and nothing changes. Smoke
(`SmokeVolume`, drawn unlit) still only multiplies the contrast by `exp(−optical depth)`.

* **The glow** is integrated on the CPU (`FogMedium::Integrate`): steps of at most 0.75 m (at most 64, stopping once
  almost nothing gets through), each adding `T · source · (1 − e^(−σ·step))` exactly as `shaders/FogIntegrate.hlsl`
  adds a froxel slice. The source is `FogColor` plus `FogScattering` × the lamps' light at that point
  (`LuminanceProvider::FogLight`: the renderer's falloff and Henyey-Greenstein phase, `FogPhase`; a lamp with a
  shadow map is shadowed by a ray, one without lights the fog at `FogUnshadowedLight` (0.5), as on screen). With
  `VolumetricFog` off only `FogColor` and the environment's fog count, as on screen.
* **Once per sight line**: `S1` and `S2` are integrated along the line to the middle of the visible points and on
  behind it, then shared out by each point's own transmittance: `S_i = S · (1 − T_i) / (1 − T)` (exact for an evenly
  lit fog). `T1` and `T2` are exact per point (`FogMedium::OpticalDepth`).
* **Agreement with the renderer**: `RendererTests.cpp` compares the CPU glow with the froxel grid read back from
  the GPU along 30 sight lines through lit, shadowed and misty fog: within 20% (or 0.002) and transmittance within
  0.03; measured 2-5% where lit, 16% on dim lines in a wall's shadow (the GPU's one-tap shadow map).
* **Simplified**: no froxels, jitter or temporal smoothing (the CPU is continuous); shadows by ray, not by shadow
  map, and every lamp with `CastShadows` gets one (the renderer gives maps to the nearest lamps reaching the view); the
  glow does not change the eye's adaptation (the probes sample surfaces); past the renderer's 64 m grid the CPU
  still lights the fog (lamps do not reach that far).
* **Without fog** (no environment fog, no fog volumes) the old path runs unchanged, at no extra cost.
* **Interiors**: a `Clear` FogVolume keeps the level's fog out of a room for guards as on screen (`docs/Fog.md`).
* **Future density changes** (drifting noise, disturbances) go into `FogMedium::DensityAt`
  and `shaders/Fog.hlsl` together; perception samples `DensityAt` and needs no change.
* **Debug**: the F4 label shows the thief's and the background's luminance as the eye gets them and the fog's
  `T`, glow in front and behind; "Guards see the fog's glow" switches the model off to compare
  (`PerceptionSystem::SetFogGlow`). Lua: `ai.visibility` returns `fog_transmittance`, `fog_glow_front`,
  `fog_glow_behind`.
* **Try it**: `Scenes/PerceptionLab.pscene` has a bank of mist lit from inside behind (6, -1) and a lamp in a puff
  of mist between the guard and the lit doorway, in front of (-7, 0.4) (`docs/art/perception/FogGlow.jsonl` renders
  both from the guard's eye).

## Cost

Guards evaluate vision at 10 Hz each, on staggered phases (no frame-time spikes). Benchmarks
(`PenumbraTests -tc="Perception benchmark*" --no-skip`, release: 8 guards and 12 shadowed torches; 10 guards, 12
torches and lit fog with the glow on and off) are part of the test sources; physics rays (Jolt) dominate. On an M1
Pro the 10 guards cost 0.035 ms a frame with the fog's glow, 0.026 ms with fog only dimming, 0.031 ms without fog.

`Scenes/PerceptionLab.pscene` (one guard, a campfire, the player; F1 shows every factor live) is the
place to tune vision by feel; `Scenes/AILab.pscene` (a patrolling guard, two rooms, floors of every
material, a bucket to knock off a table) is the place for hearing and behaviour. In both the player's
noise probe makes a test noise of any loudness: `-` / `=` pick the loudness, `G` makes it where you aim. In any
level the runtime's debug panel (F4, [DebugTools.md](DebugTools.md)) draws every guard's view fan cut by walls and
his label (state, suspicion, the thief's `B`), rings around noises, and makes test noises with a mouse click.

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
| `SuspicionFloor` | added to `B − 1` while `B ≥ 1`: the rise at the threshold itself | 0.25 |
| `SuspiciousThreshold` | `D` from which a sighting is a stimulus | 0.3 |
| `HearingSensitivity` | distance multiplier for hearing | 1 |
| `ReactionDuration` | seconds a strength-10 stimulus lasts | 90 |
| `InvestigateLevel` / `SearchLevel` / `AlarmLevel` | state thresholds (strength) | 4 / 7 / 9 |
| `AlertMemoryTime` | seconds alert memory lasts once calm | 120 |
| `Diligence` | levels added to his reaction against distractions | 0 |
| `BodyAlarm` | strength of the reaction to finding a body | 8 |
| `ProjectileAlarm` | strength of the reaction to an arrow seen flying by | 8 |
| `TouchRange` / `TouchAlarm` | gap between capsules that counts as contact (m), strength of the reaction | 0.15 / 10 |
| `SelfShading` | weight of the target's own shading (lit side against shadowed side) next to its contrast with the background; 0 off | 0.5 |
| `CloseRange` / `CloseRangeStimulus` | distance (m) within which a figure in view is made out in the dark, minimum sight | 1.2 / 2 |
| `FieldOfView`, `PeripheralFactor`, `ViewDistance`, `EyeHeight` | cone | 130°, 0.2, 25 m, 1.65 m |
| `FocusFieldOfView`, `PeripheralContrast` | central cone of full contrast; contrast multiplier in the bands beyond it | 90°, 0.5 |

## Swapping the brightness source

`PerceptionSystem::SetLuminanceProvider` accepts any `LuminanceProvider`. A GPU "NPC eye" provider
(spec approach 3: render a 64×32 view with a player mask) can implement the same interface without
touching the perception logic.
