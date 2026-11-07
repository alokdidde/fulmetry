"""Remote board: the remote's 90 x 50 mm carrier PCB with an ESP32-C3-MINI-1, LiPo charger and power switch.

Frame: PCB centred on the origin (the remote's x, y), z = 0 at its underside. ESP-NOW radio module with its PCB
antenna at the +Y edge, USB-C on the +Y edge for charging and flashing, a TP4054-class charger (SOT-23-5) and LDO,
a JST-PH socket for the LiPo pouch, a slide power switch on the -Y edge, charge/status LEDs, 0603 passives and four
M2 holes on an 82 x 42 mm grid. The joystick, OLED and buttons are separate parts that plug into it.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from collections import defaultdict

from build123d import (Axis, Box, Compound, Cylinder, Part, Pos, RectangleRounded, Rot, Sketch, SlotCenterToCenter,
                       extrude, fillet)

PARAMS = {
    "length": 90.0,
    "width": 50.0,
    "thickness": 1.6,
    "corner_radius": 6.0,
    "hole_spacing": [82.0, 42.0],
    "hole_diameter": 2.2,
}

T = 1.6


def _box(x, y, z0, dx, dy, dz, r=0.0):
    b = Pos(x, y, z0 + dz / 2) * Box(dx, dy, dz)
    if r > 0:
        b = fillet(b.edges().filter_by(Axis.Z), r)
    return b


def _passive(k, x, y, l, w, h, body="ceramic", rot=0):
    k[body].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(0, 0, T, l * 0.6, w, h))
    for s in (-1, 1):
        k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(s * l * 0.4, 0, T, l * 0.2, w, h))


def _sot23(k, x, y, rot=0):
    k["ic-black"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(0, 0, T + 0.05, 2.9, 1.6, 1.05, 0.1))
    for sy, xs in ((-1, (-0.95, 0.0, 0.95)), (1, (-0.95, 0.95))):
        for px in xs:
            k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(px, sy * 1.15, T, 0.4, 0.7, 0.15))


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    global T
    T = p["thickness"]
    L, W = p["length"], p["width"]
    k: dict[str, list] = defaultdict(list)
    pcb = extrude(Sketch() + RectangleRounded(L, W, p["corner_radius"]), T)
    hx, hy = p["hole_spacing"][0] / 2, p["hole_spacing"][1] / 2
    for x in (-hx, hx):
        for y in (-hy, hy):
            pcb -= Pos(x, y, 0) * Cylinder(p["hole_diameter"] / 2, 10)
            k["gold"].append(Pos(x, y, T + 0.02) * (Cylinder(2.2, 0.04) - Cylinder(p["hole_diameter"] / 2, 1)))
    # Joystick, OLED header and button footprints (plated holes) so the plugged-in parts have somewhere to land.
    for i in range(5):
        k["tin"].append(Pos(-32.0 - 5.08 + i * 2.54, -8.5, T) * (Cylinder(0.75, 0.04) - Cylinder(0.45, 1)))
    k["pcb-green"].append(pcb)

    # ESP32-C3-MINI-1: 13.2 x 16.6 x 2.45 mm, antenna end at +Y.
    mx, my = 15.0, 15.6
    k["pcb-black"].append(_box(mx, my, T, 13.2, 16.6, 0.8))
    k["tin"].append(_box(mx, my - 2.2, T + 0.8, 12.0, 11.4, 1.65, 0.3))
    for i in range(6):
        k["gold"].append(_box(mx - 4.4 + i * 1.76, my + 5.3, T + 0.8, 0.5, 4.0, 0.04))
    k["gold"].append(_box(mx, my + 7.3, T + 0.8, 9.8, 0.5, 0.04))
    # USB-C on the +Y edge.
    depth = 7.35
    zc = T + 1.63
    usb = Pos(0, W / 2 + 1.0 - depth, zc) * Rot(-90, 0, 0) * extrude(Rot(0, 0, 0) * SlotCenterToCenter(8.94 - 3.26,
                                                                                                     3.26), depth)
    usb -= Pos(0, W / 2 + 1.1 - 5.0, zc) * Rot(-90, 0, 0) * extrude(SlotCenterToCenter(8.94 - 3.26, 2.66), 5.0)
    k["tin"].append(usb)
    k["plastic-black"].append(_box(0, W / 2 + 1.0 - 2.6, T + 1.28, 6.7, 5.0, 0.7))
    # Slide switch on the -Y edge (SS12D00): steel body, black lever poking out of the shell.
    k["tin"].append(_box(12.0, -W / 2 + 2.0, T, 8.6, 3.6, 3.5))
    k["plastic-black"].append(_box(12.0 + 1.5, -W / 2 - 2.2, T + 1.0, 1.4, 4.4, 1.6))
    # Charger and LDO, battery socket, LEDs and passives.
    _sot23(k, -8.0, 14.0)
    _sot23(k, -14.0, 14.0, rot=90)
    k["nylon-white"].append(_box(-6.0, -19.0, T, 5.9, 4.5, 6.0, 0.2) - _box(-6.0, -18.75, T + 1.5, 4.9, 3.0, 6.0))
    for x in (-7.0, -5.0):
        k["gold"].append(_box(x, -18.75, -1.5, 0.5, 0.5, T + 6.0))
    k["led-green"].append(_box(-2.5, 19.0, T, 1.0, 0.8, 0.5))
    k["led-amber"].append(_box(-2.5, 17.0, T, 1.0, 0.8, 0.5))
    for (x, y, r) in ((-11.0, 18.5, 0), (-11.0, 9.5, 0), (-17.5, 9.5, 90), (-5.0, 9.5, 0), (6.0, 6.0, 90),
                      (6.0, 10.0, 90), (24.0, 6.0, 0), (1.0, 17.0, 0), (1.0, 19.0, 0)):
        _passive(k, x, y, 1.6, 0.8, 0.8, rot=r)
    # A ground-pour edge of stitching vias round the antenna keep-out.
    for i in range(8):
        k["tin"].append(Pos(7.0 + i * 2.2, 5.5, T) * (Cylinder(0.35, 0.04) - Cylinder(0.15, 1)))

    children = []
    for key, solids in k.items():
        solid = solids[0] if len(solids) == 1 else Compound(children=solids)
        solid.label = key
        children.append(solid)
    return Compound(children=children)
