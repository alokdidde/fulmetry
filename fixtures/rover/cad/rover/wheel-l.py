"""Left wheel of the desk rover: a TPU 95A tyre-and-hub in one print.

Built by the worker's cad.build. Units are mm. The wheel is centred on the origin with its axle along Y,
so its bounds match the manifest's part:rover/wheel-l (60 x 14 x 60).
"""
from math import cos, radians, sin

from build123d import Box, Cylinder, Part, Pos, Rot

PARAMS = {
    "diameter": 60.0,       # outer tread diameter
    "width": 14.0,          # along the axle (Y)
    "grooves": 24,          # axial tread grooves around the rim
    "groove_width": 2.0,
    "groove_depth": 1.5,
    "rim_thickness": 5.0,   # solid band under the tread
    "web_thickness": 6.0,   # disc joining rim and hub, centred on the mid-plane
    "hub_diameter": 12.0,
    "shaft_diameter": 3.0,  # N20-style gear-motor D-shaft
    "shaft_flat": 2.5,      # across the flat
    "clearance": 0.1,       # added to the bore for a snug fit in TPU
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    r, w = p["diameter"] / 2, p["width"]

    # Modelled with the axle along Z, then turned so the axle runs along Y.
    wheel = Cylinder(r, w)

    # Lighten both faces: a ring pocket between hub and rim, down to the web.
    pocket_depth = (w - p["web_thickness"]) / 2
    if pocket_depth > 0:
        ring = Cylinder(r - p["rim_thickness"], pocket_depth) - Cylinder(p["hub_diameter"] / 2, pocket_depth)
        for side in (1, -1):
            wheel -= Pos(0, 0, side * (w - pocket_depth) / 2) * ring

    # Tread: axial grooves, offset half a pitch so lands sit on the +-X / +-Z extremes (tight 60 mm bounds).
    n = int(p["grooves"])
    if n > 0:
        pitch = 360 / n
        groove = Box(2 * p["groove_depth"], p["groove_width"], w + 2)
        for i in range(n):
            a = pitch * (i + 0.5)
            wheel -= Pos(r * cos(radians(a)), r * sin(radians(a)), 0) * Rot(0, 0, a) * groove

    # D-shaft bore through the hub: a round hole with one flat.
    br = p["shaft_diameter"] / 2 + p["clearance"]
    across = p["shaft_flat"] + p["clearance"]
    wheel -= Cylinder(br, w + 2) & (Pos(-br + across / 2, 0, 0) * Box(across, 2 * br, w + 2))

    return Rot(90, 0, 0) * wheel
