"""Imports the demo characters from the KayKit "Character Pack: Adventurers" (CC0) and authors the
clips the pack lacks: Crouch_Idle, Crouch_Walk and Look_Around.

    git clone https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0 <dir>
    python Tools/ImportCharacters.py <dir>

Writes DemoProject/Assets/Models/Characters/{Thief,Knight}.glb. The output is deterministic, so
re-running it yields identical files. Every authored clip is baked at 30 fps for all nodes:

* Crouch_*: the hips drop, the spine leans forward and both legs are re-solved with two-bone IK
  so the feet stay exactly where the source clip (Idle / Walking_A) put them.
* Look_Around: four Idle cycles with a slow head-and-chest sweep left and right.

The thief's green cloth is recoloured to charcoal in the embedded colour atlas.

Only the Python standard library is used.
"""

import json
import math
import shutil
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "DemoProject" / "Assets" / "Models" / "Characters"
SOURCE_SUBDIR = Path("addons/kaykit_character_pack_adventures/Characters/gltf")
# output -> (source file, tint replacing the green cloth or None)
CHARACTERS = {"Thief.glb": ("Rogue_Hooded.glb", (0.55, 0.5, 0.46)), "Knight.glb": ("Knight.glb", None)}
FPS = 30.0

CROUCH_DROP = 0.13        # hips drop, model units (the hips rest at 0.406)
CROUCH_LEAN = math.radians(22.0)
LOOK_YAW = math.radians(60.0)

# --------------------------------------------------------------------------------------------
# Vector / quaternion helpers (quaternions are (x, y, z, w) like glTF)
# --------------------------------------------------------------------------------------------


def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def scale(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def normalize(a): return scale(a, 1.0 / max(length(a), 1e-12))


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qconj(q): return (-q[0], -q[1], -q[2], q[3])


def qnormalize(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)


def qrotate(q, v):
    u = (q[0], q[1], q[2])
    t = scale(cross(u, v), 2.0)
    return add(add(v, scale(t, q[3])), cross(u, t))


def qaxis(axis, angle):
    axis = normalize(axis)
    s = math.sin(angle * 0.5)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle * 0.5))


def qarc(a, b):
    """Shortest rotation taking direction a to direction b."""
    a, b = normalize(a), normalize(b)
    d = dot(a, b)
    if d > 0.999999:
        return (0.0, 0.0, 0.0, 1.0)
    if d < -0.999999:
        axis = cross((1.0, 0.0, 0.0), a)
        if length(axis) < 1e-6:
            axis = cross((0.0, 1.0, 0.0), a)
        return qaxis(axis, math.pi)
    c = cross(a, b)
    return qnormalize((c[0], c[1], c[2], 1.0 + d))


def qslerp_linear(a, b, t):
    if sum(x * y for x, y in zip(a, b)) < 0.0:
        b = tuple(-c for c in b)
    return qnormalize(tuple(x + (y - x) * t for x, y in zip(a, b)))


# --------------------------------------------------------------------------------------------
# GLB container
# --------------------------------------------------------------------------------------------


class Glb:
    def __init__(self, path):
        data = Path(path).read_bytes()
        magic, version, _ = struct.unpack_from("<III", data, 0)
        if magic != 0x46546C67 or version != 2:
            raise ValueError(f"{path}: not a glTF 2.0 binary")
        json_length, _ = struct.unpack_from("<II", data, 12)
        self.json = json.loads(data[20:20 + json_length])
        bin_offset = 20 + json_length
        bin_length, _ = struct.unpack_from("<II", data, bin_offset)
        self.bin = bytearray(data[bin_offset + 8:bin_offset + 8 + bin_length])

    def read_accessor(self, index):
        accessor = self.json["accessors"][index]
        if accessor["componentType"] != 5126:
            raise ValueError("only float accessors are supported here")
        view = self.json["bufferViews"][accessor["bufferView"]]
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
        stride = view.get("byteStride", width * 4)
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return [struct.unpack_from(f"<{width}f", self.bin, offset + i * stride) for i in range(accessor["count"])]

    def add_accessor(self, values, kind):
        """Appends tightly packed float data and returns the new accessor index."""
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[kind]
        while len(self.bin) % 4:
            self.bin.append(0)
        offset = len(self.bin)
        for value in values:
            self.bin += struct.pack(f"<{width}f", *(value if width > 1 else (value,)))
        self.json["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(self.bin) - offset})
        accessor = {"bufferView": len(self.json["bufferViews"]) - 1, "componentType": 5126, "count": len(values), "type": kind}
        if kind == "SCALAR":
            accessor["min"] = [min(values)]
            accessor["max"] = [max(values)]
        self.json["accessors"].append(accessor)
        return len(self.json["accessors"]) - 1

    def save(self, path):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.json["buffers"][0]["byteLength"] = len(self.bin)
        text = json.dumps(self.json, separators=(",", ":"), sort_keys=True).encode("utf-8")
        text += b" " * ((4 - len(text) % 4) % 4)
        total = 12 + 8 + len(text) + 8 + len(self.bin)
        out = struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(text), 0x4E4F534A) + text
        out += struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin)
        Path(path).write_bytes(out)


# --------------------------------------------------------------------------------------------
# Skeleton sampling
# --------------------------------------------------------------------------------------------


class Rig:
    def __init__(self, glb):
        self.glb = glb
        self.nodes = glb.json["nodes"]
        self.parent = {child: i for i, node in enumerate(self.nodes) for child in node.get("children", [])}
        self.index = {node.get("name"): i for i, node in enumerate(self.nodes)}
        self.rest = [(tuple(n.get("translation", (0, 0, 0))), tuple(n.get("rotation", (0, 0, 0, 1))), tuple(n.get("scale", (1, 1, 1))))
                     for n in self.nodes]
        # Parents first: process nodes in depth order.
        self.order = sorted(range(len(self.nodes)), key=self.depth)

    def depth(self, i):
        d = 0
        while i in self.parent:
            i = self.parent[i]
            d += 1
        return d

    def clip(self, name):
        for animation in self.glb.json["animations"]:
            if animation["name"] == name:
                channels = []
                for channel in animation["channels"]:
                    sampler = animation["samplers"][channel["sampler"]]
                    if sampler.get("interpolation", "LINEAR") not in ("LINEAR", "STEP"):
                        raise ValueError(f"{name}: only LINEAR/STEP source clips are supported")
                    times = [t[0] for t in self.glb.read_accessor(sampler["input"])]
                    values = self.glb.read_accessor(sampler["output"])
                    channels.append((channel["target"]["node"], channel["target"]["path"], sampler.get("interpolation", "LINEAR"), times, values))
                return channels, max(c[3][-1] for c in channels)
        raise KeyError(f"clip '{name}' not found")

    def sample(self, channels, time):
        pose = [list(r) for r in self.rest]
        for node, path, interpolation, times, values in channels:
            if time <= times[0]:
                value = values[0]
            elif time >= times[-1]:
                value = values[-1]
            else:
                k = next(i for i in range(1, len(times)) if times[i] > time)
                t = (time - times[k - 1]) / (times[k] - times[k - 1])
                a, b = values[k - 1], values[k]
                if interpolation == "STEP":
                    value = a
                elif path == "rotation":
                    value = qslerp_linear(a, b, t)
                else:
                    value = tuple(x + (y - x) * t for x, y in zip(a, b))
            slot = {"translation": 0, "rotation": 1, "scale": 2}.get(path)
            if slot is not None:
                pose[node][slot] = tuple(value)
        return pose

    def globals(self, pose):
        result = [None] * len(self.nodes)
        for i in self.order:
            t, r, s = pose[i]
            if i in self.parent:
                pt, pr, ps = result[self.parent[i]]
                result[i] = (add(pt, qrotate(pr, (t[0] * ps[0], t[1] * ps[1], t[2] * ps[2]))), qnormalize(qmul(pr, r)),
                             (ps[0] * s[0], ps[1] * s[1], ps[2] * s[2]))
            else:
                result[i] = (t, qnormalize(r), s)
        return result

    def set_global_rotation(self, pose, globals_, node, rotation):
        parent_rotation = globals_[self.parent[node]][1] if node in self.parent else (0, 0, 0, 1)
        pose[node][1] = qnormalize(qmul(qconj(parent_rotation), rotation))


# --------------------------------------------------------------------------------------------
# Clip authoring
# --------------------------------------------------------------------------------------------


def solve_leg(rig, pose, side, foot_target, bend_axis):
    """Two-bone IK: bends upperleg/lowerleg so the foot reaches foot_target, knee forward."""
    upper, lower, foot = (rig.index[f"{bone}.{side}"] for bone in ("upperleg", "lowerleg", "foot"))
    g = rig.globals(pose)
    old_foot_rotation = g[foot][1]
    hip, knee, ankle = g[upper][0], g[lower][0], g[foot][0]
    thigh, shin = length(sub(knee, hip)), length(sub(ankle, knee))
    to_target = sub(foot_target, hip)
    distance = min(max(length(to_target), 1e-4), (thigh + shin) * 0.999)
    direction = normalize(to_target)
    along = (thigh * thigh - shin * shin + distance * distance) / (2.0 * distance)
    height = math.sqrt(max(thigh * thigh - along * along, 0.0))
    forward = normalize(cross(direction, bend_axis))
    new_knee = add(add(hip, scale(direction, along)), scale(forward, height))
    new_ankle = add(hip, scale(direction, distance))

    # Upper leg: rotate its global orientation so the knee lands on new_knee.
    upper_rotation = qmul(qarc(sub(knee, hip), sub(new_knee, hip)), g[upper][1])
    rig.set_global_rotation(pose, g, upper, upper_rotation)
    g = rig.globals(pose)
    lower_rotation = qmul(qarc(sub(g[foot][0], g[lower][0]), sub(new_ankle, g[lower][0])), g[lower][1])
    rig.set_global_rotation(pose, g, lower, lower_rotation)
    g = rig.globals(pose)
    rig.set_global_rotation(pose, g, foot, old_foot_rotation)  # keep the foot flat as in the source


def crouch_pose(rig, pose):
    source = rig.globals(pose)
    feet = {side: source[rig.index[f"foot.{side}"]][0] for side in ("l", "r")}
    hips = rig.index["hips"]
    t = pose[hips][0]
    pose[hips][0] = (t[0], t[1] - CROUCH_DROP, t[2])
    # Knees bend around the hips' side axis (local X), so they point where the hips face.
    bend_axis = qrotate(source[hips][1], (1.0, 0.0, 0.0))
    for side in ("l", "r"):
        solve_leg(rig, pose, side, feet[side], bend_axis)
    # Lean forward from the spine, keep the eyes level.
    for bone, angle in (("spine", CROUCH_LEAN * 0.6), ("chest", CROUCH_LEAN * 0.4), ("head", -CROUCH_LEAN * 0.7)):
        node = rig.index[bone]
        pose[node][1] = qnormalize(qmul(qaxis((1.0, 0.0, 0.0), angle), pose[node][1]))
    return pose


def look_pose(rig, pose, time, duration):
    yaw = LOOK_YAW * math.sin(2.0 * math.pi * time / duration)
    for bone, share in (("chest", 0.3), ("head", 0.7)):
        node = rig.index[bone]
        pose[node][1] = qnormalize(qmul(qaxis((0.0, 1.0, 0.0), yaw * share), pose[node][1]))
    return pose


def bake(rig, name, duration, pose_at):
    """Samples pose_at(time) at FPS over [0, duration] and appends it as a new animation."""
    frames = max(int(round(duration * FPS)), 1)
    times = [duration * i / frames for i in range(frames + 1)]
    poses = [pose_at(t) for t in times]
    glb = rig.glb
    input_accessor = glb.add_accessor(times, "SCALAR")
    animation = {"name": name, "channels": [], "samplers": []}
    for node in range(len(rig.nodes)):
        for slot, path, kind in ((0, "translation", "VEC3"), (1, "rotation", "VEC4"), (2, "scale", "VEC3")):
            values = [pose[node][slot] for pose in poses]
            if all(max(abs(a - b) for a, b in zip(v, rig.rest[node][slot])) < 1e-6 for v in values):
                continue  # matches the rest pose: nothing to animate
            # Round so regenerating on another machine yields byte-identical files.
            values = [tuple(round(c, 6) for c in v) for v in values]
            animation["samplers"].append({"input": input_accessor, "output": glb.add_accessor(values, kind), "interpolation": "LINEAR"})
            animation["channels"].append({"sampler": len(animation["samplers"]) - 1, "target": {"node": node, "path": path}})
    glb.json["animations"].append(animation)


def author_clips(rig):
    for name, source in (("Crouch_Idle", "Idle"), ("Crouch_Walk", "Walking_A")):
        channels, duration = rig.clip(source)
        bake(rig, name, duration, lambda t, c=channels: crouch_pose(rig, rig.sample(c, t)))
    channels, idle = rig.clip("Idle")
    look_duration = idle * 4
    bake(rig, "Look_Around", look_duration, lambda t: look_pose(rig, rig.sample(channels, t % idle), t, look_duration))


# --------------------------------------------------------------------------------------------
# Texture recolouring (the pack's rogue wears bright green; a thief wants charcoal)
# --------------------------------------------------------------------------------------------


def png_decode(data):
    """Decodes an 8-bit RGB/RGBA PNG into (width, height, channels, bytearray of pixels)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    offset, idat, header = 8, b"", None
    while offset < len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        chunk = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat += chunk
        offset += 12 + length
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or color not in (2, 6) or interlace:
        raise ValueError("only 8-bit, non-interlaced RGB/RGBA PNGs are supported")
    channels = 4 if color == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels
    pixels = bytearray(height * stride)
    previous = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        pixels[y * stride:(y + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def png_encode(width, height, channels, pixels):
    stride = width * channels
    raw = b"".join(b"\x00" + bytes(pixels[y * stride:(y + 1) * stride]) for y in range(height))

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6 if channels == 4 else 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def recolor_greens(glb, tint):
    """Replaces green hues in the embedded colour atlas with `tint` scaled by the original brightness."""
    image = glb.json["images"][0]
    view = glb.json["bufferViews"][image["bufferView"]]
    start = view.get("byteOffset", 0)
    width, height, channels, pixels = png_decode(bytes(glb.bin[start:start + view["byteLength"]]))
    for i in range(0, len(pixels), channels):
        r, g, b = pixels[i] / 255.0, pixels[i + 1] / 255.0, pixels[i + 2] / 255.0
        if g > r * 1.3 and g > b * 1.05 and g - min(r, b) > 0.15:
            luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b
            for c in range(3):
                pixels[i + c] = min(int(round(tint[c] * luminance * 255.0)), 255)
    encoded = png_encode(width, height, channels, pixels)
    # Append the new image and point the image at it (the old bytes stay unused in the buffer).
    while len(glb.bin) % 4:
        glb.bin.append(0)
    glb.json["bufferViews"].append({"buffer": 0, "byteOffset": len(glb.bin), "byteLength": len(encoded)})
    glb.bin += encoded
    image["bufferView"] = len(glb.json["bufferViews"]) - 1


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    source = Path(sys.argv[1]) / SOURCE_SUBDIR
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for output, (input_name, tint) in CHARACTERS.items():
        glb = Glb(source / input_name)
        author_clips(Rig(glb))
        if tint:
            recolor_greens(glb, tint)
        glb.json.setdefault("asset", {})["extras"] = {
            "source": f"KayKit Character Pack: Adventurers 1.0 by Kay Lousberg - {input_name} (CC0 1.0)",
            "modified": "Penumbra Tools/ImportCharacters.py: added Crouch_Idle, Crouch_Walk, Look_Around" + (", recoloured cloth" if tint else "")}
        glb.save(OUTPUT / output)
        print(f"{output}: {len(glb.json['animations'])} clips, {(OUTPUT / output).stat().st_size // 1024} KiB")
    shutil.copyfile(Path(sys.argv[1]) / "addons/kaykit_character_pack_adventures/LICENSE.txt", OUTPUT / "KayKit_LICENSE.txt")


if __name__ == "__main__":
    main()
