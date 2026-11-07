"""Main board: the rover's ESP32 carrier (KiCad: fixtures/rover/kicad/rover-main.kicad_sch), populated.

Frame: PCB centred on the origin, +X towards the front of the rover, z = 0 at the underside of the PCB.
Components follow the schematic: ESP32-WROOM-32E (U1), DRV8833PWP dual H-bridge (U2, HTSSOP-16), AP2112K-3.3
LDO (U3, SOT-23-5), three JST-PH 2-pin sockets (battery J1, motors J2/J3), a 1x3 servo header (J4), a green
status LED with its resistor, and 0603 decoupling capacitors. A USB-C programming socket and EN/BOOT buttons are
added as on any ESP32 dev board (not yet in the r3 schematic). Four M3 mounting holes on a 53 x 38 mm grid.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from collections import defaultdict
from math import atan2, degrees

from build123d import (Axis, Box, Compound, Cylinder, Part, Pos, RectangleRounded, Rot, Sketch, SlotCenterToCenter,
                       extrude, fillet)

PARAMS = {
    "length": 60.0,          # X
    "width": 45.0,           # Y
    "thickness": 1.6,
    "corner_radius": 3.0,
    "hole_spacing_x": 53.0,
    "hole_spacing_y": 38.0,
    "hole_diameter": 3.2,
}

T = 1.6  # PCB top, set in build()


def _box(x, y, z0, dx, dy, dz, r=0.0):
    b = Pos(x, y, z0 + dz / 2) * Box(dx, dy, dz)
    if r > 0:
        b = fillet(b.edges().filter_by(Axis.Z), r)
    return b


def _chip(k, x, y, body_l, body_w, h, pins_per_side, pitch, rot=0, lead=0.6, pad=False):
    """A gull-wing IC: epoxy body (ic-black) and tinned leads along its long sides."""
    k["ic-black"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(0, 0, T + 0.05, body_l, body_w, h - 0.05, 0.1))
    k["ic-black"].append(Pos(x, y, 0) * Rot(0, 0, rot) * Pos(-body_l / 2 + 0.6, -body_w / 2 + 0.6,
                                                         T + h) * Cylinder(0.25, 0.04))  # pin-1 dot (shiny)
    span = (pins_per_side - 1) * pitch
    for s in (-1, 1):
        for i in range(pins_per_side):
            px = -span / 2 + i * pitch
            k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(px, s * (body_w / 2 + lead / 2), T, min(pitch * 0.5, 0.45),
                                                                 lead, 0.15))
    if pad:
        k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(0, 0, T, body_l * 0.7, body_w * 0.6, 0.04))


def _passive(k, x, y, l, w, h, body="ceramic", rot=0):
    """An SMD chip resistor/capacitor: body plus two tinned end caps."""
    k[body].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(0, 0, T, l * 0.6, w, h))
    for s in (-1, 1):
        k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(s * l * 0.4, 0, T, l * 0.2, w, h))


def _pin_header(k, x, y, n, rot=0, pitch=2.54, above=6.0, below=3.0):
    k["plastic-black"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(0, 0, T, n * pitch, 2.5, 2.5, 0.0))
    for i in range(n):
        px = (i - (n - 1) / 2) * pitch
        pin = Pos(x, y, 0) * Rot(0, 0, rot) * _box(px, 0, -below, 0.64, 0.64, below + T + 2.5 + above)
        k["gold"].append(pin)
        k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * Pos(px, 0, -0.5) * Cylinder(0.9, 1.0))  # solder joint


def _jst_ph(k, x, y, n=2, rot=0):
    """JST PH vertical socket (B2B-PH-K style): natural nylon shell with an open top and two gold pins."""
    pitch, depth, h = 2.0, 4.5, 6.0
    length = (n - 1) * pitch + 3.9
    body = _box(0, 0, T, length, depth, h, 0.2)
    body -= _box(0, 0.25, T + 1.5, length - 1.0, depth - 1.5, h)
    body -= _box(0, -depth / 2, T + h - 2.5, 1.6, 1.4, 3)  # latch slot
    k["nylon-white"].append(Pos(x, y, 0) * Rot(0, 0, rot) * body)
    for i in range(n):
        px = (i - (n - 1) / 2) * pitch
        k["gold"].append(Pos(x, y, 0) * Rot(0, 0, rot) * _box(px, 0.25, -1.5, 0.5, 0.5, T + 1.5 + h - 1.5))


def _usb_c(k, x, y, rot=0):
    """Mid-mount USB-C receptacle: tin shell (stadium 8.94 x 3.26), black tongue, overhanging the edge by 1 mm."""
    depth = 7.35
    zc = T + 1.63
    shell = Pos(-depth / 2, 0, zc) * Rot(0, 90, 0) * extrude(Rot(0, 0, 90) * SlotCenterToCenter(8.94 - 3.26, 3.26),
                                                             depth)
    shell -= Pos(-depth / 2 - 0.1, 0, zc) * Rot(0, 90, 0) * extrude(Rot(0, 0, 90) * SlotCenterToCenter(8.94 - 3.26,
                                                                                                         2.66), 5.0)
    tongue = _box(-depth / 2 + 2.6, 0, T + 1.28, 5.0, 6.7, 0.7)
    for s in (-1, 1):
        shell += _box(depth / 2 - 1.2, s * 5.0, 0.0 - 0.8, 1.0, 0.6, T + 1.6)  # through-hole shell legs
    k["tin"].append(Pos(x, y, 0) * Rot(0, 0, rot) * shell)
    k["plastic-black"].append(Pos(x, y, 0) * Rot(0, 0, rot) * tongue)


def _tact(k, x, y):
    """3 x 4 mm SMD tact switch: black body, tin cover plate, white plunger."""
    k["plastic-black"].append(_box(x, y, T, 4.0, 3.0, 1.2, 0.2))
    k["tin"].append(_box(x, y, T + 1.2, 3.6, 2.6, 0.15))
    k["nylon-white"].append(Pos(x, y, T + 1.35 + 0.4) * fillet(Box(1.8, 1.2, 0.8).edges().filter_by(Axis.Z), 0.3))
    for sx in (-1, 1):
        k["tin"].append(_box(x + sx * 2.3, y, T, 0.6, 1.6, 0.25))


def _wroom(k, x, y):
    """ESP32-WROOM-32E: 25.5 x 18 x 3.1 mm. Black module PCB with gold castellations, tin RF can, PCB antenna (+X)."""
    L, W = 25.5, 18.0
    k["pcb-black"].append(_box(x, y, T, L, W, 0.8))
    can_l = 17.6
    k["tin"].append(_box(x - L / 2 + 0.7 + can_l / 2, y, T + 0.8, can_l, 15.8, 2.3, 0.3))
    # Castellated pads down both long sides and across the back.
    for s in (-1, 1):
        for i in range(14):
            k["gold"].append(_box(x - L / 2 + 1.5 + i * 1.27, y + s * (W / 2 - 0.4), T + 0.8, 0.6, 0.9, 0.03))
    for i in range(10):
        k["gold"].append(_box(x - L / 2 + 0.4, y - 5.7 + i * 1.27, T + 0.8, 0.9, 0.6, 0.03))
    # Meandered inverted-F antenna on the module's front end.
    ax = x + L / 2 - 3.3
    zig = []
    for i in range(7):
        zig.append(_box(ax - 2.2 + 0.0, y - 7.0 + i * 2.2 + 1.1, T + 0.8, 0.45, 2.2 + 0.45, 0.04))
        zig.append(_box(ax, y - 7.0 + i * 2.2, T + 0.8, 4.4, 0.45, 0.04))
    k["gold"].extend(zig)


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    global T
    T = p["thickness"]
    L, W = p["length"], p["width"]
    k: dict[str, list] = defaultdict(list)

    pcb = extrude(Sketch() + RectangleRounded(L, W, p["corner_radius"]), T)
    hx, hy = p["hole_spacing_x"] / 2, p["hole_spacing_y"] / 2
    for x in (-hx, hx):
        for y in (-hy, hy):
            pcb -= Pos(x, y, 0) * Cylinder(p["hole_diameter"] / 2, 10)
            ring = Pos(x, y, T + 0.02) * (Cylinder(3.0, 0.04) - Cylinder(p["hole_diameter"] / 2, 1))
            k["gold"].append(ring)
    k["pcb-green"].append(pcb)

    # Silkscreen-ish white frames around the main parts (very thin, so they read as print).
    def frame(x, y, dx, dy):
        k["nylon-white"].append(_box(x, y, T, dx + 0.6, dy + 0.6, 0.02) - _box(x, y, T - 1, dx, dy, 2))

    _wroom(k, 16.6, -6.5)
    _usb_c(k, -L / 2 + 7.35 / 2 - 1.0, 9.0)
    _chip(k, -6.0, 3.0, 5.0, 4.4, 1.0, 8, 0.65, rot=90, pad=True)           # U2 DRV8833PWP
    frame(-6.0, 3.0, 6.4, 7.0)
    _chip(k, -16.0, -11.5, 2.9, 1.6, 1.1, 3, 0.95, rot=0, lead=0.5)          # U3 AP2112K (SOT-23-5 shown 3+3)
    _jst_ph(k, -24.5, -9.5, rot=90)                                          # J1 battery
    frame(-24.5, -9.5, 4.5, 5.9)
    _jst_ph(k, -6.0, 19.0, rot=0)                                            # J2 motor L
    _jst_ph(k, -6.0, -19.0, rot=180)                                         # J3 motor R
    _pin_header(k, 16.0, 17.5, 3)                                            # J4 servo (S, +, -)
    frame(16.0, 17.5, 7.62, 2.54)
    _tact(k, -18.0, 2.0)                                                     # EN
    _tact(k, -18.0, -3.5)                                                    # BOOT
    # Status LED (green) and its 330 R, decoupling capacitors (0603) and a 10 uF bulk cap (0805).
    k["led-green"].append(_box(-25.5, 3.0, T, 1.0, 0.8, 0.5))
    k["tin"].extend(_box(-25.5 + s * 0.65, 3.0, T, 0.3, 0.8, 0.45) for s in (-1, 1))
    _passive(k, -25.5, 0.6, 1.6, 0.8, 0.45, body="ic-black")
    for (x, y, r) in ((-12.5, -11.5, 90), (-19.5, -11.5, 90), (-6.0, -3.0, 0), (-1.5, 3.0, 90), (2.5, -12.0, 90),
                      (2.5, -1.0, 90), (6.5, 7.0, 0), (-10.5, 9.0, 0)):
        _passive(k, x, y, 1.6, 0.8, 0.8, rot=r)
    _passive(k, -12.0, -6.0, 2.0, 1.25, 1.1, rot=0)
    # Power and motor traces under the solder mask (a lighter green), as polylines of 0.5 / 0.8 mm tracks.
    def track(points, w):
        for (x0, y0), (x1, y1) in zip(points, points[1:]):
            dx, dy = x1 - x0, y1 - y0
            length = (dx * dx + dy * dy) ** 0.5
            seg = Pos((x0 + x1) / 2, (y0 + y1) / 2, T) * Rot(0, 0, degrees(atan2(dy, dx))) * \
                _box(0, 0, 0, length + w, w, 0.03)
            k["pcb-trace"].append(seg)

    track([(-6.0, 7.5), (-6.0, 15.5)], 0.8)                       # U2 -> J2 motor L
    track([(-6.0, -1.5), (-6.0, -6.0), (-8.0, -8.0), (-8.0, -15.5)], 0.8)  # U2 -> J3 motor R
    track([(-22.0, -9.5), (-18.5, -11.5)], 0.8)                   # J1 -> U3
    track([(-13.5, -11.5), (-10.0, -14.0), (1.0, -14.0), (4.0, -12.0)], 0.5)  # 3V3 -> module
    track([(-1.5, 3.0), (2.0, 3.0), (3.9, 1.0)], 0.4)
    track([(10.0, 2.5), (13.0, 9.0), (15.5, 15.0), (16.0, 15.5)], 0.4)  # servo PWM
    track([(-18.0, 5.0), (-14.0, 8.0), (-10.5, 9.0)], 0.4)
    track([(-26.0, 9.0), (-20.0, 11.0), (-16.0, 13.0), (-10.0, 13.0), (0.0, 6.0), (3.9, 6.0)], 0.4)  # USB D+/D-
    track([(-26.0, 10.2), (-20.5, 12.2), (-16.5, 14.2), (-9.5, 14.2), (0.5, 7.2), (3.9, 7.2)], 0.4)
    # A few vias and a ground-pour stitching row along the antenna keep-out edge.
    for i in range(8):
        k["tin"].append(Pos(2.0, -15.0 + i * 2.0, T) * (Cylinder(0.35, 0.04) - Cylinder(0.15, 1)))

    children = []
    for key, solids in k.items():
        solid = solids[0] if len(solids) == 1 else Compound(children=solids)
        solid.label = key
        children.append(solid)
    return Compound(children=children)
