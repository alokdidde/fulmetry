"""Remote battery: a 1S LiPo pouch cell (503450, 3.7 V 1000 mAh) with its protection board and JST-PH lead.

Frame: centred on the pouch, +Z up; it lies flat under the remote board. The silver pouch has the sealed seam
folded down both long sides, a kapton-wrapped protection board across the +X end and red/black leads.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from build123d import Axis, Box, Compound, Cylinder, Part, Pos, Rot, fillet

PARAMS = {
    "length": 50.0,
    "width": 30.0,
    "thickness": 5.6,
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    L, W, t = p["length"], p["width"], p["thickness"]
    pouch = Box(L - 4.0, W - 1.2, t)
    pouch = fillet(pouch.edges().filter_by(Axis.X), t / 2 - 0.3)
    for s in (-1, 1):
        pouch += Pos(0, s * (W / 2 - 0.9), -t / 2 + 1.6) * Box(L - 4.0, 0.4, 3.0)  # folded side seams
    pouch += Pos(-L / 2 + 1.5, 0, 0) * Box(3.0, W - 1.6, t - 1.2)                  # sealed far end
    pcb = Pos(L / 2 - 1.8, 0, 0) * Box(3.6, W - 2.0, t - 0.6)
    label = Pos(-2.0, 0, t / 2 + 0.05) * Box(30.0, 20.0, 0.1)
    wires = {}
    for key, y in (("wire-red", 1.2), ("wire-black", -1.2)):
        wires[key] = Pos(L / 2 + 0.0, y, 0.6) * Rot(0, 90, 0) * Cylinder(0.6, 4.0)
    out = [(pouch, "aluminium"), (pcb, "wrap-yellow"), (label, "nylon-white")] + [(w, k) for k, w in wires.items()]
    children = []
    for solid, key in out:
        solid.label = key
        children.append(solid)
    return Compound(children=children)
