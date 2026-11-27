"""Writes and reads binary glTF (.glb) for props: one node, one mesh, a primitive per material (Python stdlib only).

The materials point at the shared texture sheets in the project's Assets (relative URIs from the .glb), so every
prop using a sheet shares one copy in the engine (AssetManager's texture cache). The output is deterministic: the
same prop gives the same bytes.
"""

import json
import os
import struct

FLOAT, UNSIGNED_SHORT, UNSIGNED_INT = 5126, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
REPEAT, LINEAR, LINEAR_MIPMAP_LINEAR = 10497, 9729, 9987


def _f32(value):
    """The value as float32 stores it (accessor bounds must match the stored data)."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _pad(data, byte=b"\x00"):
    return data + byte * ((4 - len(data) % 4) % 4)


def write(path, name, primitives, materials, assets_dir, generator="Penumbra PropGen"):
    """`primitives`: {material name: (positions, normals, uvs, indices)} (Prop.primitives); `materials`: config.json's
    "materials"; texture paths there are relative to `assets_dir`."""
    path = os.fspath(path)
    gltf = {"asset": {"version": "2.0", "generator": generator}, "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"name": name, "mesh": 0}], "meshes": [{"name": name, "primitives": []}],
            "materials": [], "textures": [], "images": [], "samplers": [], "accessors": [], "bufferViews": [],
            "buffers": []}
    blob = bytearray()
    images = {}

    def view(data, target):
        offset = len(blob)
        blob.extend(_pad(bytes(data)))
        gltf["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        return len(gltf["bufferViews"]) - 1

    def accessor(values, kind, component, target, bounds=False):
        flat = [c for v in values for c in (v if isinstance(v, tuple) else (v,))]
        fmt = {FLOAT: "f", UNSIGNED_SHORT: "H", UNSIGNED_INT: "I"}[component]
        record = {"bufferView": view(struct.pack(f"<{len(flat)}{fmt}", *flat), target), "componentType": component,
                  "count": len(values), "type": kind}
        if bounds:
            width = len(values[0])
            record["min"] = [_f32(min(v[i] for v in values)) for i in range(width)]
            record["max"] = [_f32(max(v[i] for v in values)) for i in range(width)]
        gltf["accessors"].append(record)
        return len(gltf["accessors"]) - 1

    def texture(asset_path):
        if not gltf["samplers"]:
            gltf["samplers"].append({"magFilter": LINEAR, "minFilter": LINEAR_MIPMAP_LINEAR, "wrapS": REPEAT, "wrapT": REPEAT})
        if asset_path not in images:
            # Real paths on both sides: a symlinked folder (macOS's /var) must not change the climb.
            uri = os.path.relpath(os.path.realpath(os.path.join(assets_dir, asset_path)),
                                  os.path.realpath(os.path.dirname(os.path.abspath(path)))).replace(os.sep, "/")
            gltf["images"].append({"uri": uri})
            gltf["textures"].append({"sampler": 0, "source": len(gltf["images"]) - 1})
            images[asset_path] = len(gltf["textures"]) - 1
        return {"index": images[asset_path]}

    for material_name in sorted(primitives):
        positions, normals, uvs, indices = primitives[material_name]
        if not indices:
            continue
        spec = materials[material_name]
        pbr = {"baseColorFactor": list(spec.get("base_color", [1, 1, 1])) + [1.0], "metallicFactor": 1.0,
               "roughnessFactor": 1.0}
        material = {"name": material_name, "pbrMetallicRoughness": pbr}
        if spec.get("albedo"):
            pbr["baseColorTexture"] = texture(spec["albedo"])
        if spec.get("metallic_roughness"):
            pbr["metallicRoughnessTexture"] = texture(spec["metallic_roughness"])
        else:
            pbr["metallicFactor"], pbr["roughnessFactor"] = spec.get("metallic", 0.0), spec.get("roughness", 0.8)
        if spec.get("normal"):
            material["normalTexture"] = texture(spec["normal"])
        gltf["materials"].append(material)
        index_type = UNSIGNED_SHORT if len(positions) < 65536 else UNSIGNED_INT
        gltf["meshes"][0]["primitives"].append({
            "attributes": {"POSITION": accessor(positions, "VEC3", FLOAT, ARRAY_BUFFER, bounds=True),
                           "NORMAL": accessor(normals, "VEC3", FLOAT, ARRAY_BUFFER),
                           "TEXCOORD_0": accessor(uvs, "VEC2", FLOAT, ARRAY_BUFFER)},
            "indices": accessor(indices, "SCALAR", index_type, ELEMENT_ARRAY_BUFFER),
            "material": len(gltf["materials"]) - 1, "mode": 4})

    for key in ("textures", "images", "samplers"):
        if not gltf[key]:
            del gltf[key]
    gltf["buffers"].append({"byteLength": len(blob)})
    text = _pad(json.dumps(gltf, separators=(",", ":"), sort_keys=True).encode("utf-8"), b" ")
    binary = _pad(bytes(blob))
    data = (struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(binary)) +
            struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(binary), 0x004E4942) + binary)
    with open(path, "wb") as f:
        f.write(data)
    return data


def read(path):
    """(the glTF JSON, the binary chunk) of a .glb; raises ValueError for anything that is not one."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 20:
        raise ValueError("too short for a .glb")
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2 or length != len(data):
        raise ValueError("not a glTF 2.0 binary (bad header)")
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise ValueError("the first chunk is not JSON")
    gltf = json.loads(data[20:20 + json_length].decode("utf-8"))
    rest = 20 + json_length
    binary = b""
    if rest + 8 <= len(data):
        bin_length, bin_type = struct.unpack_from("<II", data, rest)
        if bin_type != 0x004E4942:
            raise ValueError("the second chunk is not BIN")
        binary = data[rest + 8:rest + 8 + bin_length]
    return gltf, binary


def read_accessor(gltf, binary, index):
    """The values of one accessor as a list of tuples (or of numbers for scalars)."""
    accessor = gltf["accessors"][index]
    view = gltf["bufferViews"][accessor["bufferView"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    fmt = {FLOAT: "f", UNSIGNED_SHORT: "H", UNSIGNED_INT: "I"}[accessor["componentType"]]
    count = accessor["count"]
    values = struct.unpack_from(f"<{count * width}{fmt}", binary, view.get("byteOffset", 0) + accessor.get("byteOffset", 0))
    if width == 1:
        return list(values)
    return [tuple(values[i:i + width]) for i in range(0, len(values), width)]
