#!/usr/bin/env python3
"""Writes stand-in rover meshes (binary glTF) for the viewport until the real ones land in fixtures/rover/meshes/.

    python3 make_testmeshes.py [out_dir]      # default: this script's directory

One .glb per rover part slug (chassis-tub.glb, wheel-l.glb, ...). Geometry is modelled in the part's own frame
(mm, Z up, the frame of Part.bounds in the rover fixture) and written the way build123d's exporter does it (S8):
glTF conventions, metres, +Y up. Only numpy is needed. Primitives: filleted boxes (rounded rectangle rings with
edge fillets, analytic normals) and lathed profiles; materials use glTF pbrMetallicRoughness factors. A material
whose base colour is the glTF default white means "use Part.color" in the viewport.
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

PART = None  # sentinel colour: take the part's own colour


def srgb(hexcol):
    c = [int(hexcol[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c] + [1.0]


# name -> (base colour (linear RGBA) or PART, roughness, metallic)
MATERIALS = {
    "body": (PART, 0.55, 0.0),        # printed plastic in the part's colour
    "white": (srgb("#f2f2f0"), 0.5, 0.0),
    "rubber": (srgb("#232529"), 0.85, 0.0),
    "hub": (srgb("#d7d9dc"), 0.5, 0.0),
    "dark": (srgb("#1b1c1f"), 0.6, 0.0),
    "glass": (srgb("#0d1a2b"), 0.08, 0.0),
    "lensring": (srgb("#2c2f34"), 0.35, 0.6),
    "steel": (srgb("#b9bcc1"), 0.3, 1.0),
    "brass": (srgb("#c8a24a"), 0.35, 1.0),
    "gold": (srgb("#d9b25a"), 0.3, 1.0),
    "pcb": (srgb("#1f8a5b"), 0.45, 0.0),
    "chip": (srgb("#16171a"), 0.45, 0.0),
    "yellow": (srgb("#f2b705"), 0.5, 0.0),
    "label": (srgb("#2a2c30"), 0.6, 0.0),
    "blue": (srgb("#2f6fe8"), 0.5, 0.0),
    "red": (srgb("#e8402f"), 0.5, 0.0),
    "terminal": (srgb("#2f6fe8"), 0.5, 0.0),
}


class Mesh:
    def __init__(self):
        self.parts = {}  # material -> (positions list, normals list, indices list)

    def add(self, mat, pos, nrm, tris):
        P, N, I = self.parts.setdefault(mat, ([], [], []))
        base = sum(len(p) for p in P)
        # Make every triangle wind counter-clockwise seen from outside (its vertex normals point outside).
        tris = np.asarray(tris, dtype=np.int64)
        a, b, c = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
        gn = np.cross(b - a, c - a)
        vn = nrm[tris[:, 0]] + nrm[tris[:, 1]] + nrm[tris[:, 2]]
        flip = (gn * vn).sum(axis=1) < 0
        tris[flip] = tris[flip][:, [0, 2, 1]]
        # Drop degenerate triangles (collapsed fillet poles).
        keep = np.linalg.norm(gn, axis=1) > 1e-9
        P.append(pos)
        N.append(nrm)
        I.append(tris[keep] + base)


def xform(pos, nrm, R=None, t=(0, 0, 0)):
    R = np.eye(3) if R is None else np.asarray(R, float)
    return pos @ R.T + np.asarray(t, float), nrm @ R.T


def rot(axis, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    if axis == "x":
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == "y":
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def rrect(hx, hy, r, seg=6):
    """Rounded rectangle outline (counter-clockwise): points and outward 2D normals."""
    r = max(0.0, min(r, hx, hy))
    pts, nrm = [], []
    for k, (cx, cy) in enumerate([(hx - r, hy - r), (-hx + r, hy - r), (-hx + r, -hy + r), (hx - r, -hy + r)]):
        for i in range(seg + 1):
            a = math.pi / 2 * (k + i / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
            nrm.append((math.cos(a), math.sin(a)))
    return np.array(pts), np.array(nrm)


def fbox(m, mat, size, R_corner=3.0, r_edge=1.0, center=(0, 0, 0), R=None, seg=None, fseg=None, top=True, bottom=True,
         inward=False):
    """Filleted box: vertical corner radius R_corner, top/bottom edge fillet r_edge, analytic smooth normals.
    inward=True turns it inside out (a cavity). Returns the top ring (points) for joining to other surfaces."""
    small = min(size) < 4
    seg = seg or (2 if small else 5)
    fseg = fseg or (1 if small else 3)
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    r_edge = min(r_edge, R_corner if R_corner > 0 else r_edge, hx, hy, hz)
    R_corner = max(R_corner, r_edge)
    # Profile: (inset, z, nz-angle) from the bottom centre up to the top centre.
    prof = []
    for i in range(fseg + 1):  # bottom fillet: from the bottom face to the wall
        a = -math.pi / 2 + math.pi / 2 * i / fseg
        prof.append((r_edge - r_edge * math.cos(a), -hz + r_edge + r_edge * math.sin(a), a))
    for i in range(fseg + 1):  # top fillet
        a = math.pi / 2 * i / fseg
        prof.append((r_edge - r_edge * math.cos(a), hz - r_edge + r_edge * math.sin(a), a))
    rings_p, rings_n = [], []
    for inset, z, a in prof:
        pts, n2 = rrect(hx - inset, hy - inset, R_corner - inset, seg)
        p = np.c_[pts, np.full(len(pts), z)]
        n = np.c_[n2 * math.cos(a), np.full(len(pts), math.sin(a))]
        rings_p.append(p)
        rings_n.append(n)
    npt = len(rings_p[0])
    pos = np.concatenate(rings_p)
    nrm = np.concatenate(rings_n)
    tris = []
    for r in range(len(prof) - 1):
        for i in range(npt):
            j = (i + 1) % npt
            a0, a1, b0, b1 = r * npt + i, r * npt + j, (r + 1) * npt + i, (r + 1) * npt + j
            tris += [(a0, a1, b1), (a0, b1, b0)]
    # Caps: flat faces with their own vertices so the normals stay sharp.
    for cap, ring, nz in ((bottom, 0, -1), (top, len(prof) - 1, 1)):
        if not cap:
            continue
        base = len(pos)
        p = rings_p[ring]
        pos = np.vstack([pos, p, [[0, 0, p[0, 2]]]])
        nrm = np.vstack([nrm, np.tile([0, 0, nz], (npt + 1, 1))])
        c = base + npt
        tris += [(c, base + i, base + (i + 1) % npt) for i in range(npt)]
    if inward:
        nrm = -nrm
    pos, nrm = xform(pos, nrm, R, center)
    m.add(mat, pos, nrm, tris)
    return xform(rings_p[-1], rings_n[-1], R, center)[0]


def lathe(m, mat, profile, seg=48, R=None, t=(0, 0, 0), smooth=True):
    """Revolve a (radius, z) profile around +Z. Sharp corners where the profile repeats a point."""
    prof = [(float(r), float(z)) for r, z in profile]
    pos, nrm, tris = [], [], []
    # Split the profile into smooth runs at repeated points (hard edges).
    runs, cur = [], [prof[0]]
    for p in prof[1:]:
        if p == cur[-1]:
            runs.append(cur)
            cur = [p]
        else:
            cur.append(p)
    runs.append(cur)
    for run in runs:
        if len(run) < 2:
            continue
        base = len(pos)
        # 2D normals of the run (outward = right of travel direction when z goes up along r>0 side).
        n2 = []
        for i in range(len(run)):
            a = run[max(i - 1, 0)]
            b = run[min(i + 1, len(run) - 1)]
            dr, dz = b[0] - a[0], b[1] - a[1]
            ln = math.hypot(dr, dz) or 1
            n2.append((dz / ln, -dr / ln))
        for (r, z), (nr, nz) in zip(run, n2):
            for k in range(seg + 1):
                a = 2 * math.pi * k / seg
                pos.append((r * math.cos(a), r * math.sin(a), z))
                nrm.append((nr * math.cos(a), nr * math.sin(a), nz))
        for i in range(len(run) - 1):
            for k in range(seg):
                a0 = base + i * (seg + 1) + k
                b0 = a0 + seg + 1
                tris += [(a0, a0 + 1, b0 + 1), (a0, b0 + 1, b0)]
    pos, nrm = np.array(pos, float), np.array(nrm, float)
    pos, nrm = xform(pos, nrm, R, t)
    m.add(mat, pos, nrm, tris)


def cylinder(m, mat, radius, z0, z1, R=None, t=(0, 0, 0), seg=32, chamfer=0.0):
    c = min(chamfer, radius / 2, (z1 - z0) / 2)
    prof = [(0, z0), (radius - c, z0), (radius - c, z0)]
    if c:
        prof += [(radius, z0 + c), (radius, z0 + c)]
    prof += [(radius, z1 - c), (radius, z1 - c)]
    if c:
        prof += [(radius - c, z1), (radius - c, z1)]
    prof += [(0, z1)]
    # Cap normals: run from centre to rim on the bottom faces down, on the top up.
    lathe(m, mat, prof, seg, R, t)


# ---- parts (frames and sizes follow fixtures/rover/fulmetry.project.json bounds) -----------------------------

def chassis_tub():
    m = Mesh()
    outer = fbox(m, "body", (160, 120, 40), R_corner=12, r_edge=4, top=False)
    # Open top: an inner wall and a floor so the tub reads as a shell when exploded, joined by a flat rim.
    inner = fbox(m, "body", (153, 113, 36.5), R_corner=8.5, r_edge=2.5, center=(0, 0, 1.75), top=False, inward=True)
    n = len(outer)
    pos = np.vstack([outer, inner])
    pos[:, 2] = 20.0
    m.add("body", pos, np.tile([0.0, 0.0, 1.0], (2 * n, 1)),
          [t for i in range(n) for t in ((i, (i + 1) % n, n + (i + 1) % n), (i, n + (i + 1) % n, n + i))])
    for x in (-50, -30, -10, 10, 30, 50):  # side vent slots
        for y, s in ((60.3, 1), (-60.3, -1)):
            fbox(m, "dark", (12, 0.8, 4), R_corner=0.4, r_edge=0.4, center=(x, y, -6))
    for x, y in ((-70, 50), (70, 50), (-70, -50), (70, -50)):  # corner bosses for the shell screws
        cylinder(m, "body", 4.5, -18, 20, t=(x, y, 0), seg=24)
    return m


def top_shell():
    m = Mesh()
    fbox(m, "body", (164, 124, 18), R_corner=14, r_edge=6, center=(0, 0, -3))
    fbox(m, "body", (136, 96, 6), R_corner=10, r_edge=3, center=(0, 0, 8.5))  # raised deck
    for x, y in ((-72, 52), (72, 52), (-72, -52), (72, -52)):  # countersunk screw heads
        cylinder(m, "steel", 3.2, 5.6, 6.3, t=(x, y, 0), seg=24, chamfer=0.4)
        fbox(m, "dark", (2.6, 0.8, 0.4), 0.2, 0.1, center=(x, y, 6.4))
        fbox(m, "dark", (0.8, 2.6, 0.4), 0.2, 0.1, center=(x, y, 6.4))
    for i in range(7):  # cooling grille on the deck
        fbox(m, "dark", (3, 30, 1.2), R_corner=1.5, r_edge=0.5, center=(-60 + i * 7, 0, 11.2))
    cylinder(m, "dark", 9, 11.0, 11.8, t=(50, 0, 0), seg=40)  # pan servo opening
    fbox(m, "red", (8, 4, 1.2), 1, 0.4, center=(-20, -40, 11.4))  # power LED window
    return m


def camera_head():
    m = Mesh()
    fbox(m, "body", (40, 36, 30), R_corner=4, r_edge=2.5)
    # Lens on the front (+x): bezel, barrel, glass.
    Rx = rot("y", 90)
    cylinder(m, "lensring", 11, 0, 4, R=Rx, t=(20, 0, 1), seg=48, chamfer=0.8)
    cylinder(m, "dark", 8.5, 0, 6, R=Rx, t=(20, 0, 1), seg=48, chamfer=0.6)
    cylinder(m, "glass", 6.2, 0, 6.4, R=Rx, t=(20, 0, 1), seg=48, chamfer=0.3)
    fbox(m, "yellow", (1.2, 5, 5), 0.6, 0.4, center=(20.3, -12, 9))  # status light
    for y, z in ((12, -9), (-12, -9)):  # IR LEDs
        cylinder(m, "dark", 2.2, 0, 1.2, R=Rx, t=(20, y, z), seg=20, chamfer=0.3)
    for x, y in ((-12, 0), (8, 0)):  # top screws
        cylinder(m, "dark", 1.8, 14.6, 15.4, t=(x, y, 0), seg=20)
    return m


def wheel(side):
    """Axle along y; the hub face points away from the chassis (+y for the left wheel)."""
    m = Mesh()
    s = 1 if side == "l" else -1
    Ry = rot("x", -90 * s)  # lathe +Z -> +y (left) or -y (right)
    # Tyre carcass (rubber), rounded shoulders.
    tyre = [(22, -6.5), (26.0, -6.5), (27.6, -5.6), (28.2, -4.0), (28.2, 4.0), (27.6, 5.6), (26.0, 6.5), (22, 6.5)]
    lathe(m, "rubber", tyre, seg=78, R=Ry)
    # Tread: staggered lugs (chevron-like) around the tyre.
    n = 26
    for k in range(n):
        a = 360.0 * k / n
        for half in (-1, 1):
            off = 360.0 / n / 2 if half > 0 else 0
            ang = math.radians(a + off)
            Rk = rot("y", -(a + off))  # local x -> radial, around the axle (y)
            Rt = Rk @ rot("x", 22 * half)  # chevron: tilt about the radial axis
            c = np.array([math.cos(ang) * 28.9, 2.7 * half, math.sin(ang) * 28.9])
            # local frame: x radial (lug height), y along the axle, z tangential
            fbox(m, "rubber", (2.0, 5.6, 4.0), R_corner=0.6, r_edge=0.5, center=c, R=Rt, seg=2, fseg=1)
    # Rim and hub (printed, light grey).
    rim = [(14, -5.2), (21.5, -5.2), (22.2, -4.6), (22.2, 4.6), (21.5, 5.2), (14, 5.2), (14, 5.2), (14, -5.2)]
    lathe(m, "hub", rim, seg=96, R=Ry)
    lathe(m, "hub", [(0, -3.0), (14, -3.0), (14, -3.0), (14, 3.0), (14, 3.0), (0, 3.0)], seg=64, R=Ry)  # web
    for k in range(5):  # spokes
        a = 72.0 * k
        Rk = rot("y", a)
        fbox(m, "hub", (12, 3.6, 4.2), R_corner=1.4, r_edge=1.0, center=Rk @ np.array([12.5, s * 4.6, 0]), R=Rk)
        hole = rot("y", a + 36) @ np.array([9.5, 0, 0])  # lightening pockets between the spokes
        hole[1] = s * 3.0
        cylinder(m, "dark", 3.4, 0, 0.4, R=Ry, t=hole, seg=24)
    lathe(m, "hub", [(6.6, 3.0), (6.6, 6.4), (5.8, 7.2), (0, 7.2)], seg=48, R=Ry)  # hub boss
    cylinder(m, "steel", 2.2, 0, 7.6, R=Ry, seg=6)  # hex nut on the axle
    cylinder(m, "dark", 1.4, 0, 7.8, R=Ry, seg=20)
    return m


def motor(side):
    """N20-style gear motor; shaft along y towards the wheel."""
    m = Mesh()
    s = 1 if side == "l" else -1
    Ry = rot("x", -90 * s)
    fbox(m, "steel", (12, 10, 10), R_corner=0.6, r_edge=0.6, center=(0, s * 5, 0))        # gearbox frame
    fbox(m, "brass", (10, 8, 8), R_corner=0.5, r_edge=0.4, center=(0, s * 5, 0))
    cylinder(m, "steel", 5.8, -10, 0, R=Ry, seg=40, chamfer=0.6)                       # can
    cylinder(m, "dark", 4.0, -10.6, -10, R=Ry, seg=32)                                # end cap
    cylinder(m, "steel", 1.5, 10, 16, R=Ry, seg=16)                                    # D shaft
    for z in (-2.4, 2.4):
        fbox(m, "gold", (1.6, 2.4, 0.4), 0.2, 0.1, center=(0, -s * 9.8, z))           # tabs
    return m


def pan_servo():
    m = Mesh()
    fbox(m, "body", (23, 12, 22), R_corner=1.2, r_edge=0.8, center=(0, 0, -1))
    fbox(m, "body", (32, 12, 2.4), R_corner=1.0, r_edge=0.5, center=(0, 0, 5))      # mounting flange
    for x in (-14, 14):
        cylinder(m, "dark", 1.2, 6.2, 6.4, t=(x, 0, 0), seg=16)
    cylinder(m, "body", 5.8, 10, 12.5, t=(5.5, 0, 0), seg=40, chamfer=0.4)           # gear dome
    cylinder(m, "white", 2.4, 12.5, 15.0, t=(5.5, 0, 0), seg=24, chamfer=0.3)       # spline
    fbox(m, "label", (16, 0.4, 8), 0.4, 0.2, center=(0, -6.1, -3))                  # label
    return m


def board(w, d, chips):
    m = Mesh()
    fbox(m, "body", (w, d, 1.6), R_corner=2.0, r_edge=0.3, center=(0, 0, -1.2))
    for x, y in ((w / 2 - 3, d / 2 - 3), (-w / 2 + 3, d / 2 - 3), (w / 2 - 3, -d / 2 + 3), (-w / 2 + 3, -d / 2 + 3)):
        cylinder(m, "gold", 1.6, -0.39, -0.35, t=(x, y, 0), seg=16)
    for mat, sx, sy, sz, x, y in chips:
        fbox(m, mat, (sx, sy, sz), R_corner=0.3, r_edge=0.2, center=(x, y, -0.4 + sz / 2))
    return m


def main_board():
    chips = [("steel", 18, 25, 3.2, 12, 4), ("chip", 7, 7, 1.2, -8, -6), ("chip", 4, 4, 1.0, -8, 6),
             ("steel", 8, 6, 3.0, -24, 0), ("chip", 3, 2, 1.0, 0, -14)]
    m = board(60, 45, chips)
    for i in range(12):  # header row
        fbox(m, "chip", (2.5, 2.5, 2.5), 0.2, 0.15, center=(-14 + i * 2.54, 19.5, 0.85))
        fbox(m, "gold", (0.64, 0.64, 2.4), 0.1, 0.05, center=(-14 + i * 2.54, 19.5, 3.2))
    return m


def motor_driver():
    m = board(30, 25, [("chip", 7, 9, 1.4, -4, 0), ("chip", 3, 2, 1.0, 6, -6)])
    for y in (-6, 6):
        fbox(m, "terminal", (7, 8, 6), 0.4, 0.3, center=(10.5, y, 2.6))
        cylinder(m, "steel", 1.6, 5.4, 5.8, t=(10.5, y, 0), seg=16)
    return m


def battery():
    m = Mesh()
    fbox(m, "body", (70, 35, 20), R_corner=6, r_edge=3)
    fbox(m, "label", (44, 35.4, 20.4), R_corner=6, r_edge=3)  # wrap label band
    fbox(m, "white", (14, 0.4, 6), 0.4, 0.2, center=(0, -17.8, 0))
    fbox(m, "red", (6, 4, 3), 0.6, 0.5, center=(36, 6, -4))   # leads
    fbox(m, "dark", (6, 4, 3), 0.6, 0.5, center=(36, -1, -4))
    return m


def ball_caster():
    m = Mesh()
    sph = [(0, -8)] + [(7.0 * math.sin(math.pi * i / 24), -7.0 * math.cos(math.pi * i / 24) - 1) for i in range(1, 24)]
    sph += [(0, 6)]
    lathe(m, "steel", [(r, z) for r, z in sph], seg=48)
    lathe(m, "dark", [(0, -1.0), (6.5, -1.0), (6.5, -1.0), (7.8, 1.0), (7.8, 6.8), (6.0, 8), (0, 8)], seg=48)
    return m


def fasteners():
    m = Mesh()
    for x in (-72, 72):
        for y in (-52, 52):
            cylinder(m, "steel", 2.9, -2, 2, t=(x, y, 0), seg=6, chamfer=0.3)
            cylinder(m, "steel", 1.4, -2, 0, t=(x, y, 0), seg=16)
    for x in (-36, 0, 36):
        for y in (-52, 52):
            cylinder(m, "steel", 2.2, -2, 2, t=(x, y, 0), seg=6, chamfer=0.3)
    return m


PARTS = {
    "chassis-tub": chassis_tub,
    "top-shell": top_shell,
    "camera-head": camera_head,
    "wheel-l": lambda: wheel("l"),
    "wheel-r": lambda: wheel("r"),
    "motor-l": lambda: motor("l"),
    "motor-r": lambda: motor("r"),
    "pan-servo": pan_servo,
    "main-board": main_board,
    "motor-driver": motor_driver,
    "battery": battery,
    "ball-caster": ball_caster,
    "fasteners": fasteners,
}


# ---- glTF writer --------------------------------------------------------------------------------------------

def write_glb(mesh, path):
    # Part frame (mm, Z up) -> glTF (m, Y up): (x, y, z) -> (x, z, -y) / 1000, like build123d's exporter.
    to_gltf = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)
    bin_ = bytearray()
    views, accessors, prims, mats = [], [], [], []

    def add_view(data, target):
        while len(bin_) % 4:
            bin_.append(0)
        views.append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data), "target": target})
        bin_.extend(data)
        return len(views) - 1

    for mat, (P, N, I) in mesh.parts.items():
        pos = (np.concatenate(P) @ to_gltf.T / 1000.0).astype(np.float32)
        nrm = np.concatenate(N) @ to_gltf.T
        nrm = (nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)).astype(np.float32)
        idx = np.concatenate(I).astype(np.uint32).ravel()
        vp = add_view(pos.tobytes(), 34962)
        vn = add_view(nrm.tobytes(), 34962)
        vi = add_view(idx.tobytes(), 34963)
        accessors.append({"bufferView": vp, "componentType": 5126, "count": len(pos), "type": "VEC3",
                          "min": pos.min(axis=0).tolist(), "max": pos.max(axis=0).tolist()})
        accessors.append({"bufferView": vn, "componentType": 5126, "count": len(nrm), "type": "VEC3"})
        accessors.append({"bufferView": vi, "componentType": 5125, "count": len(idx), "type": "SCALAR"})
        col, rough, metal = MATERIALS[mat]
        pbr = {"roughnessFactor": rough, "metallicFactor": metal}
        if col is not PART:
            pbr["baseColorFactor"] = [round(c, 5) for c in col]
        mats.append({"name": mat, "pbrMetallicRoughness": pbr})
        prims.append({"attributes": {"POSITION": len(accessors) - 3, "NORMAL": len(accessors) - 2},
                      "indices": len(accessors) - 1, "material": len(mats) - 1})
    while len(bin_) % 4:
        bin_.append(0)
    doc = {"asset": {"version": "2.0", "generator": "fulmetry make_testmeshes.py"},
           "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": path.stem}],
           "meshes": [{"name": path.stem, "primitives": prims}], "materials": mats,
           "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(bin_)}]}
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(bin_), 0x004E4942) + bytes(bin_)
    path.write_bytes(out)
    tri = sum(len(i) for p in mesh.parts.values() for i in p[2])
    return tri


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in PARTS.items():
        tri = write_glb(fn(), out / f"{name}.glb")
        print(f"{name}.glb: {tri} triangles, {(out / f'{name}.glb').stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
