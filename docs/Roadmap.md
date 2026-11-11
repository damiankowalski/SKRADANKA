# Roadmap

Ideas approved for Penumbra, in the order we plan to build them. Each package lands as one PR. Sizes are
working sessions: S = under one, M = one or two, L = three or more. Mechanics are numbered as in
[`research/Thief.md`](research/Thief.md).

## Done

**A. Guards that think** (navigation + hearing + reactions): Recast/Detour navmesh and agents, 0-10
loudness scale, sound around corners along the navmesh, surface-dependent footsteps and landing noise,
noisy physical objects, reactions that last in proportion to the stimulus, search behaviour, alert memory,
the `AILab` scene and the noise probe. Covers #3, #4, #5, #15, #16 and #18. The noisemaker arrow (#10) was
left out on purpose.

## Next

### Area events and world events (approved, ~3 sessions, after A)

Things that happen without the player and compete with him for the guards' attention. They use the same
pipeline as the player's noises (`ai.emit_noise`, `ai.notify`, `Environment.AmbientNoise`), so guards react
to them exactly as they would to the player.

* **Area events** (M, ~1): a zone of the map (a trigger volume) holds a list of events with weights, a
  chance per minute and a cooldown, active while the player is near. Example: near the kitchen the cook
  drops a knife (loudness 5) and a guard walks over to check. A cook that stands at his station and plays
  an animation is about 0.5 session more. Randomness comes from a seeded generator so headless tests stay
  deterministic.
* **World events** (M, ~1.5): scene-wide events on a schedule or by chance; a storm is the placeholder.
  * *Masking*: thunder raises `AmbientNoise` for a few seconds, so the player's footsteps disappear in it.
  * *Lightning*: a flash of light through the windows briefly lights the player (the light gem jumps) and
    dazzles guards through eye adaptation. The perception model already reads live lights, so this comes
    almost for free.
  * *Competition*: a world event has a strength. If it beats the evidence a guard is acting on (a noise
    he heard), he gives up ("just the storm") and goes back to his route; if he saw the player, he keeps
    searching. The comparison is deterministic, with an optional random spread.
  * Dogs howling outside is a sound only (no dog NPCs).
* **Out of scope for now**, decided together: rain needs a particle system (separate package, M-L);
  realistic voices and dog howls need CC0 sound packs (downloads need approval) because procedural audio
  sounds artificial; NPC daily schedules (a cook walking between stations) are a large system of their own (L).

### B. Thief's workshop (~5.5 sessions)

Frob/use key (#22), moss arrows (#8, the floor side is done), light switches (#24), doors that block sight
and sound and that guards open (#20, navmesh links), lockpicking (#19).

### C. Mission loop (~8 sessions)

Blackjack knockouts and unconscious/dead states (#13), gas (#11), guards noticing bodies and carrying them
(#14), loot, objectives and difficulty (#21), pickpocketing (#23).

### Later: movement and UI (~12+ sessions)

Rope arrows (#9), climbing and mantling (#26), swimming (#27), leaning (#25), scouting orb (#28), map,
compass and readables (#30), inventory and shop (#29).
