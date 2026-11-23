"""Generates the DemoProject content: scenes, prefabs and procedural sound effects.

The Lua scripts and the HDRI are hand-made assets checked in as-is; everything produced here is
deterministic so regenerating yields identical files. Run from anywhere:

    python Tools/BuildDemoProject.py
"""

import json
import math
import random
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "DemoProject"
ASSETS = ROOT / "Assets"


class Document:
    """Builds a Penumbra scene/prefab document with stable, sequential entity ids."""

    def __init__(self, fmt, first_id=1000):
        self.format = fmt
        self.entities = []
        self.next_id = first_id

    def add(self, name, parent=None, **components):
        entity_id = str(self.next_id)
        self.next_id += 1
        record = {"ID": entity_id, "Components": {"Tag": {"Tag": name}}}
        if parent is not None:
            record["Parent"] = parent
        record["Components"].update(components)
        self.entities.append(record)
        return entity_id

    def save(self, path):
        path = ASSETS / path
        path.parent.mkdir(parents=True, exist_ok=True)
        document = {"Format": self.format, "Version": 1, "Entities": self.entities}
        path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def transform(position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)):
    return {"Translation": list(position), "Rotation": list(rotation), "Scale": list(scale)}


def material(color, roughness=0.8, metallic=0.0, emissive=0.0, emissive_color=(1, 1, 1), translucent=False):
    return {"BaseColor": list(color) + ([1.0] if len(color) == 3 else []), "Roughness": roughness, "Metallic": metallic,
            "EmissiveIntensity": emissive, "EmissiveColor": list(emissive_color), "Translucent": translucent}


def box(doc, name, position, size, color, parent=None, emissive=0.0, emissive_color=(1, 1, 1), collider=True, roughness=0.85,
        surface=None, mat=None):
    components = {
        "Transform": transform(position, scale=size),
        "Mesh": {"Mesh": "builtin:cube"},
        "Material": mat or material(color, roughness=roughness, emissive=emissive, emissive_color=emissive_color),
    }
    if collider:
        components["BoxCollider"] = {"HalfExtents": [0.5, 0.5, 0.5]}
    if surface:
        components["Surface"] = SURFACES[surface]
    return doc.add(name, parent, **components)


# Floor materials and how they change footsteps and impacts (loudness levels, 0-10 scale; see
# Scripts/AI/Noises.lua). Floors are thin collider boxes laid over the ground.
SURFACES = {
    "Stone": {"Material": "Stone", "NoiseModifier": 0.0},
    "Carpet": {"Material": "Carpet", "NoiseModifier": -2.0},
    "Grass": {"Material": "Grass", "NoiseModifier": -1.0},
    "Moss": {"Material": "Moss", "NoiseModifier": -3.0},
    "Wood": {"Material": "Wood", "NoiseModifier": 0.5},
    "Metal": {"Material": "Metal", "NoiseModifier": 2.5},
}
SURFACE_COLORS = {"Carpet": (0.35, 0.08, 0.07), "Grass": (0.12, 0.2, 0.08), "Moss": (0.1, 0.17, 0.09),
                  "Wood": (0.32, 0.22, 0.13), "Metal": (0.3, 0.32, 0.34)}


def floor_patch(doc, name, surface, center, size):
    """A walkable floor of another material, 2 cm thick, over the ground."""
    x, z = center
    w, d = size
    return box(doc, name, (x, 0.01, z), (w, 0.02, d), SURFACE_COLORS[surface], surface=surface,
               roughness=0.35 if surface == "Metal" else 0.9)


def noisy_object(doc, name, position, size, material, loudness, sound, color):
    """A small dynamic object that makes noise when it falls or is knocked over."""
    return doc.add(name, Transform=transform(position, scale=size), Mesh={"Mesh": "builtin:cylinder" if material == "Metal" else "builtin:cube"},
                   Material=material_of(color, material), BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]},
                   RigidBody={"Type": "Dynamic", "Mass": 2.0 if material == "Metal" else 4.0, "Friction": 0.5},
                   Surface={"Material": material, "NoiseModifier": SURFACES[material]["NoiseModifier"],
                            "ImpactLoudness": loudness, "ImpactSound": sound})


def material_of(color, surface):
    return material(color, roughness=0.3 if surface == "Metal" else 0.85, metallic=0.8 if surface == "Metal" else 0.0)


# --------------------------------------------------------------------------------------------
# Prefabs
# --------------------------------------------------------------------------------------------

def torch_prefab_entities(doc, position=(0, 0, 0), parent=None, shadows=False, name="Torch"):
    """Wall torch: a post plus a flame child (flickering light, emissive, continuous fire)."""
    post = doc.add(name, parent,
                   Transform=transform(position),
                   StimResponse={"Rules": [
                       {"Stim": "Water", "Threshold": 0.3, "Once": False, "Actions": [
                           {"Type": "DisableLights"},
                           {"Type": "PlaySound", "Asset": "Audio/Hiss.wav", "Value": 0.7},
                           {"Type": "SpawnPrefab", "Asset": "Prefabs/Steam.pprefab"}]},
                       {"Stim": "Fire", "Threshold": 0.3, "Once": False, "Actions": [
                           {"Type": "EnableLights"}]}]})
    doc.add("Pole", post, Transform=transform((0, 0.6, 0), scale=(0.08, 1.2, 0.08)),
            Mesh={"Mesh": "builtin:cylinder"}, Material=material((0.25, 0.16, 0.1)),
            BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]})
    doc.add("Wrap", post, Transform=transform((0, 1.22, 0), scale=(0.13, 0.16, 0.13)),
            Mesh={"Mesh": "builtin:cylinder", "CastShadows": False}, Material=material((0.08, 0.06, 0.04), roughness=1.0))
    doc.add("Flame", post, Transform=transform((0, 1.28, 0)),
            Flame={"Height": 0.5, "Radius": 0.11, "Intensity": 4.5},
            PointLight={"Color": [1.0, 0.55, 0.25], "Intensity": 11.0, "Range": 10.0, "Flicker": 0.18, "CastShadows": shadows},
            StimSource={"Stim": "Fire", "Delivery": "Continuous", "Radius": 0.5, "Intensity": 1.0},
            # A generous sensor around the flame: arrows aimed at it register a hit (sensors never block sight).
            SphereCollider={"Radius": 1.2}, RigidBody={"Type": "Static", "IsTrigger": True})
    return post


def arrow_target(emissive=0.0):
    """Guards can see an arrow in flight: a thin shaft judged by the same contrast model as the player
    (a burning one glows). Seen, it sends them to where it was shot from."""
    return {"Kind": "Projectile", "SilhouettePoints": [[0, 0, -0.3], [0, 0, 0], [0, 0, 0.3]], "Radius": 0.04,
            "Reflectance": 0.25, "Emissive": emissive}


def bolt(doc, arrow, color, emissive, light=None):
    """The crossbow bolt's look (it flies along -Z): a wooden shaft, an iron head and two crossed
    fletchings in the bolt's colour. The fletchings glow a little so the flight can be followed in
    the dark. Visual only: the arrow's physics is the small sphere on the root."""
    wood = material((0.32, 0.22, 0.12), roughness=0.9)
    doc.add("Shaft", arrow, Transform=transform(rotation=(90, 0, 0), scale=(0.022, 0.55, 0.022)),
            Mesh={"Mesh": "builtin:cylinder", "CastShadows": False}, Material=wood)
    doc.add("Head", arrow, Transform=transform((0, 0, -0.3), scale=(0.04, 0.04, 0.09)),
            Mesh={"Mesh": "builtin:sphere", "CastShadows": False}, Material=material((0.55, 0.55, 0.58), roughness=0.35, metallic=0.9))
    fletching = material(color, emissive=emissive, emissive_color=color)
    for index, size in enumerate(((0.1, 0.006, 0.13), (0.006, 0.1, 0.13))):
        doc.add(f"Fletching{index + 1}", arrow, Transform=transform((0, 0, 0.21), scale=size),
                Mesh={"Mesh": "builtin:cube", "CastShadows": False}, Material=fletching)
    if light:
        doc.add("Glow", arrow, Transform=transform((0, 0, -0.3)),
                PointLight={"Color": list(light), "Intensity": 4.0, "Range": 5.0, "Flicker": 0.2})


def build_prefabs():
    doc = Document("PenumbraPrefab")
    torch_prefab_entities(doc)
    doc.save("Prefabs/Torch.pprefab")

    for name, stim, color, light in (("WaterArrow", "Water", (0.3, 0.6, 1.0), None),
                                     ("FireArrow", "Fire", (1.0, 0.45, 0.1), (1.0, 0.5, 0.2))):
        doc = Document("PenumbraPrefab")
        arrow = doc.add(name,
                        Transform=transform(scale=(1, 1, 1)),
                        SphereCollider={"Radius": 0.06},
                        RigidBody={"Type": "Dynamic", "Mass": 0.1, "GravityFactor": 0.3, "ContinuousCollision": True,
                                   "TransparentToSight": True},
                        StimSource={"Stim": stim, "Delivery": "Contact", "Intensity": 1.0, "Radius": 1.0, "DestroyOnContact": True},
                        PerceptionTarget=arrow_target(emissive=1.5 if light else 0.0),
                        Script={"Script": "Scripts/Arrow.lua"})
        bolt(doc, arrow, color, 4.0 if light else 1.5, light)
        doc.save(f"Prefabs/{name}.pprefab")

    # Gas arrow: bursts into a sleeping-gas cloud where it lands (Scripts/GasArrow.lua).
    doc = Document("PenumbraPrefab")
    arrow = doc.add("GasArrow", Transform=transform(), SphereCollider={"Radius": 0.06},
                    RigidBody={"Type": "Dynamic", "Mass": 0.1, "GravityFactor": 0.3, "ContinuousCollision": True,
                               "TransparentToSight": True},
                    PerceptionTarget=arrow_target(emissive=0.3),
                    Script={"Script": "Scripts/GasArrow.lua"})
    bolt(doc, arrow, (0.4, 0.9, 0.3), 1.5)
    doc.save("Prefabs/GasArrow.pprefab")
    # Moss arrow: a patch of moss where it lands on a floor (Scripts/MossArrow.lua).
    doc = Document("PenumbraPrefab")
    arrow = doc.add("MossArrow", Transform=transform(), SphereCollider={"Radius": 0.06},
                    RigidBody={"Type": "Dynamic", "Mass": 0.1, "GravityFactor": 0.3, "ContinuousCollision": True,
                               "TransparentToSight": True},
                    PerceptionTarget=arrow_target(),
                    Script={"Script": "Scripts/MossArrow.lua"})
    bolt(doc, arrow, (0.3, 0.5, 0.2), 0.8)
    doc.save("Prefabs/MossArrow.pprefab")
    # The patch: a 2 m disc of moss, 2 cm thick, footsteps on it near silent. Kinematic, so it does
    # not trigger a navmesh rebuild; transparent to sight like any floor covering.
    doc = Document("PenumbraPrefab")
    doc.add("MossPatch", Transform=transform(scale=(2.0, 0.02, 2.0)),
            Mesh={"Mesh": "builtin:cylinder"}, Material=material(SURFACE_COLORS["Moss"], roughness=1.0),
            BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]},
            RigidBody={"Type": "Kinematic", "TransparentToSight": True},
            Surface=SURFACES["Moss"])
    doc.save("Prefabs/MossPatch.pprefab")

    # The cloud: a continuous Gas stim for a few seconds (NPCs with a Gas -> Knockout rule fall
    # asleep in it), a little smoke for the eyes and a faint green haze for the player.
    doc = Document("PenumbraPrefab")
    cloud = doc.add("GasCloud", Transform=transform(),
                    StimSource={"Stim": "Gas", "Delivery": "Continuous", "Radius": 2.5, "Intensity": 1.0},
                    SmokeVolume={"Radius": 2.5, "Density": 0.3, "Lifetime": 6.0})
    doc.add("Haze", cloud, Transform=transform(scale=(4.5, 3.0, 4.5)), Mesh={"Mesh": "builtin:sphere", "CastShadows": False},
            Material=material((0.45, 0.75, 0.35, 0.18), emissive=0.15, emissive_color=(0.4, 0.8, 0.3), translucent=True))
    doc.save("Prefabs/GasCloud.pprefab")

    doc = Document("PenumbraPrefab")
    doc.add("PowderKeg",
            Transform=transform(scale=(0.7, 0.9, 0.7)),
            Mesh={"Mesh": "builtin:cylinder"}, Material=material((0.35, 0.2, 0.1)),
            BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]},
            StimResponse={"Rules": [{"Stim": "Fire", "Threshold": 0.5, "Once": True, "Actions": [
                {"Type": "EmitStim", "Asset": "Fire", "Value": 1.0, "Radius": 3.5},
                {"Type": "RadialImpulse", "Value": 40.0, "Radius": 5.0},
                {"Type": "EmitNoise", "Value": 10.0, "Asset": "Explosion"},
                {"Type": "PlaySound", "Asset": "Audio/Explosion.wav", "Value": 1.0},
                {"Type": "SpawnPrefab", "Asset": "Prefabs/Smoke.pprefab"},
                {"Type": "Destroy"}]}]})
    doc.save("Prefabs/PowderKeg.pprefab")

    for name, radius, density, lifetime in (("Smoke", 3.0, 0.9, 12.0), ("Steam", 1.2, 0.6, 3.0)):
        doc = Document("PenumbraPrefab")
        doc.add(name, Transform=transform(), SmokeVolume={"Radius": radius, "Density": density, "Lifetime": lifetime})
        doc.save(f"Prefabs/{name}.pprefab")


# --------------------------------------------------------------------------------------------
# Characters
# --------------------------------------------------------------------------------------------

# Skinned, animated characters (KayKit Adventurers, CC0; see Models/Characters/README.md). One file
# carries several weapons and accessories: each character hides the parts it does not use.
THIEF = ("Thief.glb", ["Knife", "Knife_Offhand", "2H_Crossbow", "Throwable"])
WATCHMAN = ("Knight.glb", ["Knight_Cape", "1H_Sword_Offhand", "2H_Sword", "Badge_Shield", "Rectangle_Shield", "Spike_Shield"])
KNIGHT = ("Knight.glb", ["1H_Sword_Offhand", "2H_Sword", "Round_Shield", "Rectangle_Shield", "Spike_Shield"])
# The cook: the knight's body without armour, weapons or cape.
COOK = ("Knight.glb", ["1H_Sword_Offhand", "1H_Sword", "2H_Sword", "Badge_Shield", "Round_Shield", "Rectangle_Shield", "Spike_Shield",
                       "Knight_Helmet", "Knight_Cape"])


def character_model(doc, parent, character):
    """The visible body: faces -Z like the entity (glTF models face +Z) and is about 1.8 m tall."""
    model, hidden = character
    doc.add("Model", parent, Transform=transform(rotation=(0, 180, 0), scale=(0.8, 0.8, 0.8)),
            Mesh={"Mesh": f"Models/Characters/{model}", "HiddenNodes": hidden},
            Animator={"Clip": "Idle", "BlendDuration": 0.2})


def add_player(doc, position, yaw=0.0):
    player = doc.add("Player",
                     Transform=transform(position, (0, yaw, 0)),
                     CharacterController={"Radius": 0.3, "Height": 1.8},
                     PerceptionTarget={"Reflectance": 0.25},
                     AudioListener={},
                     FogOfWar={},  # he sees a cone in front and a little circle around him (docs/FogOfWar.md)
                     Script={"Script": "Scripts/Player.lua"})
    character_model(doc, player, THIEF)
    return player


def add_guard(doc, name, position, yaw, waypoints, lantern=False, character=WATCHMAN, **properties):
    guard = doc.add(name,
                    Transform=transform(position, (0, yaw, 0)),
                    CharacterController={"Radius": 0.3, "Height": 1.8},
                    Perception={},
                    StimResponse=NPC_RESPONSES,
                    Ragdoll={},  # knocked out or killed, the body goes limp (physics)
                    Script={"Script": "Scripts/Guard.lua", "Properties": {"waypoints": ",".join(waypoints), **properties}})
    character_model(doc, guard, character)
    if lantern:
        doc.add("Lantern", guard, Transform=transform((0.25, 1.2, -0.3), (-15, 0, 0)),
                SpotLight={"Color": [1.0, 0.85, 0.6], "Intensity": 30.0, "Range": 14.0, "InnerAngle": 18.0, "OuterAngle": 32.0,
                           "CastShadows": True})
    return guard


# How NPCs take arrows and gas: sleeping gas knocks them out (Prefabs/GasCloud.pprefab); fire hurts
# (two fire arrows kill) and, like a water arrow, tells them they were shot at from where it came.
NPC_RESPONSES = {"Rules": [
    {"Stim": "Gas", "Threshold": 0.3, "Once": True, "Actions": [{"Type": "Knockout"}]},
    {"Stim": "Fire", "Threshold": 0.5, "Once": False, "Actions": [{"Type": "Damage", "Value": 75.0}, {"Type": "Alarm"}]},
    {"Stim": "Water", "Threshold": 0.5, "Once": False, "Actions": [{"Type": "Alarm"}]},
]}


GOLD, SILVER = (1.0, 0.78, 0.32), (0.8, 0.82, 0.85)


def add_loot(doc, name, position, size, label, value, color=GOLD, mesh="builtin:cylinder", parent=None, item="",
             pickpocket=False):
    """Something to steal (Scripts/Loot.lua). Precious metal catches a faint glint even in the dark.
    Loot on a belt (pickpocket) has no collider: the thief's hands find it on the NPC (Player.lua)."""
    components = {
        "Transform": transform(position, scale=size),
        "Mesh": {"Mesh": mesh, "CastShadows": False},
        "Material": material(color, roughness=0.3 if color != LEATHER else 0.9, metallic=0.0 if color == LEATHER else 1.0,
                             emissive=0.0 if color == LEATHER else 0.25, emissive_color=color),
        "Script": {"Script": "Scripts/Loot.lua", "Properties": {"label": label, "value": float(value), "item": item,
                                                                  "pickpocket": pickpocket}},
    }
    if not pickpocket:
        components["BoxCollider"] = {"HalfExtents": [0.5, 0.5, 0.5]}
    return doc.add(name, parent, **components)


LEATHER = (0.33, 0.2, 0.1)


def add_purse(doc, npc, value, side=1.0, name="Purse"):
    """A purse on an NPC's belt, at the hip behind him."""
    return add_loot(doc, name, (0.22 * side, 0.95, 0.12), (0.14, 0.16, 0.12), "purse", value, color=LEATHER,
                    mesh="builtin:sphere", parent=npc, pickpocket=True)


def add_servant(doc, name, position, yaw, waypoints, bell="Bell"):
    """A servant doing chores between waypoints; alarmed, he runs to the bell (Scripts/Servant.lua)."""
    servant = doc.add(name, Transform=transform(position, (0, yaw, 0)), CharacterController={"Radius": 0.3, "Height": 1.8},
                      Perception={"FieldOfView": 140.0}, StimResponse=NPC_RESPONSES, Ragdoll={},
                      Script={"Script": "Scripts/Servant.lua", "Properties": {"waypoints": ",".join(waypoints), "bell": bell}})
    character_model(doc, servant, COOK)
    return servant


def add_bell(doc, position, name="Bell"):
    """An alarm bell on a post: ringing it calls every guard (Scripts/Servant.lua)."""
    bell = doc.add(name, Transform=transform(position), BoxCollider={"HalfExtents": [0.15, 1.2, 0.15], "Offset": [0, 1.2, 0]})
    doc.add("Post", bell, Transform=transform((0, 1.2, 0), scale=(0.15, 2.4, 0.15)), Mesh={"Mesh": "builtin:cube"},
            Material=material((0.3, 0.2, 0.12)))
    doc.add("BellBody", bell, Transform=transform((0, 2.3, 0.2), scale=(0.35, 0.4, 0.35)), Mesh={"Mesh": "builtin:cylinder"},
            Material=material((0.6, 0.45, 0.15), roughness=0.3, metallic=0.9))
    return bell


def add_door(doc, name, hinge, yaw, width=1.2, height=2.2, label="door", locked=False, key="", key_label="key", pins=6):
    """A hinged door (Scripts/World/Door.lua) standing at its hinge; the closed panel runs along the
    entity's +X. The panel is kinematic (the navmesh runs through the doorway) and lets sound
    through muffled (Surface.SoundLoss); the Lock child is where the lockpicking bar hangs."""
    door = doc.add(name, Transform=transform(hinge, rotation=(0, yaw, 0)),
                   Script={"Script": "Scripts/World/Door.lua", "Properties": {
                       "label": label, "width": width, "locked": locked, "key": key, "key_label": key_label, "pins": pins}})
    doc.add("Panel", door, Transform=transform((width / 2, height / 2, 0), scale=(width - 0.02, height, 0.08)),
            Mesh={"Mesh": "builtin:cube"}, Material=material((0.26, 0.17, 0.1), roughness=0.9),
            BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]}, RigidBody={"Type": "Kinematic"},
            Surface={"Material": "Wood", "NoiseModifier": 0.5, "SoundLoss": 2.5})
    doc.add("Lock", door, Transform=transform((width - 0.15, 1.0, 0), scale=(0.06, 0.1, 0.12)),
            Mesh={"Mesh": "builtin:cube", "CastShadows": False}, Material=material((0.45, 0.4, 0.3), roughness=0.4, metallic=0.8))
    return door


def add_light_switch(doc, name, position, yaw, lamps, label):
    """A wall switch for some lamps; its forward (-Z) side faces the room it is used from."""
    switch = doc.add(name, Transform=transform(position, rotation=(0, yaw, 0)),
                     Script={"Script": "Scripts/World/LightSwitch.lua", "Properties": {"lamps": ",".join(lamps), "label": label}})
    doc.add("Plate", switch, Transform=transform(scale=(0.14, 0.2, 0.03)), Mesh={"Mesh": "builtin:cube"},
            Material=material((0.55, 0.45, 0.3), roughness=0.4, metallic=0.6), BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]})
    # The Lever pivots at the plate's centre (the script tips it about X); its shape is a child so
    # rotating it does not skew the mesh.
    lever = doc.add("Lever", switch, Transform=transform((0, 0, -0.02)))
    doc.add("Handle", lever, Transform=transform((0, 0, -0.05), scale=(0.035, 0.035, 0.1)),
            Mesh={"Mesh": "builtin:cube", "CastShadows": False}, Material=material((0.15, 0.12, 0.1)))
    return switch


def add_cook(doc, name, position, yaw, zone_center, radius=14.0):
    """A cook busy at his station; the kitchen event zone around him makes the noises of his work
    (Scripts/World/EventZone.lua, odds in Scripts/World/Events.lua)."""
    cook = doc.add(name, Transform=transform(position, (0, yaw, 0)),
                   CapsuleCollider={"Radius": 0.3, "HalfHeight": 0.6, "Offset": [0, 0.9, 0]})
    character_model(doc, cook, COOK)
    doc.add("KitchenZone", Transform=transform(zone_center),
            Script={"Script": "Scripts/World/EventZone.lua", "Properties": {"set": "Kitchen", "radius": radius, "actor": name}})
    return cook


def add_world_events(doc, windows=(), dogs_position=(0, 1, 30)):
    """Storm and dogs (Scripts/World/WorldEvents.lua): a directional light for the lightning flashes
    (off between them) and the rain loop."""
    doc.add("Lightning", Transform=transform(rotation=(-60, 30, 0)),
            DirectionalLight={"Color": [0.75, 0.82, 1.0], "Intensity": 0.0, "CastShadows": True, "Enabled": False})
    doc.add("Rain", Transform=transform(), AudioSource={"Clip": "Audio/Rain.wav", "Loop": True, "Spatial": False, "Volume": 0.35})
    doc.add("World", Transform=transform(),
            Script={"Script": "Scripts/World/WorldEvents.lua",
                    "Properties": {"windows": ",".join(windows), "dogs_position": list(dogs_position)}})


def add_environment(doc, ambient=0.006, map_intensity=0.12, fog=0.01, moon=0.08, fog_scattering=None):
    environment = {"EnvironmentMap": "Environment/NightSky_1k.hdr", "EnvironmentIntensity": map_intensity,
                   "SkyboxIntensity": 0.6, "AmbientColor": [ambient, ambient, ambient * 1.4], "Exposure": 1.4,
                   "FogDensity": fog, "FogColor": [0.02, 0.025, 0.035], "SSAOEnabled": True, "BloomIntensity": 0.05}
    if fog_scattering is not None:
        environment["FogScattering"] = fog_scattering  # how much lamp light the fog sends to the eye (docs/Fog.md)
    doc.add("Environment", Transform=transform(), Environment=environment)
    doc.add("Moon", Transform=transform(rotation=(-35, 140, 0)),
            DirectionalLight={"Color": [0.55, 0.65, 1.0], "Intensity": moon, "CastShadows": True, "ShadowSoftness": 1.5})


# --------------------------------------------------------------------------------------------
# Levels
# --------------------------------------------------------------------------------------------

def build_stealth_level():
    """Night courtyard: dark corners, a brightly lit hall whose doorway silhouettes intruders,
    torches to douse, powder kegs by the gate and two patrolling guards."""
    doc = Document("PenumbraScene")
    add_environment(doc)
    doc.add("Camera", Transform=transform((0, 3, 8)), Camera={"VerticalFOV": 65.0, "NearClip": 0.05, "FarClip": 300.0})

    stone, dark_stone, wood = (0.32, 0.31, 0.3), (0.18, 0.18, 0.19), (0.3, 0.2, 0.12)
    box(doc, "Ground", (0, -0.5, 0), (40, 1, 40), (0.22, 0.22, 0.21))
    # Outer walls
    for name, position, size in (("WallNorth", (0, 2.5, -20), (40, 5, 1)), ("WallSouth", (0, 2.5, 20), (40, 5, 1)),
                                 ("WallEast", (20, 2.5, 0), (1, 5, 40)), ("WallWest", (-20, 2.5, 0), (1, 5, 40))):
        box(doc, name, position, size, dark_stone)

    # The great hall (north-east): bright interior, open doorway facing the courtyard.
    box(doc, "HallBack", (10, 3, -16), (12, 6, 0.6), stone)
    box(doc, "HallSideWest", (4.3, 3, -12), (0.6, 6, 8), stone)
    box(doc, "HallSideEast", (15.7, 3, -12), (0.6, 6, 8), stone)
    box(doc, "HallFrontLeft", (6.5, 3, -8), (4.6, 6, 0.6), stone)
    box(doc, "HallFrontRight", (13.5, 3, -8), (4.6, 6, 0.6), stone)
    box(doc, "HallLintel", (10, 5, -8), (2.4, 2, 0.6), stone)
    box(doc, "HallRoof", (10, 6.2, -12), (12, 0.4, 8.6), dark_stone)
    for i, x in enumerate((7, 10, 13)):
        lamp = doc.add(f"HallLight{i}", Transform=transform((x, 4.5, -12)),
                       PointLight={"Color": [1.0, 0.85, 0.65], "Intensity": 60.0, "Range": 12.0, "CastShadows": True})
        doc.add("Lantern", lamp, Transform=transform((0, 0.3, 0), scale=(0.3, 0.4, 0.3)),
                Mesh={"Mesh": "builtin:cube", "CastShadows": False},
                Material=material((1.0, 0.9, 0.7), emissive=6.0, emissive_color=(1.0, 0.85, 0.6)))
    # The switch for the hall lamps, just inside the doorway (Scripts/World/LightSwitch.lua).
    add_light_switch(doc, "HallSwitch", (8.3, 1.3, -8.33), 0, ["HallLight0", "HallLight1", "HallLight2"], "hall lamps")
    box(doc, "HallTapestry", (10, 3, -15.6), (6, 3.5, 0.1), (0.7, 0.55, 0.35), collider=False)

    # A lit window on the west wall: a second silhouette trap.
    box(doc, "Window", (-19.4, 2.5, -6), (0.1, 2.0, 1.6), (0.9, 0.8, 0.6), emissive=3.0, emissive_color=(1.0, 0.8, 0.5), collider=False)

    # Cover: crates and a cart.
    random.seed(7)
    for i, (x, z) in enumerate(((-8, 4), (-7, 5.2), (2, -2), (3.2, -2.4), (-12, -10), (12, 6), (-3, 12), (6, 13))):
        box(doc, f"Crate{i}", (x, 0.5, z), (1, 1, 1), wood, roughness=0.9)
    box(doc, "Cart", (-2, 0.6, -6), (3, 1.2, 1.6), wood)

    # Torches along the walls. Every lamp that can reach past a wall casts shadows, or it would light the rooms
    # behind it (the renderer gives the shadow maps to the lamps reaching the view; the guards always see walls).
    for x, z in ((-18.8, 6), (-6, -18.8), (18.8, 10), (0, 18.8), (8, -7.2)):
        torch_prefab_entities(doc, (x, 0, z), shadows=True)

    # Powder kegs stacked by the gate guardhouse.
    for i, (x, z) in enumerate(((15, 15), (16, 15.6), (15.4, 16.6))):
        doc.add("PowderKeg", Transform=transform((x, 0.45, z), scale=(0.7, 0.9, 0.7)),
                Mesh={"Mesh": "builtin:cylinder"}, Material=material((0.35, 0.2, 0.1)),
                BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]},
                StimResponse={"Rules": [{"Stim": "Fire", "Threshold": 0.5, "Once": True, "Actions": [
                    {"Type": "EmitStim", "Asset": "Fire", "Value": 1.0, "Radius": 3.5},
                    {"Type": "RadialImpulse", "Value": 40.0, "Radius": 5.0},
                    {"Type": "EmitNoise", "Value": 10.0, "Asset": "Explosion"},
                    {"Type": "PlaySound", "Asset": "Audio/Explosion.wav", "Value": 1.0},
                    {"Type": "SpawnPrefab", "Asset": "Prefabs/Smoke.pprefab"},
                    {"Type": "Destroy"}]}]})

    # The cellar (north-west, against the outer wall): a dark storeroom behind a locked door. The
    # captain's key opens it; without it the lock can be picked. GuardA looks in on his round.
    box(doc, "CellarWest", (-13, 1.5, -16.75), (0.4, 3, 5.5), stone)
    box(doc, "CellarEast", (-7.5, 1.5, -16.75), (0.4, 3, 5.5), stone)
    box(doc, "CellarFrontLeft", (-12.025, 1.5, -14), (2.35, 3, 0.4), stone)
    box(doc, "CellarFrontRight", (-8.475, 1.5, -14), (2.35, 3, 0.4), stone)
    box(doc, "CellarLintel", (-10.25, 2.6, -14), (1.2, 0.8, 0.4), stone)
    box(doc, "CellarRoof", (-10.25, 3.1, -16.75), (6.1, 0.2, 5.9), dark_stone)
    add_door(doc, "CellarDoor", (-10.85, 0, -14), 0, label="cellar door", locked=True, key="CellarKey", key_label="cellar key")

    # Patrol waypoints and guards.
    for name, position in (("WP_A1", (-10, 0, -4)), ("WP_A2", (8, 0, -5)), ("WP_A3", (8, 0, 4)), ("WP_A4", (-10, 0, 6)),
                           ("WP_A5", (-10.25, 0, -17.5)), ("WP_B1", (14, 0, 12)), ("WP_B2", (14, 0, -4))):
        doc.add(name, Transform=transform(position))
    guard_a = add_guard(doc, "GuardA", (-10, 0.05, -4), 90, ["WP_A1", "WP_A2", "WP_A3", "WP_A4", "WP_A5"])
    guard_b = add_guard(doc, "GuardB", (14, 0.05, 12), 180, ["WP_B1", "WP_B2"], lantern=True, character=KNIGHT)

    # Floors that change how loud footsteps are, and a bucket waiting to be knocked off a crate.
    floor_patch(doc, "HallCarpet", "Carpet", (10, -12), (10.6, 7.2))
    floor_patch(doc, "Grate", "Metal", (0, 5), (3, 3))
    floor_patch(doc, "Lawn", "Grass", (-12, 11), (6, 5))
    noisy_object(doc, "Bucket", (-8, 1.2, 4), (0.3, 0.4, 0.3), "Metal", 9.0, "Audio/Clang.wav", (0.45, 0.45, 0.48))
    doc.add("Navigation", Transform=transform(), NavMesh={})

    # Life in the house: the cook at work in the hall, a storm now and then, dogs beyond the walls.
    add_cook(doc, "Cook", (13, 0.05, -14.6), 180, (10, 0, -12))
    for name, position in (("WP_S1", (6, 0, -10)), ("WP_S2", (14, 0, -10)), ("WP_S3", (10, 0, -5))):
        doc.add(name, Transform=transform(position))
    add_servant(doc, "Servant", (6, 0.05, -10), 90, ["WP_S1", "WP_S2", "WP_S3"])
    add_bell(doc, (11, 0, 16.5))
    add_world_events(doc, windows=("Window",), dogs_position=(0, 1, 28))

    # The exit gate (south-east corner) and the player start (dark north-west corner).
    doc.add("Gate", Transform=transform((17, 1.5, 18)), BoxCollider={"HalfExtents": [1.5, 1.5, 1.0]},
            RigidBody={"Type": "Static", "IsTrigger": True}, Script={"Script": "Scripts/Goal.lua"})
    box(doc, "GateArch", (17, 4.2, 19.2), (4, 0.6, 0.6), stone)
    add_player(doc, (-17, 0.05, -17), yaw=-135)
    doc.add("Ambience", Transform=transform((0, 3, 0)),
            AudioSource={"Clip": "Audio/Wind.wav", "Loop": True, "PlayOnStart": True, "Spatial": False, "Volume": 0.25})

    # The mission (Scripts/Missions/Manor.lua): silver in the hall, odds and ends in the courtyard,
    # purses on the guards' belts and the cellar key on the captain's (GuardB).
    doc.add("Mission", Transform=transform(), Script={"Script": "Scripts/MissionControl.lua",
                                                      "Properties": {"mission": "Scripts/Missions/Manor"}})
    box(doc, "HallTable", (10, 0.45, -13), (3, 0.9, 1.2), wood, surface="Wood")
    add_loot(doc, "GoldGoblet", (9.2, 1.02, -13), (0.12, 0.24, 0.12), "gold goblet", 150)
    add_loot(doc, "SilverPlate", (10.2, 0.915, -13.1), (0.4, 0.03, 0.4), "silver plate", 75, color=SILVER)
    add_loot(doc, "Candlestick", (11.0, 1.1, -12.8), (0.08, 0.4, 0.08), "candlestick", 100, color=SILVER)
    box(doc, "Chest", (7, 0.35, -15.2), (1.0, 0.7, 0.6), wood, surface="Wood")
    add_loot(doc, "JewelBox", (7, 0.8, -15.2), (0.4, 0.2, 0.3), "jewel box", 200, mesh="builtin:cube")
    add_loot(doc, "Coins", (-12, 1.04, -10), (0.2, 0.08, 0.2), "stack of coins", 50)
    add_loot(doc, "SilverRing", (-2, 1.22, -6), (0.08, 0.03, 0.08), "silver ring", 50, color=SILVER)
    add_loot(doc, "Emerald", (12, 1.06, 6), (0.1, 0.12, 0.1), "emerald", 125, color=(0.2, 0.85, 0.45), mesh="builtin:sphere")
    # In the cellar: the lord's wine and a strongbox.
    box(doc, "CellarShelf", (-12.3, 0.5, -18.5), (1.0, 1.0, 1.4), wood, surface="Wood")
    add_loot(doc, "Wine", (-12.3, 1.15, -18.2), (0.09, 0.3, 0.09), "bottle of old wine", 75, color=(0.25, 0.05, 0.08))
    add_loot(doc, "Strongbox", (-8.4, 0.2, -18.6), (0.5, 0.4, 0.35), "strongbox", 175, mesh="builtin:cube", color=(0.3, 0.3, 0.32))
    add_purse(doc, guard_a, 75)
    add_purse(doc, guard_b, 100)
    add_loot(doc, "CellarKey", (-0.22, 0.95, 0.12), (0.04, 0.14, 0.02), "cellar key", 0, color=(0.55, 0.5, 0.4),
             mesh="builtin:cube", parent=guard_b, item="CellarKey", pickpocket=True)
    doc.save("Scenes/Stealth.pscene")


def build_menu():
    """Title screen: the briefing and the choice of difficulty (Scripts/Menu.lua) over a dim wall
    lit by a torch."""
    doc = Document("PenumbraScene")
    add_environment(doc, ambient=0.01, map_intensity=0.05, fog=0.0, moon=0.02)
    doc.add("Camera", Transform=transform((0, 1.6, 4)), Camera={"VerticalFOV": 60.0})
    box(doc, "Ground", (0, -0.5, 0), (20, 1, 20), (0.2, 0.19, 0.18))
    box(doc, "Wall", (0, 3, -2), (20, 6, 0.6), (0.3, 0.29, 0.28))
    torch_prefab_entities(doc, (2.5, 0, -1.5))
    doc.add("Menu", Transform=transform(), Script={"Script": "Scripts/Menu.lua", "Properties": {"mission": "Scripts/Missions/Manor"}})
    doc.save("Scenes/Menu.pscene")


def campfire(doc, position):
    """Ring of stones, a few logs, a big flame and a strong flickering shadow-casting light."""
    fire = doc.add("Campfire", Transform=transform(position),
                   StimResponse={"Rules": [
                       {"Stim": "Water", "Threshold": 0.3, "Once": False, "Actions": [
                           {"Type": "DisableLights"}, {"Type": "PlaySound", "Asset": "Audio/Hiss.wav", "Value": 0.8},
                           {"Type": "SpawnPrefab", "Asset": "Prefabs/Steam.pprefab"}]},
                       {"Stim": "Fire", "Threshold": 0.3, "Once": False, "Actions": [{"Type": "EnableLights"}]}]})
    for i in range(8):
        angle = i / 8 * 6.283
        doc.add(f"Stone{i}", fire, Transform=transform((0.55 * math.cos(angle), 0.08, 0.55 * math.sin(angle)), scale=(0.22, 0.16, 0.2)),
                Mesh={"Mesh": "builtin:sphere"}, Material=material((0.25, 0.24, 0.23)))
    for i, yaw in enumerate((0, 60, 120)):
        doc.add(f"Log{i}", fire, Transform=transform((0, 0.12, 0), (0, yaw, 80), scale=(0.12, 0.8, 0.12)),
                Mesh={"Mesh": "builtin:cylinder", "CastShadows": False}, Material=material((0.12, 0.07, 0.04), roughness=1.0))
    doc.add("Flame", fire, Transform=transform((0, 0.15, 0)),
            Flame={"Height": 1.1, "Radius": 0.32, "Intensity": 8.0, "Speed": 0.9},
            PointLight={"Color": [1.0, 0.55, 0.25], "Intensity": 45.0, "Range": 14.0, "Flicker": 0.2, "CastShadows": True},
            StimSource={"Stim": "Fire", "Delivery": "Continuous", "Radius": 0.8, "Intensity": 1.0},
            SphereCollider={"Radius": 0.9}, RigidBody={"Type": "Static", "IsTrigger": True})
    return fire


# Where to stand in PerceptionLab to try the fog glow (x, z); PenumbraTests/src/LevelTests.cpp checks them.
FOG_SPOT_EAST = (6.0, -1.0)
FOG_SPOT_WEST = (-7.0, 0.4)


def build_perception_lab():
    """One guard, one campfire, one player: the place to tune and understand NPC vision.
    A small walled yard so firelight falls on walls as well as the floor; a polished stone
    patch near the fire, a dark corner, pillars for cover, a lit doorway behind the guard's
    field of view for silhouette tests, and two patches of lit mist for the fog's glow."""
    doc = Document("PenumbraScene")
    add_environment(doc, ambient=0.004, map_intensity=0.08, fog=0.0, moon=0.04)
    doc.add("Camera", Transform=transform((0, 3, 8)), Camera={"VerticalFOV": 65.0})

    stone = (0.3, 0.29, 0.28)
    box(doc, "Ground", (0, -0.5, 0), (24, 1, 20), (0.2, 0.19, 0.18))
    for name, position, size in (("WallNorth", (0, 2, -10), (24, 4, 0.6)), ("WallSouth", (0, 2, 10), (24, 4, 0.6)),
                                 ("WallEast", (12, 2, 0), (0.6, 4, 20)), ("WallWest", (-12, 2, 0), (0.6, 4, 20))):
        box(doc, name, position, size, stone)
    # Polished stone (marble) floor patch around the fire: bright when lit.
    box(doc, "Marble", (0, 0.01, 2), (5, 0.02, 4), (0.85, 0.83, 0.8), collider=False, roughness=0.25)
    # Pillars for cover.
    for i, (x, z) in enumerate(((-4, -2), (4, -2), (-6, 5), (6, 5))):
        box(doc, f"Pillar{i}", (x, 1.5, z), (0.7, 3, 0.7), stone)
    # A lit doorway in the west wall: a bright opening to silhouette the player against.
    box(doc, "Doorway", (-11.6, 1.4, 6), (0.2, 2.8, 1.6), (0.9, 0.75, 0.5), emissive=2.5, emissive_color=(1.0, 0.75, 0.45), collider=False)

    campfire(doc, (0, 0, 2))
    torch_prefab_entities(doc, (-11.2, 0, -6))
    torch_prefab_entities(doc, (11.2, 0, -6))

    # Fog glow (docs/Perception.md, "Fog"): two places to try it, both in the guard's view (clear of the pillars).
    # East: a bank of mist with a cold lamp in it, out of reach of the walls. Stand at FOG_SPOT_EAST, in front of
    # the bank: a dark figure against glowing fog, seen sooner than when fog only dimmed him.
    doc.add("FogBankEast", Transform=transform((8.6, 1.2, 2.0)),
            FogVolume={"HalfExtents": [1.4, 1.4, 1.4], "Density": 0.3, "EdgeFade": 0.6})
    doc.add("FogLampBehind", Transform=transform((8.8, 1.5, 2.2)),
            PointLight={"Color": [0.55, 0.8, 1.0], "Intensity": 12.0, "Range": 2.2, "Flicker": 0.0, "CastShadows": False})
    # West: a puff of mist with a warm lamp in it, between the guard and the lit doorway. Stand at FOG_SPOT_WEST,
    # against the doorway: the glow in between hides what the doorway would give away (switch the lamp off to see).
    doc.add("FogPuffWest", Transform=transform((-3.5, 1.2, -3.8)),
            FogVolume={"HalfExtents": [1.2, 1.4, 1.2], "Density": 0.06, "EdgeFade": 0.6})
    doc.add("FogLampBetween", Transform=transform((-3.5, 1.5, -3.8)),
            PointLight={"Color": [1.0, 0.6, 0.3], "Intensity": 10.0, "Range": 2.0, "Flicker": 0.0, "CastShadows": False})

    guard = doc.add("Guard", Transform=transform((0, 0.05, -8), (0, 180, 0)),  # facing the yard
                    CharacterController={"Radius": 0.3, "Height": 1.8}, Perception={},
                    Script={"Script": "Scripts/Sentry.lua"})
    character_model(doc, guard, WATCHMAN)
    add_player(doc, (-9, 0.05, 8), yaw=-135)
    doc.add("Debug", Transform=transform(), Script={"Script": "Scripts/PerceptionDebug.lua"})
    doc.save("Scenes/PerceptionLab.pscene")


def build_ai_lab():
    """Guard behaviour lab: a patrolling guard, two rooms joined by doorways (sound goes around
    corners, guards walk the navmesh), floors of every material and noisy objects on a table.
    The player's noise probe (- / = and G) makes test noises of any loudness; the HUD shows the
    guard's reaction, what caused it and how long it will last."""
    doc = Document("PenumbraScene")
    add_environment(doc, ambient=0.006, map_intensity=0.1, fog=0.0, moon=0.06)
    doc.add("Camera", Transform=transform((0, 3, 8)), Camera={"VerticalFOV": 65.0})
    doc.add("Navigation", Transform=transform(), NavMesh={})

    stone, dark = (0.3, 0.29, 0.28), (0.2, 0.2, 0.21)
    box(doc, "Ground", (0, -0.5, 0), (30, 1, 24), (0.22, 0.21, 0.2), surface="Stone")
    for name, position, size in (("WallNorth", (0, 2, -12), (30, 4, 0.6)), ("WallSouth", (0, 2, 12), (30, 4, 0.6)),
                                 ("WallEast", (15, 2, 0), (0.6, 4, 24)), ("WallWest", (-15, 2, 0), (0.6, 4, 24))):
        box(doc, name, position, size, dark)

    # The house: two rooms (carpet west, wooden floor east) with doors to the yard and between them.
    for name, position, size in (("HouseFrontWest", (-9.5, 2, -2), (5, 4, 0.4)), ("HouseFrontMiddle", (0, 2, -2), (10, 4, 0.4)),
                                 ("HouseFrontEast", (9.5, 2, -2), (5, 4, 0.4)), ("HouseSideWest", (-12, 2, -7), (0.4, 4, 10)),
                                 ("HouseSideEast", (12, 2, -7), (0.4, 4, 10)), ("PartitionNorth", (0, 2, -10), (0.4, 4, 4)),
                                 ("PartitionSouth", (0, 2, -4), (0.4, 4, 4))):
        box(doc, name, position, size, stone)
    floor_patch(doc, "WestRoomCarpet", "Carpet", (-6, -7), (11.6, 9.6))
    floor_patch(doc, "EastRoomFloor", "Wood", (6, -7), (11.6, 9.6))

    # Yard floors: a metal grate, a lawn and a moss patch next to plain cobbles.
    floor_patch(doc, "Grate", "Metal", (-8, 6), (4, 4))
    floor_patch(doc, "Lawn", "Grass", (8, 6), (6, 6))
    floor_patch(doc, "Moss", "Moss", (0, 8), (3, 3))

    # A window in the west room's back wall glows with every lightning flash.
    box(doc, "Window", (-6, 2.2, -11.65), (1.6, 1.4, 0.1), (0.6, 0.65, 0.8), emissive=0.2, emissive_color=(0.7, 0.8, 1.0),
        collider=False)
    # The east room is a kitchen: the cook works at the table.
    add_cook(doc, "Cook", (8.3, 0.05, -6), 90, (6, 0, -7))  # at the table, facing it
    add_world_events(doc, windows=("Window",), dogs_position=(0, 1, 20))

    # A table in the east room with a metal bucket and a wooden box to knock off.
    box(doc, "Table", (6, 0.45, -6), (2.0, 0.9, 1.0), (0.3, 0.2, 0.12), surface="Wood")
    noisy_object(doc, "Bucket", (6.6, 1.12, -6), (0.3, 0.4, 0.3), "Metal", 9.0, "Audio/Clang.wav", (0.45, 0.45, 0.48))
    noisy_object(doc, "WoodenBox", (5.4, 1.07, -6), (0.3, 0.3, 0.3), "Wood", 5.0, "Audio/Thud.wav", (0.4, 0.28, 0.15))
    for i, (x, z) in enumerate(((-4, 4), (4, 1), (-11, -10))):
        box(doc, f"Crate{i}", (x, 0.5, z), (1, 1, 1), (0.3, 0.2, 0.12), surface="Wood")

    # Light: torches by the doors and in the east room; the west room stays dark.
    for x, z in ((-4.4, -1.3), (4.4, -1.3), (11.3, -9)):  # beside the doors, not in them
        torch_prefab_entities(doc, (x, 0, z))

    for name, position in (("WP_1", (-10, 0, 8)), ("WP_2", (10, 0, 8)), ("WP_3", (6, 0, -4)), ("WP_4", (-6, 0, -9))):
        doc.add(name, Transform=transform(position))
    add_guard(doc, "Guard", (-10, 0.05, 8), 90, ["WP_1", "WP_2", "WP_3", "WP_4"], catch_scene="Scenes/AILab.pscene")
    # A second guard walks the yard (shouts carry between them), a servant tidies the west room.
    for name, position in (("WP_5", (12, 0, 10)), ("WP_6", (12, 0, 1)), ("WP_7", (-2, 0, 1)),
                           ("WP_S1", (-9, 0, -5)), ("WP_S2", (-3, 0, -9))):
        doc.add(name, Transform=transform(position))
    add_guard(doc, "Guard2", (12, 0.05, 10), 180, ["WP_5", "WP_6", "WP_7"], catch_scene="Scenes/AILab.pscene", character=KNIGHT)
    add_servant(doc, "Servant", (-9, 0.05, -5), 90, ["WP_S1", "WP_S2"])
    add_bell(doc, (-13.5, 0, 0))
    add_player(doc, (-13, 0.05, 10), yaw=-135)
    doc.add("Debug", Transform=transform(), Script={"Script": "Scripts/PerceptionDebug.lua"})
    doc.save("Scenes/AILab.pscene")


def build_feature_test():
    doc = Document("PenumbraScene")
    add_environment(doc, ambient=0.02, map_intensity=0.2, fog=0.0)
    doc.add("Camera", Transform=transform((6, 4, 10), (-15, 30, 0)), Camera={})
    box(doc, "Ground", (0, -0.5, 0), (40, 1, 40), (0.3, 0.3, 0.3))
    box(doc, "Doorway", (0, 3, -9), (12, 6, 0.4), (0.3, 0.3, 0.3), emissive=2.0)
    box(doc, "Curtain", (0, 2, -3), (6, 4, 0.2), (0.3, 0.3, 0.3))
    guard = doc.add("Guard", Transform=transform((0, 0.05, 0)), CharacterController={}, Perception={}, Ragdoll={})
    character_model(doc, guard, WATCHMAN)
    player = doc.add("Player", Transform=transform((0, 0.05, -6)), CharacterController={}, PerceptionTarget={})
    character_model(doc, player, THIEF)
    doc.add("Crate", Transform=transform((4, 0.5, 8)), Mesh={"Mesh": "builtin:cube"}, Material=material((0.4, 0.3, 0.2)),
            BoxCollider={}, RigidBody={"Type": "Dynamic"})
    doc.add("Bouncer", Transform=transform((-4, 3, 8), scale=(0.5, 0.5, 0.5)), Mesh={"Mesh": "builtin:sphere"},
            SphereCollider={}, RigidBody={"Type": "Dynamic", "Restitution": 0.5}, Script={"Script": "Scripts/Tests/Bouncer.lua"})
    doc.add("Speaker", Transform=transform((2, 1, 2)), AudioSource={"Clip": "Audio/Wind.wav", "Loop": True})
    noisy_object(doc, "Bucket", (-3, 0.2, 1), (0.3, 0.4, 0.3), "Metal", 9.0, "", (0.45, 0.45, 0.48))
    torch_prefab_entities(doc, (5, 0, -2))
    doc.add("PowderKeg", Transform=transform((10, 0.45, -5), scale=(0.7, 0.9, 0.7)), Mesh={"Mesh": "builtin:cylinder"},
            BoxCollider={}, StimResponse={"Rules": [{"Stim": "Fire", "Threshold": 0.5, "Once": True, "Actions": [
                {"Type": "EmitStim", "Asset": "Fire", "Value": 1.0, "Radius": 3.0},
                {"Type": "PlaySound", "Asset": "Audio/Explosion.wav", "Value": 1.0},
                {"Type": "ScriptEvent", "Asset": "boom"},
                {"Type": "Destroy"}]}]})
    doc.add("Runner", Transform=transform((20, 0, 20)),
            Script={"Script": "Scripts/Tests/FeatureTest.lua", "Properties": {"factor": 3.0}})
    doc.save("Scenes/FeatureTest.pscene")


# --------------------------------------------------------------------------------------------
# Procedural audio (22.05 kHz mono)
# --------------------------------------------------------------------------------------------

RATE = 22050


def write_wav(name, samples):
    path = ASSETS / "Audio" / name
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32000)) for s in samples))


def noise(seed):
    generator = random.Random(seed)
    return lambda: generator.uniform(-1.0, 1.0)


def build_audio():
    n = noise(1)
    write_wav("Footstep.wav", [n() * math.exp(-i / (RATE * 0.025)) * 0.8 for i in range(int(RATE * 0.15))])
    n = noise(2)
    write_wav("Bow.wav", [(n() * 0.4 + math.sin(i * 0.05 * (1 - i / (RATE * 0.3)))) * math.exp(-i / (RATE * 0.08)) * 0.6
                          for i in range(int(RATE * 0.3))])
    n = noise(3)
    write_wav("Explosion.wav", [n() * math.exp(-i / (RATE * 0.5)) * (1.0 if i > 200 else i / 200) for i in range(int(RATE * 2.0))])
    n = noise(4)
    write_wav("Hiss.wav", [n() * 0.5 * math.sin(math.pi * i / (RATE * 0.8)) for i in range(int(RATE * 0.8))])
    write_wav("Alert.wav", [0.5 * math.sin(2 * math.pi * (600 + 300 * i / (RATE * 0.25)) * i / RATE) * math.exp(-i / (RATE * 0.2))
                            for i in range(int(RATE * 0.25))])
    write_wav("Alarm.wav", [0.5 * math.sin(2 * math.pi * (500 if (i // (RATE // 8)) % 2 else 700) * i / RATE) for i in range(RATE)])
    write_wav("Win.wav", [0.4 * math.sin(2 * math.pi * f * i / RATE) * math.exp(-(i % (RATE // 4)) / (RATE * 0.15))
                          for f, i in ((392 if j < RATE // 4 else 523 if j < RATE // 2 else 659, j) for j in range(int(RATE * 0.75)))])
    # Metal: a few inharmonic partials ringing out after a noisy strike (bucket, grate).
    n = noise(6)
    write_wav("Clang.wav", [(0.35 * n() * math.exp(-i / (RATE * 0.01)) +
                             sum(a * math.sin(2 * math.pi * f * i / RATE) for f, a in ((523, 0.3), (1131, 0.22), (1873, 0.15), (2647, 0.1)))
                             * math.exp(-i / (RATE * 0.35))) * 0.8 for i in range(int(RATE * 1.2))])
    n = noise(7)
    write_wav("Footstep_Metal.wav", [(n() * math.exp(-i / (RATE * 0.012)) * 0.5 +
                                      (0.25 * math.sin(2 * math.pi * 910 * i / RATE) + 0.15 * math.sin(2 * math.pi * 1530 * i / RATE))
                                      * math.exp(-i / (RATE * 0.08))) for i in range(int(RATE * 0.3))])
    n = noise(8)
    write_wav("Footstep_Wood.wav", [(n() * 0.4 * math.exp(-i / (RATE * 0.015)) + 0.5 * math.sin(2 * math.pi * 180 * i / RATE)
                                     * math.exp(-i / (RATE * 0.04))) for i in range(int(RATE * 0.15))])
    n = noise(9)
    value, samples = 0.0, []
    for i in range(int(RATE * 0.12)):
        value += (n() - value) * 0.08  # low-passed: soft floors only thump
        samples.append(value * 2.0 * math.exp(-i / (RATE * 0.03)))
    write_wav("Footstep_Soft.wav", samples)
    n = noise(10)
    write_wav("Thud.wav", [(n() * 0.3 * math.exp(-i / (RATE * 0.02)) + 0.7 * math.sin(2 * math.pi * 95 * i / RATE)
                            * math.exp(-i / (RATE * 0.09))) for i in range(int(RATE * 0.35))])

    # A knife on stone: bright, short partials.
    n = noise(11)
    write_wav("Knife.wav", [(0.3 * n() * math.exp(-i / (RATE * 0.004)) +
                             sum(a * math.sin(2 * math.pi * f * i / RATE) for f, a in ((2310, 0.3), (3470, 0.2), (5120, 0.12)))
                             * math.exp(-i / (RATE * 0.06))) for i in range(int(RATE * 0.4))])
    # Thunder: a sharp crack, then a long low rumble that swells and fades.
    n = noise(12)
    value, samples = 0.0, []
    length = int(RATE * 4.5)
    for i in range(length):
        value += (n() - value) * 0.015
        t = i / RATE
        rumble = value * 4.0 * min(t / 0.4, 1.0) * math.exp(-t / 1.6) * (0.7 + 0.3 * math.sin(2 * math.pi * 1.3 * t))
        samples.append(rumble + n() * 0.6 * math.exp(-t / 0.05))
    write_wav("Thunder.wav", samples)
    # Rain: dense soft noise with sparse droplets; loops seamlessly.
    n = noise(13)
    drops = random.Random(14)
    value, samples = 0.0, []
    length = RATE * 4
    for i in range(length):
        value += (n() - value) * 0.25
        drop = 0.4 * n() if drops.random() < 0.002 else 0.0
        samples.append((value * 0.35 + drop) * min(1.0, i / 2000, (length - i) / 2000))
    write_wav("Rain.wav", samples)
    # A dog howling in the distance (synthetic: a gliding tone with vibrato; real recordings would be better).
    length = int(RATE * 2.4)
    phase, samples = 0.0, []
    for i in range(length):
        t = i / length
        frequency = 420 + 260 * math.sin(math.pi * min(t * 1.4, 1.0)) + 12 * math.sin(2 * math.pi * 5.5 * i / RATE)
        phase += 2 * math.pi * frequency / RATE
        samples.append(0.35 * (math.sin(phase) + 0.3 * math.sin(2 * phase)) * math.sin(math.pi * t) ** 0.6)
    write_wav("Howl.wav", samples)
    # The alarm bell: two strokes of inharmonic partials ringing out.
    length = int(RATE * 2.5)
    samples = []
    for i in range(length):
        t = i / RATE
        value = 0.0
        for start in (0.0, 0.9):
            if t >= start:
                s = t - start
                value += sum(a * math.sin(2 * math.pi * f * s) for f, a in ((440, 0.3), (1107, 0.2), (1870, 0.12), (2633, 0.08))) * math.exp(-s / 0.7)
        samples.append(value * 0.7)
    write_wav("Bell.wav", samples)
    # Coins and silver: a few bright clinks.
    n = noise(12)
    length = int(RATE * 0.5)
    samples = []
    for i in range(length):
        t = i / RATE
        value = 0.0
        for start, pitch in ((0.0, 1.0), (0.09, 1.19), (0.2, 0.94)):
            if t >= start:
                s = t - start
                value += sum(a * math.sin(2 * math.pi * f * pitch * s) for f, a in ((2900, 0.25), (4700, 0.15), (6100, 0.08))) * math.exp(-s / 0.06)
        samples.append(value * 0.8 + 0.05 * n() * math.exp(-t / 0.02))
    write_wav("Coins.wav", samples)
    # "Hmm, just the weather": a short low hum.
    length = int(RATE * 0.6)
    write_wav("Mumble.wav", [0.4 * (math.sin(2 * math.pi * 140 * i / RATE) + 0.4 * math.sin(2 * math.pi * 280 * i / RATE))
                             * math.sin(math.pi * i / length) * (0.8 + 0.2 * math.sin(2 * math.pi * 6 * i / RATE)) for i in range(length)])

    # Wind: low-passed noise with a slow swell; loops seamlessly (starts and ends near zero).
    n = noise(5)
    value, samples = 0.0, []
    length = RATE * 4
    for i in range(length):
        value += (n() - value) * 0.02
        samples.append(value * 1.5 * (0.6 + 0.4 * math.sin(2 * math.pi * i / length)) * math.sin(math.pi * i / length) ** 0.1)
    write_wav("Wind.wav", samples)

    n = noise(22)  # a door: hinges creaking, a slow wavering squeal
    write_wav("Door.wav", [(0.25 * math.sin(2 * math.pi * (300 + 120 * math.sin(i / RATE * 9)) * i / RATE) * (0.6 + 0.4 * n())
                            * math.sin(math.pi * i / (RATE * 0.7))) for i in range(int(RATE * 0.7))])
    n = noise(21)  # a light switch
    write_wav("Click.wav", [(0.6 * n() * math.exp(-i / (RATE * 0.002)) + 0.3 * math.sin(2 * math.pi * 2400 * i / RATE)
                            * math.exp(-i / (RATE * 0.004))) for i in range(int(RATE * 0.05))])

# The lab is lit for the night like the other levels. Its haze is thin (0.01); at the default 0.5 the haze its torches
# light gave the thief away within a second or two where the guards could not see him before the fog glow (PR #35's
# report). The owner chose a middle way: at 0.25 the glow still outlines him, after a few seconds (2.9-7.1 s;
# LevelTests.cpp checks one).
PHYSICS_LAB_FOG_SCATTERING = 0.25


def build_physics_lab():
    """Physics sandbox: a ramp up to a stone platform; on top, a table with a copper jug. Push the
    table down the ramp or shoot the jug off it with an arrow; a guard nearby hears the clatter."""
    doc = Document("PenumbraScene")
    add_environment(doc, fog_scattering=PHYSICS_LAB_FOG_SCATTERING)  # night, like the other levels
    doc.add("Camera", Transform=transform((0, 3, 12)), Camera={"VerticalFOV": 65.0, "NearClip": 0.05, "FarClip": 200.0})
    stone, wood = (0.32, 0.31, 0.3), (0.3, 0.2, 0.12)
    box(doc, "Ground", (0, -0.5, 0), (30, 1, 30), (0.22, 0.22, 0.21))
    # The platform (top at 2 m) and the ramp up to it from the south: 6 m long, about 18 degrees.
    box(doc, "Platform", (0, 1, -6), (6, 2, 4), stone)
    slope = math.degrees(math.atan2(2.0, 6.0))
    normal = (0.0, math.cos(math.radians(slope)), math.sin(math.radians(slope)))
    doc.add("Ramp", Transform=transform((0, 1 - 0.2 * normal[1], -1 - 0.2 * normal[2]), rotation=(slope, 0, 0), scale=(4, 0.4, 6.5)),
            Mesh={"Mesh": "builtin:cube"}, Material=material(stone), BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]})

    # The table at the top of the ramp: one dynamic box for physics, a top and four legs to look at.
    table = doc.add("Table", Transform=transform((0, 2.45, -5.0)), BoxCollider={"HalfExtents": [0.8, 0.45, 0.45]},
                    RigidBody={"Type": "Dynamic", "Mass": 20.0, "Friction": 0.5},
                    Surface={"Material": "Wood", "NoiseModifier": 0.5, "ImpactLoudness": 7.0, "ImpactSound": "Audio/Thud.wav"})
    doc.add("Top", table, Transform=transform((0, 0.41, 0), scale=(1.6, 0.08, 0.9)), Mesh={"Mesh": "builtin:cube"},
            Material=material(wood))
    for i, (x, z) in enumerate(((-0.72, -0.37), (0.72, -0.37), (-0.72, 0.37), (0.72, 0.37))):
        doc.add(f"Leg{i}", table, Transform=transform((x, -0.04, z), scale=(0.08, 0.82, 0.08)), Mesh={"Mesh": "builtin:cube"},
                Material=material(wood))
    # The copper jug near the table's back edge: loud when it lands (a metal impact).
    doc.add("CopperJug", Transform=transform((0.3, 3.06, -5.25), scale=(0.2, 0.32, 0.2)), Mesh={"Mesh": "builtin:cylinder"},
            Material=material((0.72, 0.42, 0.2), roughness=0.35, metallic=0.9),
            BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]}, RigidBody={"Type": "Dynamic", "Mass": 1.5, "Friction": 0.4},
            Surface={"Material": "Metal", "NoiseModifier": 2.5, "ImpactLoudness": 8.0, "ImpactSound": "Audio/Clang.wav"})

    torch_prefab_entities(doc, (3.6, 0, -3.6), shadows=True)
    torch_prefab_entities(doc, (-2.5, 2, -7.6))
    # A guard pacing east of the platform: he hears the table or the jug come down.
    for name, position in (("WP_L1", (9, 0, -8)), ("WP_L2", (9, 0, 3))):
        doc.add(name, Transform=transform(position))
    add_guard(doc, "Guard", (9, 0.05, -8), 180, ["WP_L1", "WP_L2"], catch_scene="Scenes/PhysicsLab.pscene")
    # A lookout on the west side of the ramp, gazing west (away from the table and the jug): knock
    # him out (a gas bolt, or the blackjack from behind) and he slumps on the slope, a ragdoll;
    # bolts shove his body further down. An 18 degree ramp alone is too gentle to roll a body.
    add_guard(doc, "Lookout", (-1.6, 1.73, -3.0), 90, [], catch_scene="Scenes/PhysicsLab.pscene")
    add_player(doc, (0, 0.05, 8), yaw=0)
    doc.add("Navigation", Transform=transform(), NavMesh={})
    doc.save("Scenes/PhysicsLab.pscene")


def add_double_door(doc, name, center, axis, label="garden door"):
    """Two 1.2 m leaves hinged at the jambs of a 2.4 m opening, meeting in the middle. axis "x":
    the wall runs along X (the opening spans center.x +-1.2); "z": along Z."""
    x, z = center
    if axis == "x":
        add_door(doc, name + "Left", (x - 1.2, 0, z), 0, label=label)
        add_door(doc, name + "Right", (x + 1.2, 0, z), 180, label=label)
    else:
        add_door(doc, name + "Left", (x, 0, z + 1.2), 90, label=label)
        add_door(doc, name + "Right", (x, 0, z - 1.2), -90, label=label)


# Generated stone walls (Tools/AssetGen, provenance .json beside each; docs/art/mood/README.md, v3). One tile
# holds about six courses of stone, so it covers STONE_TILE_METRES of wall. The albedos are painted dark (mean
# linear 0.10 and 0.055); STONE_TINT lifts them towards the flat greys they replace.
STONE_TEXTURES = {"light": "Textures/StoneWall/stone_wall_1000", "dark": "Textures/StoneWall/stone_wall_1002"}
STONE_TILE_METRES = 2.0
STONE_TINT = 2.0


def tiled_material(texture, tint, repeats):
    """A generated material (albedo, normal, metallic/roughness under the path stem `texture`), tinted and
    repeated `repeats` (u, v) times across a face of the built-in cube."""
    mat = material((tint, tint, tint), roughness=1.0)
    mat.update({"AlbedoTexture": f"{texture}_albedo.png", "NormalTexture": f"{texture}_normal.png",
                "MetallicRoughnessTexture": f"{texture}_mr.png", "UVScale": [round(r, 3) for r in repeats]})
    return mat


def stone_wall_material(kind, size):
    """A tiled stone material for a wall box of the given size: the tile keeps its size on the long faces
    (the narrow ends and the top are stretched: the built-in cube has no world-space mapping)."""
    width, height = max(size[0], size[2]), size[1]
    return tiled_material(STONE_TEXTURES[kind], STONE_TINT, (width / STONE_TILE_METRES, height / STONE_TILE_METRES))


# Generated cobblestone on the floors (the mood lab): one tile covers COBBLE_TILE_METRES of floor. The built-in
# cube's top face maps u along Z and v along X. The albedo averages 0.055 linear; COBBLE_TINT lifts it towards
# the flat 0.22 it replaces.
COBBLE_TEXTURE = "Textures/Cobblestone/cobblestone_1001"
COBBLE_TILE_METRES = 1.5
COBBLE_TINT = 3.0


def cobblestone_material(size):
    """A tiled cobblestone material for the top face of a floor box of the given size."""
    return tiled_material(COBBLE_TEXTURE, COBBLE_TINT, (size[2] / COBBLE_TILE_METRES, size[0] / COBBLE_TILE_METRES))


# The corridors' roofs (the mood lab): each slab is two layers, generated roofing on top (wooden shingles: roofs
# are roofing, never the walls' stone, docs/art/World.md) and a wooden ceiling below (what the thief sees from the
# corridor). The cube's top face maps u along Z and v along X, its bottom face u along X and v along Z.
ROOF_TEXTURE = "Textures/Roof/roof_tiles_1003"
ROOF_TILE_METRES = 2.0
ROOF_TINT = 2.0
CEILING_TEXTURE = "Textures/Roof/wood_ceiling_1002"
CEILING_TILE_METRES = 2.0
CEILING_TINT = 2.0


def roof(doc, name, position, size, colour, textured):
    """A roof slab: flat, or (textured) roof tiles over a wooden ceiling, each half the thickness."""
    if not textured:
        box(doc, name, position, size, colour)
        return
    x, y, z = position
    half = (size[0], size[1] / 2, size[2])
    box(doc, name + "Tiles", (x, y + size[1] / 4, z), half, colour,
        mat=tiled_material(ROOF_TEXTURE, ROOF_TINT, (size[2] / ROOF_TILE_METRES, size[0] / ROOF_TILE_METRES)))
    box(doc, name, (x, y - size[1] / 4, z), half, colour,
        mat=tiled_material(CEILING_TEXTURE, CEILING_TINT, (size[0] / CEILING_TILE_METRES, size[2] / CEILING_TILE_METRES)))


def cloister_architecture(doc, textured=False):
    """The cloister's stone: ground, garden, the ring of covered corridors with doors, the wings. Textured: the
    walls wear the generated stone, the ground cobblestone, the roofs tiles over wooden ceilings (the mood lab); otherwise flat greys (the playable cloister)."""
    flat_stone, flat_dark_stone, wall_height = (0.32, 0.31, 0.3), (0.18, 0.18, 0.19), 3.2

    def wall(name, position, size, kind):
        colour = flat_stone if kind == "light" else flat_dark_stone
        box(doc, name, position, size, colour, mat=stone_wall_material(kind, size) if textured else None)

    stone, dark_stone = flat_stone, flat_dark_stone
    box(doc, "Ground", (0, -0.5, 0), (60, 1, 30), (0.22, 0.22, 0.21), mat=cobblestone_material((60, 1, 30)) if textured else None)

    # The garden: grass, a fountain in the middle and hedges for cover.
    floor_patch(doc, "Garden", "Grass", (0, 0), (15.6, 15.6))
    doc.add("Fountain", Transform=transform((0, 0.4, 0), scale=(2.4, 0.8, 2.4)), Mesh={"Mesh": "builtin:cylinder"},
            Material=material(stone), BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]})
    for i, (x, z, w, d) in enumerate(((-4.5, -4.5, 3, 1), (4.5, -4.5, 1, 3), (-4.5, 4.5, 1, 3), (4.5, 4.5, 3, 1))):
        box(doc, f"Hedge{i}", (x, 0.6, z), (w, 1.2, d), (0.1, 0.22, 0.08), roughness=1.0)

    # Garden walls (the inner side of the ring), each with a 2.4 m doorway in the middle.
    for side, sign in (("North", -1), ("South", 1)):
        z = 8 * sign
        wall(f"Inner{side}A", (-4.7, wall_height / 2, z), (7.0, wall_height, 0.4), "light")
        wall(f"Inner{side}B", (4.7, wall_height / 2, z), (7.0, wall_height, 0.4), "light")
        wall(f"Inner{side}Lintel", (0, 2.7, z), (2.4, 1.0, 0.4), "light")
        add_double_door(doc, f"Door{side}", (0, z), "x")
    for side, sign in (("West", -1), ("East", 1)):
        x = 8 * sign
        wall(f"Inner{side}A", (x, wall_height / 2, -4.7), (0.4, wall_height, 7.0), "light")
        wall(f"Inner{side}B", (x, wall_height / 2, 4.7), (0.4, wall_height, 7.0), "light")
        wall(f"Inner{side}Lintel", (x, 2.7, 0), (0.4, 1.0, 2.4), "light")
        add_double_door(doc, f"Door{side}", (x, 0), "z")

    # Outer walls of the ring; the east and west ones open onto the wings.
    for side, sign in (("North", -1), ("South", 1)):
        wall(f"Outer{side}", (0, wall_height / 2, 11.2 * sign), (22.8, wall_height, 0.4), "dark")
    for side, sign in (("West", -1), ("East", 1)):
        wall(f"Outer{side}A", (11.2 * sign, wall_height / 2, -6.45), (0.4, wall_height, 9.9), "dark")
        wall(f"Outer{side}B", (11.2 * sign, wall_height / 2, 6.45), (0.4, wall_height, 9.9), "dark")
    # The ring's roof (the garden stays open to the sky).
    for name, position, size in (("RoofNorth", (0, 3.3, -9.6), (22.8, 0.2, 3.6)), ("RoofSouth", (0, 3.3, 9.6), (22.8, 0.2, 3.6)),
                                 ("RoofWest", (-9.6, 3.3, 0), (3.6, 0.2, 15.6)), ("RoofEast", (9.6, 3.3, 0), (3.6, 0.2, 15.6))):
        roof(doc, name, position, size, dark_stone, textured)

    # The wings: 3 m wide corridors, 14 m long, roofed, closed at the far end.
    for side, sign in (("West", -1), ("East", 1)):
        middle = 18.7 * sign
        wall(f"{side}WingNorth", (middle, wall_height / 2, -1.7), (14.6, wall_height, 0.4), "dark")
        wall(f"{side}WingSouth", (middle, wall_height / 2, 1.7), (14.6, wall_height, 0.4), "dark")
        wall(f"{side}WingEnd", (26.2 * sign, wall_height / 2, 0), (0.4, wall_height, 3.8), "dark")
        roof(doc, f"{side}WingRoof", (middle, 3.3, 0), (14.6, 0.2, 3.8), dark_stone, textured)


def build_cloister():
    """The cloister (an outbuilding): a square garden ringed by covered corridors, a pair of doors
    into the garden from each side, wings leading off east and west. Two guards walk the ring the
    opposite ways. The thief starts at the end of the east wing and must reach the west wing."""
    doc = Document("PenumbraScene")
    add_environment(doc)
    doc.add("Camera", Transform=transform((24, 3, 0)), Camera={"VerticalFOV": 65.0, "NearClip": 0.05, "FarClip": 300.0})
    cloister_architecture(doc)

    # Light: torches in the ring's corners and one in each wing; the doorways in between stay dark.
    for i, (x, z) in enumerate(((10.5, 10.5), (-10.5, 10.5), (10.5, -10.5), (-10.5, -10.5))):
        torch_prefab_entities(doc, (x, 0, z), shadows=i < 2)
    torch_prefab_entities(doc, (17, 0, -1.2))
    torch_prefab_entities(doc, (-17, 0, 1.2))

    # Two guards walk the ring the opposite ways, pausing at the corners.
    corners = {"NE": (9.6, 0, -9.6), "SE": (9.6, 0, 9.6), "SW": (-9.6, 0, 9.6), "NW": (-9.6, 0, -9.6)}
    for name, position in corners.items():
        doc.add(f"WP_{name}", Transform=transform(position))
    add_guard(doc, "GuardNorth", (9.6, 0.05, -9.6), 180, ["WP_NE", "WP_SE", "WP_SW", "WP_NW"], lantern=True,
              catch_scene="Scenes/Cloister.pscene")
    add_guard(doc, "GuardSouth", (-9.6, 0.05, 9.6), -90, ["WP_SW", "WP_SE", "WP_NE", "WP_NW"], character=KNIGHT,
              catch_scene="Scenes/Cloister.pscene")
    doc.add("Navigation", Transform=transform(), NavMesh={})

    # Start at the end of the east wing; the way out is the end of the west wing.
    add_player(doc, (24.5, 0.05, 0), yaw=90)
    doc.add("Goal", Transform=transform((-24.5, 1.5, 0)), BoxCollider={"HalfExtents": [1.2, 1.5, 1.4]},
            RigidBody={"Type": "Static", "IsTrigger": True}, Script={"Script": "Scripts/Goal.lua"})
    doc.add("Mission", Transform=transform(), Script={"Script": "Scripts/MissionControl.lua",
                                                      "Properties": {"mission": "Scripts/Missions/Cloister"}})
    doc.add("Ambience", Transform=transform((0, 3, 0)),
            AudioSource={"Clip": "Audio/Wind.wav", "Loop": True, "PlayOnStart": True, "Spatial": False, "Volume": 0.2})
    doc.save("Scenes/Cloister.pscene")


# The mood lab (docs/art/mood): the cloister's stone with no guards, lit three ways for choosing the
# game's lighting. Warm zone = east half, cold zone = west half. Every light source is a root named
# "<Fire|Astral>_<Warm|Cold>_<kind><n>"; Scripts/MoodLab.lua switches them and the environment per
# variant (docs/art/mood/MoodLab.jsonl renders them). Not a level: nothing here is played.
MOOD_TORCHES = (((10.5, 10.5), "Warm", True), ((10.5, -10.5), "Warm", False), ((17, -1.2), "Warm", False),
                ((-10.5, 10.5), "Cold", True), ((-10.5, -10.5), "Cold", False), ((-17, 1.2), "Cold", False))
MOOD_BRAZIERS = (((21.5, 0.9), "Warm"), ((3.0, 3.0), "Warm"), ((-21.5, -0.9), "Cold"), ((-3.0, -3.0), "Cold"))
MOOD_ASTRAL_LAMPS = (((8.8, -10.6), "Warm"), ((9.6, 5.0), "Warm"), ((14.0, 1.2), "Warm"), ((3.0, -3.0), "Warm"),
                     ((-8.8, -10.6), "Cold"), ((-9.6, 5.0), "Cold"), ((-14.0, -1.2), "Cold"), ((-3.0, 3.0), "Cold"))


def mood_brazier(doc, name, position, shadows=False):
    """A fire basket: an iron bowl on three legs, a big flame and a warm flickering light."""
    root = doc.add(name, Transform=transform((position[0], 0, position[1])))
    iron = material((0.08, 0.07, 0.07), roughness=0.6, metallic=0.7)
    for index, angle in enumerate((0, 120, 240)):
        doc.add(f"Leg{index + 1}", root, Transform=transform((0.22 * math.sin(math.radians(angle)), 0.4, 0.22 * math.cos(math.radians(angle))),
                                                             scale=(0.04, 0.8, 0.04)),
                Mesh={"Mesh": "builtin:cylinder"}, Material=iron)
    doc.add("Bowl", root, Transform=transform((0, 0.85, 0), scale=(0.6, 0.18, 0.6)), Mesh={"Mesh": "builtin:cylinder"}, Material=iron)
    doc.add("Embers", root, Transform=transform((0, 0.95, 0), scale=(0.5, 0.04, 0.5)), Mesh={"Mesh": "builtin:cylinder", "CastShadows": False},
            Material=material((0.3, 0.08, 0.02), emissive=3.0, emissive_color=(1.0, 0.35, 0.08)))
    doc.add("Flame", root, Transform=transform((0, 0.95, 0)), Flame={"Height": 0.75, "Radius": 0.22, "Intensity": 5.0},
            PointLight={"Color": [1.0, 0.5, 0.2], "Intensity": 20.0, "Range": 12.0, "Flicker": 0.2, "CastShadows": shadows})


def mood_astral_lamp(doc, name, position, shadows=False):
    """An astral lamp: a dark iron post holding a glowing crystal, a cold steady light."""
    root = doc.add(name, Transform=transform((position[0], 0, position[1])))
    doc.add("Post", root, Transform=transform((0, 0.75, 0), scale=(0.07, 1.5, 0.07)), Mesh={"Mesh": "builtin:cylinder"},
            Material=material((0.06, 0.06, 0.08), roughness=0.5, metallic=0.8))
    doc.add("Crystal", root, Transform=transform((0, 1.72, 0), scale=(0.16, 0.34, 0.16)), Mesh={"Mesh": "builtin:sphere", "CastShadows": False},
            Material=material((0.3, 0.8, 1.0), roughness=0.2, emissive=6.0, emissive_color=(0.35, 0.85, 1.0)))
    doc.add("Glow", root, Transform=transform((0, 1.72, 0)),
            PointLight={"Color": [0.35, 0.85, 1.0], "Intensity": 14.0, "Range": 10.0, "Flicker": 0.0, "CastShadows": shadows})


def build_mood_lab():
    doc = Document("PenumbraScene")
    add_environment(doc)
    doc.add("Camera", Transform=transform((1.5, 1.6, -9.4), (0, -90, 0)), Camera={"VerticalFOV": 65.0, "NearClip": 0.05, "FarClip": 300.0})
    cloister_architecture(doc, textured=True)
    for index, ((x, z), zone, shadows) in enumerate(MOOD_TORCHES):
        torch_prefab_entities(doc, (x, 0, z), shadows=shadows, name=f"Fire_{zone}_Torch{index + 1}")
    for index, ((x, z), zone) in enumerate(MOOD_BRAZIERS):
        mood_brazier(doc, f"Fire_{zone}_Brazier{index + 1}", (x, z), shadows=index in (0, 2))
    for index, ((x, z), zone) in enumerate(MOOD_ASTRAL_LAMPS):
        mood_astral_lamp(doc, f"Astral_{zone}_Lamp{index + 1}", (x, z), shadows=index in (0, 4))
    # The thief in the north corridor, facing west: in the close shot he stands against the lights of
    # the north-east corner. No script, no controller: only the model playing its idle.
    thief = doc.add("Thief", Transform=transform((5.0, 0.05, -9.6), (0, 90, 0)), PerceptionTarget={"Reflectance": 0.25})
    character_model(doc, thief, THIEF)
    # Mist lying low over the garden (a FogVolume); the variants switch it on.
    doc.add("Mist_Garden", Transform=transform((0, 0.6, 0)),
            FogVolume={"HalfExtents": [7.8, 1.2, 7.8], "Density": 0.12, "EdgeFade": 0.8, "Enabled": False})
    doc.add("MoodLab", Transform=transform(), Script={"Script": "Scripts/MoodLab.lua"})
    doc.save("Scenes/MoodLab.pscene")


def build_project_file():
    project = {"Name": "Penumbra Demo", "StartScene": "Scenes/Menu.pscene", "AssetDirectory": "Assets",
               "WindowWidth": 1600, "WindowHeight": 900}
    (ROOT / "project.json").write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    build_project_file()
    build_prefabs()
    build_stealth_level()
    build_menu()
    build_feature_test()
    build_perception_lab()
    build_ai_lab()
    build_physics_lab()
    build_cloister()
    build_mood_lab()
    build_audio()
    print(f"Demo project written to {ROOT}")
