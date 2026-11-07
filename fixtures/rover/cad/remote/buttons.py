"""A and B buttons: two red printed caps on 6 x 6 mm tactile switches, diagonal like a game pad.

Frame: centred between the two buttons, z = 0 at the remote shell's top surface. A is lower right (+X, -Y), B upper
left. Each cap is a Ø9 mm dome standing 2.4 mm proud of the shell with a retaining flange under the top wall and a
stem down to its tact switch, which sits on the remote board 12.4 mm below the shell top.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from build123d import Axis, Box, Compound, Cylinder, Part, Pos, Sphere, fillet

PARAMS = {
    "positions": [[3.5, -4.0], [-3.5, 4.0]],   # A, B relative to the part origin
    "cap_diameter": 9.0,
    "proud": 2.4,                               # cap top above the shell
    "wall": 2.0,
    "board_top": -12.4,                         # remote board's top below the shell top
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    caps, switches, plungers, legs = [], [], [], []
    r = p["cap_diameter"] / 2
    for x, y in p["positions"]:
        h = p["proud"] + 1.0 + p["wall"]
        column = Pos(x, y, h / 2 - p["wall"]) * Cylinder(r, h)
        top = Pos(x, y, p["proud"] - 6.0) * Sphere(6.0) + Pos(x, y, -p["wall"] / 2 - 0.5) * Box(2 * r + 2, 2 * r + 2,
                                                                                           p["wall"] + 1.0)
        cap = column & top
        flange = Pos(x, y, -p["wall"] - 0.6) * Cylinder(r + 1.2, 1.2)
        stem_len = -p["wall"] - 1.2 - (p["board_top"] + 5.0)
        stem = Pos(x, y, -p["wall"] - 1.2 - stem_len / 2) * Cylinder(1.8, stem_len)
        caps.append(cap + flange + stem)
        # 6 x 6 x 5 mm tact switch: black body, steel cover, black plunger.
        zb = p["board_top"]
        switches.append(Pos(x, y, zb + 1.75) * fillet(Box(6.0, 6.0, 3.5).edges().filter_by(Axis.Z), 0.3))
        plungers.append(Pos(x, y, zb + 3.5 + 0.75) * Cylinder(1.75, 1.5))
        legs.append(Pos(x, y, zb + 3.55) * (Box(6.2, 6.2, 0.2) - Pos(0, 0, 0) * Cylinder(2.0, 1)))
        for sx in (-1, 1):
            for sy in (-1, 1):
                legs.append(Pos(x + sx * 3.25, y + sy * 2.25, zb + 0.4) * Box(0.6, 0.7, 0.8))
    out = [(Compound(children=caps), "pla-red"), (Compound(children=switches + plungers), "plastic-black"),
           (Compound(children=legs), "steel")]
    children = []
    for solid, key in out:
        solid.label = key
        children.append(solid)
    return Compound(children=children)
