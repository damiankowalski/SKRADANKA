# World and architecture

The anchor for everyone who builds levels, writes AssetGen prompts or picks materials: what world the game is set
in and what its buildings are made of. Set by the owner on 2026-11-20. The look (light, fog, colour) is direction B
in `docs/art/mood/README.md`; this file covers the world and its architecture. Change it only with the owner.

## The world

* **Medieval at heart.** A walled city, its manors, cloisters, workshops and poor streets, and the land outside.
  Stone, timber, plaster, iron. Light comes from fire: torches, candles, fire baskets, hearths.
* **Magic, rare and practical.** Not a world of wizards: magic shows in things people use. Astral lamps (a cold
  crystal light), the thief's elemental bolts (water, fire, gas, moss), wards and charms.
* **Nature takes back what people leave.** Wild animals, and stranger creatures, move into abandoned districts,
  ruins, cellars and the land outside the walls. Empty places are overgrown, damp and not empty.
* **Early steam machinery as accents.** Pipes, boilers, valves, brass gauges and gears in workshops, the rich
  quarters' gadgets and the city's works. An accent laid over the medieval city, not a steam city.
* **Its spirit**: the classic first-person thief games (the owner's reference: Thief). A dark city of contrasts
  between rich manors and poor streets, a thief in the shadows, guards with lanterns. Prompts never name the
  game (AssetGen's rule); this file may, as a reference.

## Architecture: what things are made of

Every surface is made of what such a surface was made of. A shortcut like the walls' stone on the roof breaks it.

| Part | Made of | Not |
|---|---|---|
| Walls | Dressed or rough stone (castles, cloisters, cellars, city walls); timber frame with plaster (houses); brick here and there | |
| Roofs | Roofing: wooden shingles, thatch (straw, reed), fired clay tiles; tarred boards or planks on poor sheds | the walls' stone; slate that reads as wall stone |
| Walkable tops | Only where a wall carries a walk (a wall walk, a balcony, battlements): stone flags with a parapet | a sloping roof in paving |
| Ceilings | Timber: planks on beams; stone vaults in halls, crypts and cellars | |
| Floors | Cobbles or setts outdoors, stone flags in halls, boards in houses, beaten earth or mud in poor places | |
| Doors, shutters | Planks with iron bands and nails | |
| Machinery | Iron, copper and brass pipes, rivets, gauges, gears; soot and verdigris | chrome, clean plastic-like metal |

**Wear tells the story.** Dirt, damp and moss gather at the foot of walls and in corners (floors are walked less
at the wall), soot above torches and hearths, damp stains under roofs and eaves, moss and plants where people
have left. Machinery leaks rust and verdigris.

## Using it

* **AssetGen prompts** (`Tools/AssetGen/prompts`) describe one material at a time in these terms; the shared
  `style.txt` stays a description of the painting style, not of the world.
* **Levels** (`Tools/BuildDemoProject.py`, the `penumbra-build-game` skill) pick each surface's material from
  the table above.
* **The texture catalog** (`docs/art/textures/catalog`) notes what each variant could serve in these terms.
