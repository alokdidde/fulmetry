"""Screws and inserts of the rover, each at its assembled position.

Frame: the same as the top shell's (x, y of the rover; z = 0 at the tub's rim, which the manifest places 50 mm
above the floor), so every fastener sits where it is used:

- 4 x M3 x 10 ISO 7380 button-head screws: lid -> tub corner bosses (±72, ±52).
- 4 x M3 x 5.7 brass heat-set inserts in those bosses, flush with the rim.
- 4 x M3 x 6 button-head screws and 4 x M3 x 4 inserts: main board -> lid standoffs (board hole grid 53 x 38 at x -20).
- 2 x M2 x 8 pan-head (Phillips) self-tappers: SG90 tabs -> turret.
- 4 x M2 x 5 pan-head self-tappers: motor driver -> tub standoffs.
- 2 x M3 x 8 button-head screws: ball caster flange -> tub (from below).
- 4 x M1.6 x 3 pan-head screws: N20 gearboxes -> tub side walls (in 0.6 mm counterbores, behind the wheels).

Steel screws are labelled "steel", inserts "brass" (worker/fulmetry_worker/mesh.py).
"""
from math import cos, radians, sin

from build123d import Axis, Box, Compound, Cylinder, Part, Pos, RegularPolygon, Rot, Sphere, chamfer, extrude

PARAMS = {
    "lid_screws": [72.0, 52.0],
    "lid_top": 3.0,               # lid plate top (the screw heads sit 0.4 lower, in spot-faces)
    "board_x": -20.0,
    "board_holes": [53.0, 38.0],
    "board_top": 9.64,            # standoffs (3 + 5) + 1.6 mm PCB + pad
    "servo_tab_top": 17.5,        # turret top 15 + 2.5 mm tabs
    "servo_case_x": 44.65,
    "servo_tab_holes": 27.8,
    "driver_x": 35.0,
    "driver_holes": [25.0, 20.0],
    "driver_top": -32.36,         # 50 - (12.4 floor + 4 standoffs + 1.2 PCB) + pad
    "caster_x": -65.0,
    "caster_holes": 25.0,
    "caster_flange_bottom": -35.0,
    "motor_wall_y": 60.0,
    "motor_z": -20.0,
}


def _dome(r: float, k: float, rim: float, top_z: float) -> Part:
    """A sphere whose cap passes through the head's top centre and its rim at height `rim` above the bearing face."""
    h = k - rim                      # cap height above the rim
    radius = (r * r + h * h) / (2 * h)
    return Pos(0, 0, top_z - radius) * Sphere(radius)


def button_head(d: float, length: float, top_z: float) -> Part:
    """ISO 7380 button head with hex socket, head top at top_z, shank pointing down."""
    dk, k = {3.0: (5.7, 1.65), 2.0: (3.5, 1.3)}.get(d, (1.9 * d, 0.55 * d))
    head = Pos(0, 0, top_z - k / 2) * Cylinder(dk / 2, k)
    head &= _dome(dk / 2, k, 0.3 * k, top_z)
    hexagon = Pos(0, 0, top_z - 1.0) * extrude(RegularPolygon(1.0 if d == 3 else 0.75, 6), 1.2)
    head -= hexagon
    shank = Pos(0, 0, top_z - k - length / 2) * Cylinder(d / 2 * 0.95, length)
    shank = chamfer(shank.edges().sort_by(Axis.Z)[:1], 0.15 * d)
    return head + shank


def pan_head(d: float, length: float, top_z: float) -> Part:
    """Pan head with a Phillips recess; head top at top_z, shank down."""
    dk, k = 2.0 * d, 0.7 * d
    head = Pos(0, 0, top_z - k / 2) * Cylinder(dk / 2, k)
    head &= _dome(dk / 2, k, 0.6 * k, top_z)
    w = 0.22 * d
    head -= Pos(0, 0, top_z) * (Box(dk * 0.55, w, k) + Box(w, dk * 0.55, k))
    shank = Pos(0, 0, top_z - k - length / 2) * Cylinder(d / 2 * 0.9, length)
    shank = chamfer(shank.edges().sort_by(Axis.Z)[:1], 0.15 * d)
    return head + shank


def insert(d_out: float, length: float, top_z: float, bore: float) -> Part:
    """Knurled brass heat-set insert, flush at top_z."""
    body = Pos(0, 0, top_z - length / 2) * Cylinder(d_out / 2 - 0.15, length)
    for i in range(8):
        a = 360 * i / 8
        body += Pos((d_out / 2 - 0.2) * cos(radians(a)), (d_out / 2 - 0.2) * sin(radians(a)),
                    top_z - length / 2) * Rot(0, 0, a + 30) * Box(0.35, 0.5, length * 0.8)
    body -= Pos(0, 0, top_z - length / 2) * Cylinder(bore / 2, length + 1)
    return body


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    steel, brass = [], []
    lx, ly = p["lid_screws"]
    for x in (-lx, lx):
        for y in (-ly, ly):
            steel.append(Pos(x, y, 0) * button_head(3.0, 10.0, p["lid_top"] - 0.4 + 1.65))
            brass.append(Pos(x, y, 0) * insert(4.6, 5.7, 0.0, 3.0))
    hx, hy = p["board_holes"][0] / 2, p["board_holes"][1] / 2
    for x in (p["board_x"] - hx, p["board_x"] + hx):
        for y in (-hy, hy):
            steel.append(Pos(x, y, 0) * button_head(3.0, 6.0, p["board_top"] + 1.65))
            brass.append(Pos(x, y, 0) * insert(4.6, 4.0, p["lid_top"] + 5.0, 3.0))
    for s in (-1, 1):
        steel.append(Pos(p["servo_case_x"] + s * p["servo_tab_holes"] / 2, 0, 0) *
                     pan_head(2.0, 8.0, p["servo_tab_top"] + 1.4))
    dx, dy = p["driver_holes"][0] / 2, p["driver_holes"][1] / 2
    for x in (p["driver_x"] - dx, p["driver_x"] + dx):
        for y in (-dy, dy):
            steel.append(Pos(x, y, 0) * pan_head(2.0, 5.0, p["driver_top"] + 1.4))
    for s in (-1, 1):
        # Upside down: head below the flange.
        steel.append(Pos(p["caster_x"], s * p["caster_holes"] / 2, p["caster_flange_bottom"]) * Rot(180, 0, 0) *
                     button_head(3.0, 8.0, 1.65))
    for s in (-1, 1):
        for x in (-4.5, 4.5):
            # Heads on the outside of the side walls, shanks pointing into the tub.
            steel.append(Pos(x, s * p["motor_wall_y"], p["motor_z"]) * Rot(-90 * s, 0, 0) *
                         pan_head(1.6, 3.0, 0.52))
    s_part = Compound(children=steel)
    b_part = Compound(children=brass)
    s_part.label, b_part.label = "steel", "brass"
    return Compound(children=[s_part, b_part])
