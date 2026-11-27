"""The prop kit: props built from boards, beams, staves and iron, as flat-shaded parts (Tools/PropGen/README.md).

Coordinates are the engine's: metres, Y up, the prop's pivot at the middle of its base. A part is a list of planar
faces with outward normals and UVs; parts carry a material name (config.json "materials") and the lowest level of
detail they belong to (`lods`: (0, 1, 2) in every level, (0,) the full model only: single boards, nails, thin bands;
(1,) or (2,) simpler stand-ins: a side as one slab, the whole crate as one box).

UVs are in metres of the material's texture (`metres_per_repeat`). Wood follows its grain: the texture's boards run
along V, and a board-sized part (`board=True`) is mapped onto one of the texture's boards so the gaps between the
texture's boards fall on the part's edges.
"""

import math
import random


# ------------------------------------------------------------------------------------------------ vectors

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(a):
    length = math.sqrt(dot(a, a))
    return scale(a, 1.0 / length) if length > 0 else a


def rotation(degrees):
    """3x3 rotation (rows) from Euler degrees (x, y, z), applied in the order Z, X, Y (yaw last), like the engine's
    Transform: a part is tilted first, then turned."""
    rx, ry, rz = (math.radians(d) for d in degrees)
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    x = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    y = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    z = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))
    return matmul(y, matmul(x, z))


def matmul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def apply(m, v):
    return (dot(m[0], v), dot(m[1], v), dot(m[2], v))


IDENTITY = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
AXES = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
AXIS = {"x": 0, "y": 1, "z": 2}


# ------------------------------------------------------------------------------------------------ the prop

class Face:
    __slots__ = ("points", "normal", "uvs")

    def __init__(self, points, normal, uvs):
        self.points, self.normal, self.uvs = points, normal, uvs


class Part:
    __slots__ = ("material", "lods", "faces", "name")

    def __init__(self, material, lods, name):
        self.material, self.lods, self.name, self.faces = material, tuple(lods), name, []


class Prop:
    """A prop under construction. `materials` is config.json's "materials"; `seed` fixes every random choice (which
    of the texture's boards a board shows, where a texture starts)."""

    def __init__(self, materials, seed):
        self.materials = materials
        self.random = random.Random(seed)
        self.parts = []
        self.collision = []  # boxes: (centre, half extents), in the prop's space

    # ---------------------------------------------------------------- building blocks

    def box(self, centre, size, material, rotate=(0, 0, 0), grain=None, board=False, lods=(0, 1, 2), name="box"):
        """A box `size` (x, y, z) metres around `centre`, turned by `rotate` (Euler degrees). `grain`: the local axis
        ("x", "y", "z") the wood runs along (default: the longest). `board`: map the part onto one of the texture's
        boards across its width."""
        part = Part(material, lods, name)
        half = [s / 2.0 for s in size]
        grain_axis = AXIS[grain] if grain else max(range(3), key=lambda i: size[i])
        m = rotation(rotate)
        spec = self.materials[material]
        per_metre = 1.0 / spec.get("metres_per_repeat", 1.0)
        boards = spec.get("boards_per_repeat")
        offset = (self.random.random(), self.random.random())
        board_index = self.random.randrange(boards) if boards else 0
        for i in range(3):
            j, k = (i + 1) % 3, (i + 2) % 3
            # V along the grain when the face shows it, else along the face's longer side.
            if grain_axis in (j, k):
                v_axis = grain_axis
            else:
                v_axis = j if size[j] >= size[k] else k
            u_axis = k if v_axis == j else j
            for sign in (1, -1):
                normal = scale(AXES[i], sign)
                corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
                if sign < 0:
                    corners.reverse()
                points, uvs = [], []
                for a, b in corners:
                    local = [0.0, 0.0, 0.0]
                    local[i] = sign * half[i]
                    local[j] = a * half[j]
                    local[k] = b * half[k]
                    points.append(add(centre, apply(m, tuple(local))))
                    u_local, v_local = local[u_axis], local[v_axis]
                    if board and boards and v_axis == grain_axis:
                        u = ((u_local + half[u_axis]) / (2 * half[u_axis]) + board_index) / boards
                    else:
                        u = u_local * per_metre + offset[0]
                    uvs.append((u, v_local * per_metre + offset[1]))
                part.faces.append(Face(points, apply(m, normal), uvs))
        self.parts.append(part)
        return part

    def lathe(self, profile, segments, material, centre=(0, 0, 0), lods=(0, 1, 2), name="lathe", caps=(False, False),
              board=False, phase=0.0):
        """A solid of revolution around the vertical axis through `centre`: `profile` is a list of (radius, height)
        from the bottom up; `segments` flat sides (a barrel's staves when `board`: each side one texture board).
        `caps` closes the bottom / top with a flat disc."""
        part = Part(material, lods, name)
        spec = self.materials[material]
        per_metre = 1.0 / spec.get("metres_per_repeat", 1.0)
        boards = spec.get("boards_per_repeat")
        offset = (self.random.random(), self.random.random())
        angles = [phase + 2 * math.pi * s / segments for s in range(segments + 1)]

        def at(radius, height, angle):
            return (centre[0] + radius * math.cos(angle), centre[1] + height, centre[2] + radius * math.sin(angle))

        for s in range(segments):
            a0, a1 = angles[s], angles[s + 1]
            board_index = self.random.randrange(boards) if boards else 0
            v_offset = self.random.random()
            for (r0, h0), (r1, h1) in zip(profile, profile[1:]):
                # Counter-clockwise seen from outside: bottom a0, bottom a1, top a1, top a0 (angles grow towards -Z
                # when seen from +X, so the order runs a1 before a0 at the bottom).
                points = [at(r0, h0, a1), at(r0, h0, a0), at(r1, h1, a0), at(r1, h1, a1)]
                normal = normalize(cross(sub(points[1], points[0]), sub(points[3], points[0])))
                width0 = 2 * r0 * math.sin((a1 - a0) / 2)
                width1 = 2 * r1 * math.sin((a1 - a0) / 2)
                if board and boards:
                    us = [(1 + board_index) / boards, board_index / boards, board_index / boards, (1 + board_index) / boards]
                else:
                    u0 = (s * width0) * per_metre + offset[0]
                    us = [u0 + width0 * per_metre, u0, u0, u0 + width1 * per_metre]
                vs = [h0 * per_metre + v_offset, h0 * per_metre + v_offset, h1 * per_metre + v_offset, h1 * per_metre + v_offset]
                part.faces.append(Face(points, normal, list(zip(us, vs))))
        for index, closed in enumerate(caps):
            if not closed:
                continue
            radius, height = profile[0] if index == 0 else profile[-1]
            ring = [at(radius, height, a) for a in angles[:-1]]
            normal = (0.0, -1.0, 0.0) if index == 0 else (0.0, 1.0, 0.0)
            if index == 1:
                ring.reverse()
            uvs = [((p[0] - centre[0]) * per_metre + offset[0], (p[2] - centre[2]) * per_metre + offset[1]) for p in ring]
            part.faces.append(Face(ring, normal, uvs))
        self.parts.append(part)
        return part

    def collide(self, centre, half_extents):
        """A collision box (the engine's BoxCollider), in the prop's space."""
        self.collision.append((tuple(centre), tuple(half_extents)))

    # ---------------------------------------------------------------- results

    def triangles(self, lod=0):
        return sum(len(face.points) - 2 for part in self.parts if lod in part.lods for face in part.faces)

    def bounds(self, lod=0):
        points = [p for part in self.parts if lod in part.lods for face in part.faces for p in face.points]
        low = tuple(min(p[i] for p in points) for i in range(3))
        high = tuple(max(p[i] for p in points) for i in range(3))
        return low, high

    def default_collision(self):
        """One box around the whole prop when the family names none."""
        if self.collision:
            return self.collision
        low, high = self.bounds(0)
        return [(tuple((low[i] + high[i]) / 2 for i in range(3)), tuple((high[i] - low[i]) / 2 for i in range(3)))]

    def primitives(self, lod=0):
        """Per material: (positions, normals, uvs, indices) with each face's own vertices (flat shading)."""
        out = {}
        for part in self.parts:
            if lod not in part.lods:
                continue
            positions, normals, uvs, indices = out.setdefault(part.material, ([], [], [], []))
            for face in part.faces:
                first = len(positions)
                for point, uv in zip(face.points, face.uvs):
                    positions.append(point)
                    normals.append(face.normal)
                    uvs.append(uv)
                for n in range(1, len(face.points) - 1):
                    indices.extend((first, first + n, first + n + 1))
        return out
