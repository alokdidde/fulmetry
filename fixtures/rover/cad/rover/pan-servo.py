"""Pan servo: a TowerPro SG90 9 g micro servo with its double-arm horn and horn screw, as bought.

Frame: origin on the output shaft axis at the bottom of the case, +Z up. The case runs towards -X (the output shaft
sits 5.9 mm from the +X end, like the real part). The mounting tabs' underside is at z = 15.6, the horn's top face
(where the camera head sits) at z = horn_top.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from build123d import Axis, Box, Compound, Cylinder, Part, Pos, Rot, SlotCenterToCenter, extrude, fillet

PARAMS = {
    "length": 22.5,        # X
    "width": 12.2,         # Y
    "height": 22.7,        # case top
    "shaft_from_end": 5.9,
    "tab_span": 32.2,
    "tab_z": 15.6,         # underside of the mounting tabs
    "tab_thickness": 2.5,
    "tab_hole_spacing": 27.8,
    "boss_height": 3.4,
    "horn_span": 32.0,
    "horn_top": 30.0,
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    L, W, H = p["length"], p["width"], p["height"]
    cx = p["shaft_from_end"] - L / 2  # case centre x
    case = Pos(cx, 0, H / 2) * Box(L, W, H)
    case = fillet(case.edges().filter_by(Axis.Z), 0.8)
    # Parting lines of the three-part case.
    for z in (3.6, 17.9):
        band = Pos(cx, 0, z) * (Box(L + 2, W + 2, 0.45) - Box(L - 0.5, W - 0.5, 0.45))
        case -= band
    # Mounting tabs with holes and the slot out to the end.
    tabs = Pos(cx, 0, p["tab_z"] + p["tab_thickness"] / 2) * Box(p["tab_span"], W, p["tab_thickness"])
    tabs = fillet(tabs.edges().filter_by(Axis.Z), 0.6)
    for s in (-1, 1):
        hx = cx + s * p["tab_hole_spacing"] / 2
        tabs -= Pos(hx, 0, p["tab_z"]) * Cylinder(1.0, 10)
        tabs -= Pos(hx + s * 2.5, 0, p["tab_z"]) * Box(5, 1.0, 10)
    case += tabs
    # Top: the round gear boss over the shaft and the smaller idler boss beside it.
    bh = p["boss_height"]
    case += Pos(0, 0, H + bh / 2) * Cylinder(W / 2 - 0.3, bh)
    case += Pos(-7.2, 0, H + bh * 0.3) * Cylinder(2.8, bh * 0.6)
    # Cable boot on the -X end, low down, and three short leads.
    x_end = cx - L / 2
    case += Pos(x_end - 1.0, 0, 4.5) * fillet(Box(2.0, 4.6, 2.6).edges().filter_by(Axis.X), 0.6)
    wires = {}
    for i, key in enumerate(("wire-brown", "wire-red", "wire-orange")):
        wires[key] = Pos(x_end - 2.0 - 4.0, (i - 1) * 1.25, 4.5) * Rot(0, 90, 0) * Cylinder(0.6, 8)

    # Output spline and horn (natural nylon), horn screw (steel).
    top = p["horn_top"]
    spline = Pos(0, 0, H + bh + 0.6) * Cylinder(2.4, 1.2)
    hub_h = 3.8
    hub = Pos(0, 0, top - hub_h / 2) * Cylinder(3.6, hub_h)
    arm_t = 1.5
    arm = extrude(SlotCenterToCenter(p["horn_span"] - 6, 5.6), arm_t)
    arm = Pos(0, 0, top - arm_t) * arm
    # Taper: cut wedges so the arms narrow from 7 mm at the hub to 4 mm at the tips.
    for s in (-1, 1):
        for sy in (-1, 1):
            arm -= Pos(s * 9, sy * 4.3, top - arm_t / 2) * Rot(0, 0, s * sy * 5.5) * Box(16, 3, arm_t + 1)
    horn = hub + arm
    for s in (-1, 1):
        for k in range(4):
            horn -= Pos(s * (6.0 + 2.5 * k), 0, top) * Cylinder(0.5, 4)
    horn -= Pos(0, 0, top) * Cylinder(1.3, 2.0)
    screw = Pos(0, 0, top - 1.0) * Cylinder(1.0, 2.0)
    screw += Pos(0, 0, top - 1.2 + 0.9) * Cylinder(1.6, 0.6)
    screw -= Pos(0, 0, top - 0.1) * (Box(2.2, 0.5, 0.6) + Box(0.5, 2.2, 0.6))

    out = [(case, "servo-blue"), (spline + horn, "nylon-white"), (screw, "steel")]
    out += [(w, k) for k, w in wires.items()]
    children = []
    for solid, key in out:
        solid.label = key
        children.append(solid)
    return Compound(children=children)
