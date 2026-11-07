"""Right drive motor: an N20 micro gear motor (12 x 10 mm gearbox, 160 rpm at 6 V), as bought.

Frame: origin on the shaft axis at the gearbox output face; the shaft points +Y (out through the tub wall into the
wheel hub), the motor body runs towards -Y. motor-r.py is the same source with side = "right" (shaft towards -Y).

Children are labelled with material keys (worker/fulmetry_worker/mesh.py): steel can, plates and shaft, brass gears
and bushing, a black end cap and copper terminals.
"""
from math import cos, radians, sin

from build123d import (Box, Circle, Compound, Cylinder, Part, Plane, Pos, Rectangle, Rot, Sketch, extrude, fillet,
                       mirror, Axis)

PARAMS = {
    "side": "right",
    "gearbox_width": 12.0,    # X
    "gearbox_height": 10.0,   # Z
    "gearbox_length": 9.0,    # Y
    "can_diameter": 12.0,
    "can_flats": 10.0,        # across the flats (top and bottom)
    "can_length": 15.0,
    "cap_length": 1.6,
    "shaft_length": 10.0,
    "shaft_diameter": 3.0,
    "shaft_flat": 2.5,
    "mount_hole_spacing": 9.0,  # two M1.6 tapped holes in the front plate
}


def _along_y(part: Part, y0: float) -> Part:
    """A solid modelled along +Z from z=0, turned to run along -Y from y = y0."""
    return Pos(0, y0, 0) * Rot(90, 0, 0) * part


def _gear(r: float, teeth: int, t: float) -> Part:
    s = Sketch() + Circle(r - 0.25)
    for i in range(teeth):
        a = 360 * i / teeth
        s += Pos(r * cos(radians(a)), r * sin(radians(a))) * Rot(0, 0, a) * Rectangle(0.55, 0.5)
    return extrude(s, t)


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    gw, gh, gl = p["gearbox_width"], p["gearbox_height"], p["gearbox_length"]
    plate_t = 0.8

    # Gearbox: two stamped plates on four pillars, gears visible between them.
    plate = fillet(Box(gw, plate_t, gh).edges().filter_by(Axis.Y), 0.6)
    holes = Sketch() + [Pos(x, 0) * Circle(0.8) for x in (-p["mount_hole_spacing"] / 2, p["mount_hole_spacing"] / 2)]
    front = Pos(0, -plate_t / 2, 0) * plate - Pos(0, 0.1, 0) * Rot(90, 0, 0) * extrude(holes, 1.2)
    rear = Pos(0, -gl + plate_t / 2, 0) * plate
    mid = Pos(0, -gl / 2, 0) * fillet(Box(gw - 0.6, 1.0, gh - 0.6).edges().filter_by(Axis.Y), 0.4)
    steel = front + rear + mid
    for x in (-gw / 2 + 1.0, gw / 2 - 1.0):
        for z in (-gh / 2 + 1.0, gh / 2 - 1.0):
            steel += Pos(x, 0, z) * _along_y(Cylinder(0.6, gl, align=None), 0)
    # Gear stack, offset like a real N20 train (output gear on the shaft, idlers above and below).
    span = gl - 2 * plate_t
    gears = Pos(0, -plate_t, 0) * _along_y(Pos(0, 0, 0.6) * _gear(4.6, 30, 1.2), 0)
    gears += Pos(1.6, -plate_t - 2.4, 1.0) * _along_y(_gear(3.4, 22, 1.0), 0)
    gears += Pos(-1.4, -plate_t - 4.2, -0.8) * _along_y(_gear(3.8, 26, 1.0), 0)
    gears += Pos(0.8, -plate_t - span + 0.2, 0.4) * _along_y(_gear(3.6, 24, 1.0), 0)
    gears += Pos(0, plate_t * 0 + 0.01, 0) * _along_y(Cylinder(2.0, 0.8, align=None), 0.8)  # output bushing
    # Can: a drawn steel can with two flats, a rolled groove near the gearbox, then a black end cap.
    cd, cl = p["can_diameter"], p["can_length"]
    prof = Sketch() + Circle(cd / 2) & Rectangle(cd + 1, p["can_flats"])
    can = _along_y(extrude(prof, cl), -gl)
    groove = Sketch() + Circle(cd / 2 + 1) - Circle(cd / 2 - 0.25)
    can -= _along_y(Pos(0, 0, 1.2) * extrude(groove, 0.5), -gl)
    steel += can
    cap_prof = Sketch() + Circle(cd / 2 - 0.3) & Rectangle(cd, p["can_flats"] - 0.6)
    cap = _along_y(extrude(cap_prof, p["cap_length"]), -gl - cl)
    y_end = -gl - cl - p["cap_length"]
    gears += _along_y(Cylinder(1.4, 0.7, align=None), y_end)  # rear bearing boss
    terminals = None
    for x in (-3.6, 3.6):
        tab = Pos(x, y_end - 1.4, 1.2) * Box(1.6, 2.8, 0.3)
        tab -= Pos(x, y_end - 1.9, 1.2) * Cylinder(0.4, 1)
        terminals = tab if terminals is None else terminals + tab
    # D-shaft.
    sr = p["shaft_diameter"] / 2
    shaft_prof = Sketch() + Circle(sr) & Pos(-sr + p["shaft_flat"] / 2, 0) * Rectangle(p["shaft_flat"], 2 * sr)
    shaft = Pos(0, 0.8, 0) * Rot(-90, 0, 0) * extrude(Pos(0, 0) * shaft_prof, p["shaft_length"] - 0.8)
    shaft += Rot(-90, 0, 0) * Cylinder(sr, 0.8, align=None)
    steel += shaft

    parts = [(steel, "steel"), (gears, "brass"), (cap, "plastic-black"), (terminals, "copper")]
    out = []
    for solid, key in parts:
        if p["side"] == "right":
            solid = mirror(solid, Plane.XZ)
        solid.label = key
        out.append(solid)
    return Compound(children=out)
