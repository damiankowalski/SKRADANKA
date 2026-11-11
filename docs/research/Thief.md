# Thief 1 & 2 — what they showed the world

Research notes for designing Penumbra games. Nothing here is implemented as part of this note;
the status column only records what the engine already has. Costs are to be estimated together.

## How strong were they?

- **Thief: The Dark Project** (Looking Glass Studios, 1998, Dark Engine) is credited as the first PC
  stealth game built on light and sound as core mechanics, and as a cornerstone of the "immersive sim"
  genre (systems that combine into emergent play). Its lineage runs through Splinter Cell, Hitman and,
  most directly, Dishonored (Harvey Smith, a key designer on the series, led it). Tom Leonard, who built
  and tuned Thief's stealth AI, later shaped the Combine soldiers of Half-Life 2.
- **Thief II: The Metal Age** (2000, third iteration of the Dark Engine) was even better received:
  Metacritic 87 from critics, 8.9 from users; MobyGames 88%. It sold better at launch than the first game
  (UK ELSPA Silver, 100k+ copies), and its fan scene is still active (e.g. the 2005 total conversion
  *Thief 2X: Shadows of the Metal Age*).
- Both remain the reference for "pure" stealth: the player is weak in open combat, information
  (light, sound, guard behaviour) is the main resource, and levels are open with many routes.

## Mechanics

| # | Mechanic | What it does in Thief | Penumbra today |
|---|---|---|---|
| 1 | **Light gem** | HUD gem showing how lit the player is; staying in shadow is the core skill | Yes: `LightGem` (PerceptionTarget), HUD in the demo |
| 2 | **Light-based visibility** | Guards see you depending on the light on you | Yes, extended: silhouette contrast vs background, eye adaptation, periphery, motion |
| 3 | **Sound propagation through rooms** | Sounds travel through doorways and corridors (room/portal graph), are muffled by closed doors, reach guards around corners | Partial: distance + "through a wall halves the radius"; no room/portal paths, no doors |
| 4 | **Surface-dependent footsteps** | Metal and tile are loud, carpet/grass/moss silent; levels designed around it | Missing (footsteps depend only on walk/run/crouch) |
| 5 | **Movement noise levels** | Creep / walk / run, plus landing from jumps, make different noise | Partial (walk/run/crouch); no landing noise |
| 6 | **Water arrows** | Douse torches and fires; clean blood | Yes (Act/React stims) |
| 7 | **Fire arrows** | Ignite, explode, kill | Yes (ignite torches, detonate powder kegs) |
| 8 | **Moss arrows** | Carpet a floor with moss to walk silently | Missing (needs #4) |
| 9 | **Rope arrows** | Stick into wood, climb the rope | Missing (needs climbing + material tags) |
| 10 | **Noisemaker arrows** | Lure guards away with a noise | Easy: `ai.emit_noise` exists, needs the arrow + "investigate" behaviour |
| 11 | **Gas arrows / gas mines** | Knock out groups silently | Missing (stim exists, knockout state missing) |
| 12 | **Broadhead arrows, flash bombs, mines** | Combat tools; flash blinds AI | Partial (stims); blindness would hook into eye adaptation |
| 13 | **Blackjack knockouts** | Non-lethal takedown from behind | Missing (AI states: unconscious/dead) |
| 14 | **Carrying and hiding bodies** | Bodies left in the light alert other guards | Missing (AI noticing bodies = a perception target "body") |
| 15 | **Guard alert levels** | Graded suspicion ("must have been rats…"), search, alarm, calming down | Yes: suspicion meter with Unaware/Suspicious/Searching/Alarmed and decay |
| 16 | **Search behaviour** | Guards investigate the last known position, search nearby hiding spots, give up | Partial: walk to last stimulus; no area search / spots |
| 17 | **AI barks and communication** | Spoken lines reveal AI state; guards alert each other, servants flee to raise the alarm | Missing (audio exists; no voice system or AI-to-AI messages) |
| 18 | **Patrols and schedules** | Predictable routes the player learns | Yes (waypoints) |
| 19 | **Lockpicking** | Timed picking of doors and chests | Missing (no doors yet) |
| 20 | **Doors** | Open/closed doors block sight and sound, can be locked | Missing |
| 21 | **Loot and objectives by difficulty** | Steal enough loot; harder difficulties add objectives and a no-kill rule | Missing (Lua can implement it; no objective/inventory system) |
| 22 | **Frobbing (interaction)** | One "use" key for doors, items, switches, pickpocketing | Missing |
| 23 | **Pickpocketing** | Steal keys and purses from guards' belts | Missing (needs #22) |
| 24 | **Light switches and lamps** | Turn electric lights off (Thief 2) to create shadows | Easy (lights have `Enabled`; needs #22) |
| 25 | **Leaning / peeking** | Lean around corners with minimal exposure | Missing (camera/silhouette offset) |
| 26 | **Climbing and mantling** | Mantle ledges, climb ropes and vines | Missing |
| 27 | **Swimming** | Water volumes, breath | Missing |
| 28 | **Scouting orb, security cameras, turrets** (Thief 2) | Remote viewing; mechanical observers with their own perception | Cameras: easy (a `Perception` component on a turret entity) |
| 29 | **Inventory and shop** | Choose equipment between missions | Missing |
| 30 | **Map and compass, readables** | Hand-drawn maps, books and notes with lore and hints | Missing (UI is text/rects only) |
| 31 | **Open, multi-route level design** | Many entrances, secrets, verticality | A content question; the engine supports it |
| 32 | **Emergent simulation (Act/React)** | Objects react to stims; systems combine | Yes: `StimSystem` |

## Sources

- [Thief: The Dark Project — Wikipedia](https://en.wikipedia.org/wiki/Thief:_The_Dark_Project)
- [PC Gamer: In 1998, Thief defined the stealth game](https://pcgamer.com/20-years-ago-thief-the-dark-project-defined-the-stealth-game)
- [PC Gamer: the innovations of Thief have been forgotten](https://pcgamer.com/stealth-is-everywhere-in-games-but-the-innovations-of-thief-have-been-forgotten)
- [PCGamesN: Thief retrospective](https://www.pcgamesn.com/thief-the-dark-project/retrospective)
- [Shacknews: Thief dev answers questions on sound and stealth](https://www.shacknews.com/article/79782/thief-dev-answers-community-questions)
- [Thief 2 — Wikipedia](https://www.Wikipedia.org/wiki/Thief_2)
- [Thief II on Metacritic](https://legacy.prod.metacritic.com/game/pc/thief-ii-the-metal-age)
- [Thief II on MobyGames](https://www.mobygames.com/game/1261)
- [The Dark Mod wiki: AI stats](https://wiki.thedarkmod.com/index.php/AI_stats) (fan successor, for AI design reference)
- [Stealth Game Spotting Deconstruction](https://critpoints.net/2015/03/30/stealth-game-spotting-deconstruction/)
