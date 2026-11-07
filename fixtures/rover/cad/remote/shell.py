"""Remote shell: the wireless remote's printed white PLA housing (110 x 60 x 24 mm), shown as the closed pair of halves.

Frame: centred on the origin, +X to the right hand, +Y away from the user (the USB-C edge), +Z up; the manifest
places it with its underside on the desk. The plan is a rounded slab (R14 corners) with R6 top and R3 bottom edges,
2 mm walls and a parting line 9 mm above the desk.

Openings match the parts placed in fulmetry.project.json: the thumb joystick (x = -32), the OLED window (x = 2),
the A/B buttons (x = +32), USB-C on the +Y edge and the power slide switch on the -Y edge. Inside: four Ø5
standoffs carry the board 10 mm above the desk (M2 screws from below, in counterbores).
"""
from build123d import (Axis, Box, Cylinder, Part, Pos, RectangleRounded, Rot, Sketch, SlotCenterToCenter, chamfer,
                       extrude, fillet)

PARAMS = {
    "length": 110.0,
    "width": 60.0,
    "height": 24.0,
    "corner_radius": 14.0,
    "top_radius": 6.0,
    "bottom_radius": 3.0,
    "wall": 2.0,
    "split_z": 9.0,              # above the underside
    "joystick_x": -32.0,
    "joystick_hole": 21.0,
    "screen_x": 2.0,
    "screen_window": [27.0, 17.0],
    "buttons": [[35.5, -4.0], [28.5, 4.0]],   # A (lower right), B (x, y)
    "button_hole": 9.6,
    "board_z": 10.0,             # board underside above the desk
    "board_holes": [82.0, 42.0],
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    L, W, H = p["length"], p["width"], p["height"]
    w = p["wall"]
    zb, zt = -H / 2, H / 2

    def slab(lx, ly, r, h, z0):
        return Pos(0, 0, z0) * extrude(Sketch() + RectangleRounded(lx, ly, r), h)

    shell = slab(L, W, p["corner_radius"], H, zb)
    shell = fillet(shell.edges().group_by(Axis.Z)[-1], p["top_radius"])
    shell = fillet(shell.edges().group_by(Axis.Z)[0], p["bottom_radius"])
    inner = slab(L - 2 * w, W - 2 * w, p["corner_radius"] - w, H - 2 * w, zb + w)
    inner = fillet(inner.edges().group_by(Axis.Z)[-1], p["top_radius"] - w)
    shell -= inner
    # Parting line.
    zs = zb + p["split_z"]
    shell -= slab(L + 2, W + 2, p["corner_radius"], 0.5, zs) - slab(L - 0.8, W - 0.8, p["corner_radius"] - 0.4, 0.5, zs)

    # Joystick opening with a chamfered rim.
    jx = p["joystick_x"]
    shell -= Pos(jx, 0, zt) * Cylinder(p["joystick_hole"] / 2, 2 * w + 2)
    shell = chamfer(shell.edges().filter_by(lambda e: e.geom_type.name == "CIRCLE" and
                                            abs(e.radius - p["joystick_hole"] / 2) < 1e-3 and
                                            e.center().Z > zt - 0.01), 1.0)
    # OLED window: a 1 mm deep bezel recess and the through opening.
    sx = p["screen_x"]
    wx, wy = p["screen_window"]
    shell -= Pos(sx, 0, zt - 1.0) * extrude(Sketch() + RectangleRounded(wx + 4, wy + 4, 2.5), 2.0)
    shell -= Pos(sx, 0, zt - 3 * w) * extrude(Sketch() + RectangleRounded(wx, wy, 1.0), 4 * w)
    # Button openings, each in a shallow dish.
    for bx, by in p["buttons"]:
        shell -= Pos(bx, by, zt) * Cylinder(p["button_hole"] / 2, 2 * w + 2)
        shell -= Pos(bx, by, zt + 0.1) * Cylinder(p["button_hole"] / 2 + 1.6, 1.2)
    # USB-C (+Y edge) and slide switch (-Y edge) at board height.
    zc = zb + p["board_z"] + 1.6 + 1.63
    shell -= Pos(0, W / 2, zc) * Rot(90, 0, 0) * extrude(SlotCenterToCenter(9.6 - 4.0, 4.0), 3 * w, both=True)
    shell -= Pos(12.0, -W / 2, zc) * Box(9.0, 3 * w, 4.0)
    shell -= Pos(12.0, -W / 2, zc) * Box(12.0, 1.2, 6.0)
    # Board standoffs with M2 screw holes and counterbores in the underside.
    hx, hy = p["board_holes"][0] / 2, p["board_holes"][1] / 2
    so_h = p["board_z"] - w
    for x in (-hx, hx):
        for y in (-hy, hy):
            shell += Pos(x, y, zb + w + so_h / 2) * Cylinder(2.5, so_h)
            shell -= Pos(x, y, zb) * Cylinder(1.1, 2 * p["board_z"] + 1)
            shell -= Pos(x, y, zb) * Cylinder(2.1, 2.4)
    # Grip ridges on the underside at both ends.
    for s in (-1, 1):
        for i in range(5):
            x = s * (36.0 + i * 3.2)
            shell -= Pos(x, 0, zb) * Box(1.2, 30.0, 1.0)
    shell.label = "pla-white"
    return shell
