"""Left wheel of the desk rover: a white PETG hub with a black TPU 95A tyre pressed over it.

Built by the worker's cad.build. Units are mm. The wheel is centred on the origin with its axle along Y and its
outer (hub-face) side towards +Y, so its bounds match the manifest's part:rover/wheel-l (60 x 14 x 60).
wheel-r.py is the same source with side = "right" (outer face towards -Y).

The result is a compound of two labelled solids: "tpu-black" (tyre) and "petg-white" (hub). The labels are the
material keys of worker/fulmetry_worker/mesh.py.
"""
from math import cos, radians, sin

from build123d import (Axis, Box, Circle, Compound, Cylinder, GeomType, Part, Plane, Pos, Rectangle, Rot, Sketch, extrude,
                       fillet, mirror, offset)

PARAMS = {
    "side": "left",          # "left": outer face +Y; "right": outer face -Y
    "diameter": 60.0,        # outer tread diameter
    "width": 14.0,           # along the axle (Y)
    "rim_diameter": 46.0,    # hub rim = tyre bore
    "grooves": 20,           # tread blocks per row (two rows, offset half a pitch)
    "groove_width": 2.6,
    "groove_depth": 1.6,
    "centre_groove": 1.2,    # circumferential groove between the two rows
    "shoulder": 1.5,         # tyre shoulder fillet
    "spokes": 5,
    "spoke_width": 4.5,
    "web_thickness": 2.5,
    "web_recess": 1.5,       # outer face of the web sits this far inside the tyre face
    "hub_diameter": 11.0,
    "shaft_diameter": 3.0,   # N20 gear-motor D-shaft
    "shaft_flat": 2.5,       # across the flat
    "clearance": 0.1,        # added to the bore for a snug press fit
}


def _tyre(p: dict) -> Part:
    r, w, ri = p["diameter"] / 2, p["width"], p["rim_diameter"] / 2
    tyre = Cylinder(r, w) - Cylinder(ri, w)
    if p["shoulder"] > 0:
        outer = [e for e in tyre.edges().filter_by(GeomType.CIRCLE) if abs(e.radius - r) < 1e-6]
        tyre = fillet(outer, p["shoulder"])
    n = int(p["grooves"])
    if n > 0:
        pitch = 360 / n
        cg = p["centre_groove"]
        row_w = (w - cg) / 2 + 1  # each row's groove runs from the centre groove out past the face
        for row, z in ((0, (cg / 2 + w / 2) / 2 + 0.5), (1, -(cg / 2 + w / 2) / 2 - 0.5)):
            groove = Box(2 * p["groove_depth"], p["groove_width"], row_w)
            for i in range(n):
                # Row 0 has lands on the +-X / +-Z extremes, so the tight bounds stay exactly the tread diameter.
                a = pitch * (i + 0.5 + 0.5 * row)
                tyre -= Pos(r * cos(radians(a)), r * sin(radians(a)), z) * Rot(0, 0, a) * groove
        if cg > 0:
            tyre -= Cylinder(r + 1, cg) - Cylinder(r - p["groove_depth"] * 0.75, cg)
    return tyre


def _hub(p: dict) -> Part:
    w, ri = p["width"], p["rim_diameter"] / 2
    rim_w = w - 1.0
    hub = Cylinder(ri, rim_w) - Cylinder(ri - 2.2, rim_w)
    # Web near the outer face (+Z here, +Y after the final turn), with spoke windows.
    t, z_out = p["web_thickness"], w / 2 - p["web_recess"]
    web = Pos(0, 0, z_out - t / 2) * Cylinder(ri - 1, t)
    n = int(p["spokes"])
    if n > 0:
        ring = Sketch() + Circle(ri - 3.5) - Circle(p["hub_diameter"] / 2 + 2.5)
        for i in range(n):
            ring -= Rot(0, 0, i * 360 / n) * Pos((ri) / 2, 0) * Rectangle(ri, p["spoke_width"])
        windows = offset(offset(ring, -1.2), 1.2)
        web -= Pos(0, 0, z_out - t - 1) * extrude(windows, t + 2)
        # Stiffening ribs behind each spoke, on the inner side.
        for i in range(n):
            rib = Pos((ri + p["hub_diameter"] / 2) / 2 - 1, 0, z_out - t - 2.5) * Box(ri - p["hub_diameter"] / 2 - 2,
                                                                                    1.6, 5)
            hub += Rot(0, 0, i * 360 / n) * rib
    hub += web
    # Hub barrel, full width, with a chamfer on the outer end and a D-shaft bore.
    barrel = Cylinder(p["hub_diameter"] / 2, w)
    barrel = fillet(barrel.edges().sort_by(Axis.Z)[-1:], 1.0)
    hub += barrel
    br = p["shaft_diameter"] / 2 + p["clearance"]
    across = p["shaft_flat"] + p["clearance"]
    hub -= Cylinder(br, w + 2) & (Pos(-br + across / 2, 0, 0) * Box(across, 2 * br, w + 2))
    # Grub-screw boss hole (M2, radial) and a small decorative hex recess around the bore on the outer face.
    hub -= Pos(0, 0, w / 2) * extrude(Sketch() + Circle(3.0) - Circle(br + 0.6), -0.6)
    return hub


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    tyre, hub = _tyre(p), _hub(p)
    # Modelled with the axle along Z and the outer face at +Z; turn so the axle runs along Y.
    turn = Rot(-90, 0, 0)  # +Z -> +Y
    tyre, hub = turn * tyre, turn * hub
    if p["side"] == "right":
        tyre, hub = mirror(tyre, Plane.XZ), mirror(hub, Plane.XZ)
    tyre.label, hub.label = "tpu-black", "petg-white"
    return Compound(children=[tyre, hub])
