"""OLED screen: a 0.96" 128 x 64 SSD1306 module (I2C), as bought, behind the remote's window.

Frame: centred on the active area, z = 0 at the top of the glass. The glass (26.7 x 19.3 x 1.2 mm, black active
area 21.7 x 10.9 mm offset towards +Y) sits on a 27 x 19 mm blue carrier PCB; the flex tail folds round the -Y edge
and a 4-pin header (GND VCC SCL SDA) drops 9.6 mm to the remote board.

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from build123d import Axis, Box, Compound, Cylinder, Part, Pos, fillet

PARAMS = {
    "glass": [26.7, 15.5, 1.2],
    "active": [21.7, 10.9],
    "pcb": [27.0, 19.0, 1.0],
    "header_drop": 9.6,          # PCB underside to the remote board's top
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    gx, gy, gt = p["glass"]
    glass = Pos(0, 0, -gt / 2) * Box(gx, gy, gt)
    active = Pos(0, 0.6, -0.01) * Box(p["active"][0], p["active"][1], 0.02)
    px, py, pt = p["pcb"]
    pcb = Pos(0, 0, -gt - pt / 2) * fillet(Box(px, py, pt).edges().filter_by(Axis.Z), 1.0)
    for x in (-px / 2 + 2.0, px / 2 - 2.0):
        for y in (-py / 2 + 2.0, py / 2 - 2.0):
            pcb -= Pos(x, y, -gt - pt / 2) * Cylinder(1.0, pt + 1)
    flex = Pos(0, -gy / 2 - 0.4, -gt / 2 - pt / 2) * Box(12.0, 0.15, gt + pt)
    pins, solder, header = [], [], []
    z_pcb = -gt - pt
    for i in range(4):
        x = (i - 1.5) * 2.54
        pins.append(Pos(x, py / 2 - 1.6, z_pcb - p["header_drop"] / 2 + 0.6) * Box(0.64, 0.64,
                                                                                  p["header_drop"] + 1.2))
        solder.append(Pos(x, py / 2 - 1.6, z_pcb - 0.2) * Cylinder(0.8, 0.4))  # under the PCB (top is flush)
    header.append(Pos(0, py / 2 - 1.6, z_pcb - 1.25) * Box(4 * 2.54, 2.5, 2.5))
    out = [(glass, "plastic-black"), (active, "oled"), (pcb, "pcb-blue"), (flex, "copper"),
           (Compound(children=pins), "gold"), (Compound(children=solder), "tin"),
           (Compound(children=header), "plastic-black")]
    merged: dict[str, list] = {}
    for solid, key in out:
        merged.setdefault(key, []).append(solid)
    children = []
    for key, solids in merged.items():
        solid = solids[0] if len(solids) == 1 else Compound(children=solids)
        solid.label = key
        children.append(solid)
    return Compound(children=children)
