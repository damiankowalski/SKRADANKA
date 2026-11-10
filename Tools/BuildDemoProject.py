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


def box(doc, name, position, size, color, parent=None, emissive=0.0, emissive_color=(1, 1, 1), collider=True, roughness=0.85):
    components = {
        "Transform": transform(position, scale=size),
        "Mesh": {"Mesh": "builtin:cube"},
        "Material": material(color, roughness=roughness, emissive=emissive, emissive_color=emissive_color),
    }
    if collider:
        components["BoxCollider"] = {"HalfExtents": [0.5, 0.5, 0.5]}
    return doc.add(name, parent, **components)


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
    doc.add("Flame", post, Transform=transform((0, 1.3, 0), scale=(0.16, 0.22, 0.16)),
            Mesh={"Mesh": "builtin:sphere", "CastShadows": False},
            Material=material((1.0, 0.6, 0.2), emissive=6.0, emissive_color=(1.0, 0.55, 0.2)),
            PointLight={"Color": [1.0, 0.62, 0.32], "Intensity": 14.0, "Range": 9.0, "Flicker": 0.15, "CastShadows": shadows},
            StimSource={"Stim": "Fire", "Delivery": "Continuous", "Radius": 0.5, "Intensity": 1.0})
    return post


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
                        StimSource={"Stim": stim, "Delivery": "Contact", "Intensity": 1.0, "Radius": 1.0, "DestroyOnContact": True})
        components = {"Transform": transform(scale=(0.05, 0.05, 0.6)), "Mesh": {"Mesh": "builtin:cube", "CastShadows": False},
                      "Material": material(color, emissive=4.0 if light else 0.5, emissive_color=color)}
        if light:
            components["PointLight"] = {"Color": list(light), "Intensity": 4.0, "Range": 5.0, "Flicker": 0.2}
        doc.add("Shaft", arrow, **components)
        doc.save(f"Prefabs/{name}.pprefab")

    doc = Document("PenumbraPrefab")
    doc.add("PowderKeg",
            Transform=transform(scale=(0.7, 0.9, 0.7)),
            Mesh={"Mesh": "builtin:cylinder"}, Material=material((0.35, 0.2, 0.1)),
            BoxCollider={"HalfExtents": [0.5, 0.5, 0.5]},
            StimResponse={"Rules": [{"Stim": "Fire", "Threshold": 0.5, "Once": True, "Actions": [
                {"Type": "EmitStim", "Asset": "Fire", "Value": 1.0, "Radius": 3.5},
                {"Type": "RadialImpulse", "Value": 40.0, "Radius": 5.0},
                {"Type": "EmitNoise", "Radius": 35.0},
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

def character_body(doc, parent, color, head_color):
    doc.add("Body", parent, Transform=transform((0, 0.9, 0), scale=(0.6, 0.9, 0.6)), Mesh={"Mesh": "builtin:capsule"},
            Material=material(color, roughness=0.9))
    doc.add("Head", parent, Transform=transform((0, 1.7, -0.05), scale=(0.3, 0.3, 0.3)), Mesh={"Mesh": "builtin:sphere"},
            Material=material(head_color, roughness=0.9))


def add_player(doc, position, yaw=0.0):
    player = doc.add("Player",
                     Transform=transform(position, (0, yaw, 0)),
                     CharacterController={"Radius": 0.3, "Height": 1.8},
                     PerceptionTarget={"Reflectance": 0.25},
                     AudioListener={},
                     Script={"Script": "Scripts/Player.lua"})
    character_body(doc, player, (0.12, 0.12, 0.14), (0.5, 0.42, 0.35))
    return player


def add_guard(doc, name, position, yaw, waypoints, lantern=False):
    guard = doc.add(name,
                    Transform=transform(position, (0, yaw, 0)),
                    CharacterController={"Radius": 0.3, "Height": 1.8},
                    Perception={},
                    Script={"Script": "Scripts/Guard.lua", "Properties": {"waypoints": ",".join(waypoints)}})
    character_body(doc, guard, (0.45, 0.12, 0.1), (0.55, 0.45, 0.38))
    if lantern:
        doc.add("Lantern", guard, Transform=transform((0.25, 1.2, -0.3), (-15, 0, 0)),
                SpotLight={"Color": [1.0, 0.85, 0.6], "Intensity": 30.0, "Range": 14.0, "InnerAngle": 18.0, "OuterAngle": 32.0,
                           "CastShadows": True})
    return guard


def add_environment(doc, ambient=0.015, map_intensity=0.25, fog=0.01):
    doc.add("Environment", Transform=transform(),
            Environment={"EnvironmentMap": "Environment/NightSky_1k.hdr", "EnvironmentIntensity": map_intensity,
                         "SkyboxIntensity": 0.6, "AmbientColor": [ambient, ambient, ambient * 1.4], "Exposure": 1.4,
                         "FogDensity": fog, "FogColor": [0.02, 0.025, 0.035], "SSAOEnabled": True, "BloomIntensity": 0.05})
    doc.add("Moon", Transform=transform(rotation=(-35, 140, 0)),
            DirectionalLight={"Color": [0.55, 0.65, 1.0], "Intensity": 0.18, "CastShadows": True, "ShadowSoftness": 1.5})


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
        doc.add(f"HallLight{i}", Transform=transform((x, 4.5, -12)),
                PointLight={"Color": [1.0, 0.85, 0.65], "Intensity": 60.0, "Range": 12.0, "CastShadows": i == 1})
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
                    {"Type": "EmitNoise", "Radius": 35.0},
                    {"Type": "PlaySound", "Asset": "Audio/Explosion.wav", "Value": 1.0},
                    {"Type": "SpawnPrefab", "Asset": "Prefabs/Smoke.pprefab"},
                    {"Type": "Destroy"}]}]})

    # Patrol waypoints and guards.
    for name, position in (("WP_A1", (-10, 0, -4)), ("WP_A2", (8, 0, -5)), ("WP_A3", (8, 0, 4)), ("WP_A4", (-10, 0, 6)),
                           ("WP_B1", (14, 0, 12)), ("WP_B2", (14, 0, -4))):
        doc.add(name, Transform=transform(position))
    add_guard(doc, "GuardA", (-10, 0.05, -4), 90, ["WP_A1", "WP_A2", "WP_A3", "WP_A4"])
    add_guard(doc, "GuardB", (14, 0.05, 12), 180, ["WP_B1", "WP_B2"], lantern=True)

    # The exit gate (south-east corner) and the player start (dark north-west corner).
    doc.add("Gate", Transform=transform((17, 1.5, 18)), BoxCollider={"HalfExtents": [1.5, 1.5, 1.0]},
            RigidBody={"Type": "Static", "IsTrigger": True}, Script={"Script": "Scripts/Goal.lua"})
    box(doc, "GateArch", (17, 4.2, 19.2), (4, 0.6, 0.6), stone)
    add_player(doc, (-17, 0.05, -17), yaw=-135)
    doc.add("Ambience", Transform=transform((0, 3, 0)),
            AudioSource={"Clip": "Audio/Wind.wav", "Loop": True, "PlayOnStart": True, "Spatial": False, "Volume": 0.25})
    doc.save("Scenes/Stealth.pscene")


def build_feature_test():
    doc = Document("PenumbraScene")
    add_environment(doc, ambient=0.02, map_intensity=0.2, fog=0.0)
    doc.add("Camera", Transform=transform((6, 4, 10), (-15, 30, 0)), Camera={})
    box(doc, "Ground", (0, -0.5, 0), (40, 1, 40), (0.3, 0.3, 0.3))
    box(doc, "Doorway", (0, 3, -9), (12, 6, 0.4), (0.3, 0.3, 0.3), emissive=2.0)
    box(doc, "Curtain", (0, 2, -3), (6, 4, 0.2), (0.3, 0.3, 0.3))
    guard = doc.add("Guard", Transform=transform((0, 0.05, 0)), CharacterController={}, Perception={})
    character_body(doc, guard, (0.45, 0.12, 0.1), (0.55, 0.45, 0.38))
    player = doc.add("Player", Transform=transform((0, 0.05, -6)), CharacterController={}, PerceptionTarget={})
    character_body(doc, player, (0.12, 0.12, 0.14), (0.5, 0.42, 0.35))
    doc.add("Crate", Transform=transform((4, 0.5, 8)), Mesh={"Mesh": "builtin:cube"}, Material=material((0.4, 0.3, 0.2)),
            BoxCollider={}, RigidBody={"Type": "Dynamic"})
    doc.add("Bouncer", Transform=transform((-4, 3, 8), scale=(0.5, 0.5, 0.5)), Mesh={"Mesh": "builtin:sphere"},
            SphereCollider={}, RigidBody={"Type": "Dynamic", "Restitution": 0.5}, Script={"Script": "Scripts/Tests/Bouncer.lua"})
    doc.add("Speaker", Transform=transform((2, 1, 2)), AudioSource={"Clip": "Audio/Wind.wav", "Loop": True})
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
    # Wind: low-passed noise with a slow swell; loops seamlessly (starts and ends near zero).
    n = noise(5)
    value, samples = 0.0, []
    length = RATE * 4
    for i in range(length):
        value += (n() - value) * 0.02
        samples.append(value * 1.5 * (0.6 + 0.4 * math.sin(2 * math.pi * i / length)) * math.sin(math.pi * i / length) ** 0.1)
    write_wav("Wind.wav", samples)


def build_project_file():
    project = {"Name": "Penumbra Demo", "StartScene": "Scenes/Stealth.pscene", "AssetDirectory": "Assets",
               "WindowWidth": 1600, "WindowHeight": 900}
    (ROOT / "project.json").write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    build_project_file()
    build_prefabs()
    build_stealth_level()
    build_feature_test()
    build_audio()
    print(f"Demo project written to {ROOT}")
