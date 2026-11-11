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

**Area and world events**: the scene's random seed (`--seed`), event zones (the cook in the kitchen
drops a knife, bangs a pot), the storm (rain, lightning that lights the player and dazzles guards,
thunder that masks footsteps), dogs, and the competition between a distraction and a guard's reaction
(`ai.explain_away`, `Diligence`). Out of scope, decided together: rain visuals (needs a particle system,
M-L), realistic voices and howls (need CC0 sound packs), NPC daily schedules (L).

**C1. Takedowns, bodies and talking guards**: the use key (#22), blackjack (#13), sleeping gas (#11),
bodies found by other guards and carried away (#14), shouts that spread the news, barks with subtitles
and a servant who runs to the alarm bell (#17). Out of scope: real voices (need recordings), ragdolls (a
body is a death animation, carried rigidly on the shoulders; ragdolls would be a package of their own, L).

## Next

### Arrows that matter (~2.5 sessions, next)

Arrows are too safe today: they only affect objects with response rules.

* **Harm** (~1): health on `Vitality` and a `Damage` stim action; fire arrows hurt and kill (a dead NPC is a
  body like a knocked-out one). A guard hit by any arrow raises the alarm and searches towards where it came
  from, not where it hit. Arrows make an impact noise by surface (about 3-4), so a near miss is heard.
* **Arrows seen in flight** (~1.5): a flying arrow is a short-lived perception target judged by the same
  contrast model: a dark arrow against a bright wall or sky is noticed, against darkness it is not, and a
  burning arrow in the dark stands out. It is tiny (the size factor keeps distant ones unseen) but fast
  (the motion factor needs a cap), and it crosses a view cone between two 10 Hz vision updates, so the
  whole flight segment since the last update is tested. A guard who sees one checks where it came from.
* Small: carried bodies sit on the shoulder instead of hanging beside the player.

### C2. Mission loop (~2 sessions)

Loot, objectives and difficulty (#21) with an end-of-mission summary; pickpocketing (#23) on the use key.

### B. Thief's workshop (~5 sessions)

Moss arrows (#8, the floor side is done), light switches (#24), doors that block sight
and sound and that guards open (#20, navmesh links), lockpicking (#19).

### Later: movement and UI (~12+ sessions)

Rope arrows (#9), climbing and mantling (#26), swimming (#27), leaning (#25), scouting orb (#28), map,
compass and readables (#30), inventory and shop (#29).
