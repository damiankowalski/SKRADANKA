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
        surface=None):
    components = {
        "Transform": transform(position, scale=size),
        "Mesh": {"Mesh": "builtin:cube"},
        "Material": material(color, roughness=roughness, emissive=emissive, emissive_color=emissive_color),
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

def torch_prefab_entities(doc, position=(0, 0, 0), parent=None, shadows=False):
    """Wall torch: a post plus a flame child (flickering light, emissive, continuous fire)."""
    post = doc.add("Torch", parent,
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
        components = {"Transform": transform(scale=(0.05, 0.05, 0.6)), "Mesh": {"Mesh": "builtin:cube", "CastShadows": False},
                      "Material": material(color, emissive=4.0 if light else 0.5, emissive_color=color)}
        if light:
            components["PointLight"] = {"Color": list(light), "Intensity": 4.0, "Range": 5.0, "Flicker": 0.2}
        doc.add("Shaft", arrow, **components)
        doc.save(f"Prefabs/{name}.pprefab")

    # Gas arrow: bursts into a sleeping-gas cloud where it lands (Scripts/GasArrow.lua).
    doc = Document("PenumbraPrefab")
    arrow = doc.add("GasArrow", Transform=transform(), SphereCollider={"Radius": 0.06},
                    RigidBody={"Type": "Dynamic", "Mass": 0.1, "GravityFactor": 0.3, "ContinuousCollision": True,
                               "TransparentToSight": True},
                    PerceptionTarget=arrow_target(emissive=0.3),
                    Script={"Script": "Scripts/GasArrow.lua"})
    doc.add("Shaft", arrow, Transform=transform(scale=(0.05, 0.05, 0.6)), Mesh={"Mesh": "builtin:cube", "CastShadows": False},
            Material=material((0.4, 0.8, 0.3), emissive=1.5, emissive_color=(0.4, 0.9, 0.3)))
    doc.save("Prefabs/GasArrow.pprefab")
    # Moss arrow: a patch of moss where it lands on a floor (Scripts/MossArrow.lua).
    doc = Document("PenumbraPrefab")
    arrow = doc.add("MossArrow", Transform=transform(), SphereCollider={"Radius": 0.06},
                    RigidBody={"Type": "Dynamic", "Mass": 0.1, "GravityFactor": 0.3, "ContinuousCollision": True,
                               "TransparentToSight": True},
                    PerceptionTarget=arrow_target(),
                    Script={"Script": "Scripts/MossArrow.lua"})
    doc.add("Shaft", arrow, Transform=transform(scale=(0.05, 0.05, 0.6)), Mesh={"Mesh": "builtin:cube", "CastShadows": False},
            Material=material((0.25, 0.4, 0.15), emissive=0.3, emissive_color=(0.3, 0.5, 0.2)))
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
                     Script={"Script": "Scripts/Player.lua"})
    character_model(doc, player, THIEF)
    return player


def add_guard(doc, name, position, yaw, waypoints, lantern=False, character=WATCHMAN, **properties):
    guard = doc.add(name,
                    Transform=transform(position, (0, yaw, 0)),
                    CharacterController={"Radius": 0.3, "Height": 1.8},
                    Perception={},
                    StimResponse=NPC_RESPONSES,
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
                      Perception={"FieldOfView": 140.0}, StimResponse=NPC_RESPONSES,
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


def add_environment(doc, ambient=0.006, map_intensity=0.12, fog=0.01, moon=0.08):
    doc.add("Environment", Transform=transform(),
            Environment={"EnvironmentMap": "Environment/NightSky_1k.hdr", "EnvironmentIntensity": map_intensity,
                         "SkyboxIntensity": 0.6, "AmbientColor": [ambient, ambient, ambient * 1.4], "Exposure": 1.4,
                         "FogDensity": fog, "FogColor": [0.02, 0.025, 0.035], "SSAOEnabled": True, "BloomIntensity": 0.05})
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
                       PointLight={"Color": [1.0, 0.85, 0.65], "Intensity": 60.0, "Range": 12.0, "CastShadows": i == 1})
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

    # Torches along the walls (two cast shadows).
    for i, (x, z, shadows) in enumerate(((-18.8, 6, True), (-6, -18.8, False), (18.8, 10, True), (0, 18.8, False), (8, -7.2, False))):
        torch_prefab_entities(doc, (x, 0, z), shadows=shadows)

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

    # Patrol waypoints and guards.
    for name, position in (("WP_A1", (-10, 0, -4)), ("WP_A2", (8, 0, -5)), ("WP_A3", (8, 0, 4)), ("WP_A4", (-10, 0, 6)),
                           ("WP_B1", (14, 0, 12)), ("WP_B2", (14, 0, -4))):
        doc.add(name, Transform=transform(position))
    guard_a = add_guard(doc, "GuardA", (-10, 0.05, -4), 90, ["WP_A1", "WP_A2", "WP_A3", "WP_A4"])
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


def build_perception_lab():
    """One guard, one campfire, one player: the place to tune and understand NPC vision.
    A small walled yard so firelight falls on walls as well as the floor; a polished stone
    patch near the fire, a dark corner, pillars for cover and a lit doorway behind the guard's
    field of view for silhouette tests."""
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
    guard = doc.add("Guard", Transform=transform((0, 0.05, 0)), CharacterController={}, Perception={})
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

    n = noise(21)  # a light switch
    write_wav("Click.wav", [(0.6 * n() * math.exp(-i / (RATE * 0.002)) + 0.3 * math.sin(2 * math.pi * 2400 * i / RATE)
                            * math.exp(-i / (RATE * 0.004))) for i in range(int(RATE * 0.05))])

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
    build_audio()
    print(f"Demo project written to {ROOT}")
