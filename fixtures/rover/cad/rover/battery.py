"""Battery pack: two 18650 Li-ion cells side by side (2S, 7.4 V, 2000 mAh) in yellow heat-shrink, as bought.

Frame: pack centred on the origin with the cells' axes along Y (it lies across the tub, behind the axle), +Z up.
The heat-shrink folds over both ends, showing the cell ends and the nickel series strap; a small protection
board sits on the +Y end, with red and black leads to a JST-PH plug.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from build123d import (Axis, Box, Circle, Compound, Cylinder, Part, Pos, Rectangle, Rot, Sketch, Sphere, extrude,
                       fillet, offset)

PARAMS = {
    "cell_diameter": 18.4,
    "cell_length": 65.2,
    "wrap": 0.25,            # heat-shrink thickness
    "end_fold": 2.6,         # how far the shrink folds over each end
    "lead_length": 8.0,
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    d, l, t = p["cell_diameter"], p["cell_length"], p["wrap"]
    r = d / 2 + t
    end_t = 0.6                      # strap + fold at each end
    total = l + 2 * end_t
    # Cross-section: two touching cells (a stadium), extruded along Y.
    section = Sketch() + [Pos(x, 0) * Circle(r) for x in (-d / 2, d / 2)] + Rectangle(d, 2 * r)
    pack = Pos(0, total / 2, 0) * Rot(90, 0, 0) * extrude(section, total)
    pack = fillet(pack.edges().filter_by(lambda e: abs(abs(e.center().Y) - total / 2) < 1e-6), 1.6)
    # Folded-over shrink leaves an opening on each end.
    window = offset(section, -p["end_fold"])
    cells, strap = [], []
    for s in (-1, 1):
        y_face = s * total / 2
        pack -= Pos(0, y_face, 0) * Rot(90, 0, 0) * extrude(window, 0.5, both=True)
        for x in (-d / 2, d / 2):
            cells.append(Pos(x, y_face - s * 0.45, 0) * Rot(90, 0, 0) * Cylinder(d / 2 - p["end_fold"] + 0.3, 0.3))
        # Nickel strap across both cell ends, with spot-weld dimples.
        band = Pos(0, y_face - s * 0.2, 0) * Box(d + 4, 0.2, 7.0)
        for x in (-d / 2, d / 2):
            for dz in (-1.6, 1.6):
                for dx in (-2.2, 2.2):
                    band -= Pos(x + dx, y_face - s * 0.2, dz) * Sphere(0.45)
        strap.append(band)
    # White spec label on top.
    label = Pos(0, -4.0, r - 0.08) * Box(20.0, 28.0, 0.2)
    label = fillet(label.edges().filter_by(Axis.Z), 1.0)
    # Protection board on the +Y end, kapton-wrapped (shown as the board), and the leads.
    yb = total / 2
    bms = Pos(0, yb + 0.6, -2.0) * fillet(Box(30.0, 1.0, 8.0).edges().filter_by(Axis.Y), 0.8)
    leads = {}
    for key, x in (("wire-red", 2.2), ("wire-black", -0.2)):
        w = Pos(x, yb + 1.0 + p["lead_length"] / 2, 2.0) * Rot(90, 0, 0) * Cylinder(0.75, p["lead_length"])
        leads[key] = w
    plug = Pos(1.0, yb + 1.0 + p["lead_length"] + 3.0, 2.0) * fillet(Box(6.0, 6.0, 4.5).edges().filter_by(Axis.Y),
                                                                      0.4)
    plug -= Pos(1.0, yb + 1.0 + p["lead_length"] + 3.0, 4.2) * Box(1.6, 4.0, 1.0)

    out = [(pack, "wrap-yellow"), (Compound(children=cells), "steel"), (Compound(children=strap), "tin"),
           (label, "nylon-white"), (bms, "pcb-blue"), (plug, "nylon-white")]
    out += [(w, k) for k, w in leads.items()]
    children = []
    merged: dict[str, list] = {}
    for solid, key in out:
        merged.setdefault(key, []).append(solid)
    for key, solids in merged.items():
        solid = solids[0] if len(solids) == 1 else Compound(children=solids)
        solid.label = key
        children.append(solid)
    return Compound(children=children)
