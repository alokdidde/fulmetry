"""Thumb joystick: a mini two-axis analog thumbstick (ALPS RKJXV-style) with a grippy cap, as bought.

Frame: origin at the stick's pivot (the manifest's joint origin), +Z up; the stick tilts ±25 degrees about X
(and Y). The tin-plated frame and black potentiometer housings sit below the pivot, the domed bezel and stick rise
through the shell's opening, and the cap's concave, ridged top sits 11 mm above the pivot. Five legs drop to the
remote board.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from math import cos, radians, sin

from build123d import Axis, Box, Compound, Cylinder, Part, Pos, Sphere, fillet

PARAMS = {
    "base": 18.0,             # square frame
    "base_depth": 5.5,        # below the pivot
    "cap_diameter": 16.0,
    "cap_top": 11.0,          # above the pivot
    "cap_height": 4.5,
    "legs": 5.4,              # frame underside to the board
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    b, d = p["base"], p["base_depth"]
    frame = Pos(0, 0, -d + 0.4) * Box(b, b, 0.8)
    frame += Pos(0, 0, -1.0) * Box(b - 3.0, b - 3.0, 0.6)
    housing = Pos(0, 0, -d / 2 - 0.4) * fillet(Box(b - 2.0, b - 2.0, d - 1.6).edges().filter_by(Axis.Z), 1.0)
    for a in (0, 90):
        housing += Pos((b / 2 + 1.5) * cos(radians(a)), (b / 2 + 1.5) * sin(radians(a)), -d / 2) * \
            Box(3.5 if a == 0 else 6.0, 6.0 if a == 0 else 3.5, d - 1.0)  # potentiometers
    dome = Sphere(6.5) & (Pos(0, 0, 3.0) * Box(14, 14, 6.0))
    legs = []
    for i in range(5):
        legs.append(Pos(-5.08 + i * 2.54, -b / 2 + 0.5, -d - p["legs"] / 2) * Box(0.6, 0.3, p["legs"] + 0.8))
    stick = Pos(0, 0, 4.0) * Cylinder(1.8, 6.0)
    ct, ch, cd = p["cap_top"], p["cap_height"], p["cap_diameter"]
    cap = Pos(0, 0, ct - ch / 2) * Cylinder(cd / 2, ch)
    cap = fillet(cap.edges().sort_by(Axis.Z)[-1:], 1.2)
    cap -= Pos(0, 0, ct + 16.0 - 0.9) * Sphere(16.0)                       # concave thumb rest
    for i in range(24):
        a = 360 * i / 24
        cap -= Pos((cd / 2) * cos(radians(a)), (cd / 2) * sin(radians(a)), ct - ch / 2 - 0.6) * \
            Box(0.8, 0.8, ch - 1.6).rotate(Axis.Z, a)                       # knurled rim
    cap += Pos(0, 0, ct - ch - 1.0) * Cylinder(3.0, 2.0)                    # boss onto the stick
    out = [(frame, "tin"), (housing, "plastic-black"), (dome + stick, "plastic-black"), (cap, "tpu-black"),
           (Compound(children=legs), "tin")]
    merged: dict[str, list] = {}
    for solid, key in out:
        merged.setdefault(key, []).append(solid)
    children = []
    for key, solids in merged.items():
        solid = solids[0] if len(solids) == 1 else Compound(children=solids)
        solid.label = key
        children.append(solid)
    return Compound(children=children)
