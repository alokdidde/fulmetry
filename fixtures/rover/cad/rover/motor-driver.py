"""Motor driver: a 30 x 25 mm dual H-bridge breakout (DRV8833 class), as bought, on the tub floor.

Frame: PCB centred on the origin, z = 0 at the underside of the PCB, +X towards the front of the rover. Motor
outputs on two 2-way 5.08 mm screw terminals facing the left (+Y) and right (-Y) motors, logic and power on a
1x6 pin header at the back (-X), a 47 uF SMD electrolytic for the motor supply, and four M2 holes.

Note for the owner: the r3 main-board schematic also carries a DRV8833 (U2). Keep one of the two.
Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from collections import defaultdict

from build123d import (Axis, Box, Compound, Cylinder, Part, Pos, RectangleRounded, Rot, Sketch, chamfer, extrude,
                       fillet)

PARAMS = {
    "length": 30.0,
    "width": 25.0,
    "thickness": 1.2,
    "hole_spacing_x": 25.0,
    "hole_spacing_y": 20.0,
    "hole_diameter": 2.2,
}

T = 1.2


def _box(x, y, z0, dx, dy, dz, r=0.0):
    b = Pos(x, y, z0 + dz / 2) * Box(dx, dy, dz)
    if r > 0:
        b = fillet(b.edges().filter_by(Axis.Z), r)
    return b


def _passive(k, x, y, l, w, h, body="ceramic", rot=0):
    k[body].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(0, 0, T, l * 0.6, w, h))
    for s in (-1, 1):
        k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(s * l * 0.4, 0, T, l * 0.2, w, h))


def _terminal(k, x, y, rot):
    """2-way 5.08 mm PCB screw terminal: green body, wire entries on the -Y face (before rot), slotted screws on top."""
    n, pitch, depth, h = 2, 5.08, 7.6, 10.0
    body = _box(0, 0, T, n * pitch, depth, h - 2.0, 0.0)
    body += _box(0, 0.9, T + h - 2.0, n * pitch, depth - 1.8, 2.0)          # stepped top
    body = chamfer(body.edges().group_by(Axis.Z)[-1].filter_by(Axis.X), 0.5)
    steel = []
    for i in range(n):
        px = (i - (n - 1) / 2) * pitch
        body -= _box(px, -depth / 2, T + 1.4, 3.6, 3.0, 3.4)                # wire entry
        body -= Pos(px, 0.9, T + h - 1.0) * Cylinder(1.6, 3.0)               # screw well
        screw = Pos(px, 0.9, T + h - 1.6) * Cylinder(1.45, 1.2)
        screw -= _box(px, 0.9, T + h - 1.0, 3.2, 0.5, 1.0)
        steel.append(screw)
        steel.append(_box(px, -depth / 2 + 1.2, T + 1.7, 3.0, 1.0, 2.8))   # rising clamp
        k["tin"].append(_box(px, 0.9, -2.5, 1.0, 0.8, 2.6))                # pins below
    k["terminal-green"].append(Pos(x, y, 0) * Rot(0, 0, rot) * body)
    k["steel"].extend(Pos(x, y, 0) * Rot(0, 0, rot) * s for s in steel)


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    global T
    T = p["thickness"]
    L, W = p["length"], p["width"]
    k: dict[str, list] = defaultdict(list)
    pcb = extrude(Sketch() + RectangleRounded(L, W, 1.5), T)
    hx, hy = p["hole_spacing_x"] / 2, p["hole_spacing_y"] / 2
    for x in (-hx, hx):
        for y in (-hy, hy):
            pcb -= Pos(x, y, 0) * Cylinder(p["hole_diameter"] / 2, 10)
            k["tin"].append(Pos(x, y, T + 0.02) * (Cylinder(2.1, 0.04) - Cylinder(p["hole_diameter"] / 2, 1)))
    k["pcb-blue"].append(pcb)

    _terminal(k, 3.0, W / 2 - 7.6 / 2, rot=180)    # motor L (+Y), entries face +Y
    _terminal(k, 3.0, -W / 2 + 7.6 / 2, rot=0)     # motor R (-Y), entries face -Y
    # 1x6 header at the back: VM, GND, AIN1, AIN2, BIN1, BIN2.
    n, pitch = 6, 2.54
    hxp = -L / 2 + 2.2
    k["plastic-black"].append(Pos(hxp, 0, 0) * Rot(0, 0, 90) * _box(0, 0, T, n * pitch, 2.5, 2.5))
    for i in range(n):
        py = (i - (n - 1) / 2) * pitch
        k["gold"].append(_box(hxp, py, -3.0, 0.64, 0.64, 3.0 + T + 2.5 + 6.0))
        k["tin"].append(Pos(hxp, py, -0.5) * Cylinder(0.9, 1.0))
    # Driver IC (HTSSOP-16), its pads and a heat-sinking via field.
    k["ic-black"].append(_box(-6.0, 0.0, T + 0.05, 4.4, 5.0, 0.95, 0.1))
    for s in (-1, 1):
        for i in range(8):
            k["tin"].append(_box(-6.0 + s * 2.5, -2.275 + i * 0.65, T, 0.6, 0.3, 0.15))
    for i in range(3):
        for j in range(3):
            k["tin"].append(Pos(-12.0 + i * 1.0, -1.0 + j * 1.0, T) * (Cylinder(0.3, 0.04) - Cylinder(0.12, 1)))
    # 47 uF / 16 V aluminium SMD electrolytic: black base, aluminium can with a vent cross.
    ex, ey = -7.0, 6.0
    k["plastic-black"].append(_box(ex, ey, T, 6.6, 6.6, 1.0, 0.0))
    can = Pos(ex, ey, T + 1.0 + 2.2) * Cylinder(3.15, 4.4)
    can = fillet(can.edges().sort_by(Axis.Z)[-1:], 0.5)
    can -= Pos(ex, ey, T + 5.4) * (Box(4.0, 0.3, 0.3) + Box(0.3, 4.0, 0.3))
    k["aluminium"].append(can)
    k["plastic-black"].append(Pos(ex - 1.6, ey, T + 5.38) * Box(1.6, 3.8, 0.05))  # polarity band
    for (x, y, r) in ((-1.0, 2.5, 90), (-1.0, -2.5, 90), (-10.5, -6.5, 0), (-6.0, -6.5, 0), (-1.5, 7.0, 0)):
        _passive(k, x, y, 2.0, 1.25, 0.8, rot=r)
    k["led-amber"].append(_box(-6.5, -9.5, T, 1.6, 0.8, 0.6))   # power LED
    _passive(k, -3.5, -9.5, 1.6, 0.8, 0.45, body="ic-black")

    children = []
    for key, solids in k.items():
        solid = solids[0] if len(solids) == 1 else Compound(children=solids)
        solid.label = key
        children.append(solid)
    return Compound(children=children)
