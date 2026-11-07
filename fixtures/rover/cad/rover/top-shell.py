"""Top shell: the printed PETG lid of the tub, carrying the main board and the pan servo's turret.

Frame: +X forward, z = 0 is the underside of the lid plate, which rests on the tub's rim (the manifest places the
lid 50 mm above the floor). The plate is 3 mm thick with a 4 mm locating lip below it that fits inside the tub's
walls (0.3 mm clearance) and is notched around the tub's corner bosses.

- Four Ø3.4 holes with 0.4 mm spot-faces over the tub's inserts (M3 x 10 button-head screws).
- Servo turret for an SG90: two 12 mm towers carry the servo's tabs (z = 15) with Ø1.6 pilot holes for the M2 tab
  screws; a 3 mm ring joins them around the body cut-out; the servo lead leaves through a tunnel in the rear tower. The sides stay open.
- Four Ø6.5 x 5 mm standoffs for the main board (53 x 38 mm hole grid) with Ø4.0 holes for M3 x 4 inserts, and a
  cable slot under the board.
- Vent slots behind the board, and a 0.6 mm panel line 4 mm in from the edge.
"""
from build123d import (Axis, Box, Cylinder, Part, Pos, RectangleRounded, Sketch, SlotCenterToCenter, extrude, fillet)

PARAMS = {
    "length": 160.0,
    "width": 120.0,
    "thickness": 3.0,
    "corner_radius": 10.0,
    "tub_wall": 2.4,
    "lip_depth": 4.0,
    "lip_wall": 2.0,
    "clearance": 0.3,
    "boss_xy": [72.0, 52.0],
    "servo_axis_x": 50.0,
    "servo_case": [22.5, 12.2],
    "servo_shaft_from_end": 5.9,
    "servo_tab_hole_spacing": 27.8,
    "turret_height": 12.0,     # above the plate
    "turret_wall": 2.0,
    "board_x": -20.0,
    "board_holes": [53.0, 38.0],
    "standoff_height": 5.0,
}


def _rr(lx, ly, r, h, z0):
    return Pos(0, 0, z0) * extrude(Sketch() + RectangleRounded(lx, ly, r), h)


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    L, W, t = p["length"], p["width"], p["thickness"]
    r = p["corner_radius"]
    plate = _rr(L, W, r, t, 0.0)
    plate = fillet(plate.edges().group_by(Axis.Z)[-1], 1.5)

    # Locating lip under the plate, notched around the tub's corner bosses.
    c, tw = p["clearance"], p["tub_wall"]
    lo_x, lo_y = L - 2 * tw - 2 * c, W - 2 * tw - 2 * c
    lw = p["lip_wall"]
    lip = _rr(lo_x, lo_y, r - tw - c, p["lip_depth"], -p["lip_depth"]) - \
        _rr(lo_x - 2 * lw, lo_y - 2 * lw, r - tw - c - lw, p["lip_depth"], -p["lip_depth"])
    bx, by = p["boss_xy"]
    for x in (-bx, bx):
        for y in (-by, by):
            lip -= Pos(x, y, -p["lip_depth"] / 2) * Cylinder(5.0, p["lip_depth"] + 0.01)
    lid = plate + lip
    # Corner screw holes with spot-faces.
    for x in (-bx, bx):
        for y in (-by, by):
            lid -= Pos(x, y, 0) * Cylinder(1.7, 3 * t)
            lid -= Pos(x, y, t) * Cylinder(3.4, 0.8)

    # Panel line.
    groove = _rr(L - 8.0, W - 8.0, r - 4.0, 0.4, t - 0.4) - _rr(L - 9.2, W - 9.2, r - 4.6, 0.4, t - 0.4)
    lid -= groove

    # Servo turret: two towers under the servo's tabs on a low ring, open at the sides so the servo shows.
    cl, cw = p["servo_case"]
    sx = p["servo_axis_x"] + p["servo_shaft_from_end"] - cl / 2   # servo case centre
    th, tw2 = p["turret_height"], p["turret_wall"]
    tower_x0, tower_x1 = cl / 2 + 0.4, p["servo_tab_hole_spacing"] / 2 + 3.4
    tower_y = cw + 2 * tw2 + 2.0
    ring = Pos(sx, 0, 0) * (_rr(2 * tower_x1, tower_y, 3.0, t + 3.0, 0.0) -
                            _rr(cl + 0.8, cw + 0.8, 0.8, t + 4.0, -0.5))
    lid += ring
    for s in (-1, 1):
        xc = sx + s * (tower_x0 + tower_x1) / 2
        tower = Pos(xc, 0, 0) * _rr(tower_x1 - tower_x0, tower_y, 1.5, t + th, 0.0)
        tower = fillet(tower.edges().group_by(Axis.Z)[-1], 0.8)
        lid += tower
        lid -= Pos(sx + s * p["servo_tab_hole_spacing"] / 2, 0, t + th) * Cylinder(0.8, 8.0)  # M2 pilot
    lid -= Pos(sx, 0, 0) * Box(cl + 0.8, cw + 0.8, 4 * t)                           # case cut-out
    lid -= Pos(sx - (cl / 2 + tower_x1) / 2, 0, t + 1.0) * Box(tower_x1 - cl / 2 + 2.0, 5.0, 5.0)  # lead tunnel
    # Board standoffs and the cable slot under the board.
    hx, hy = p["board_holes"][0] / 2, p["board_holes"][1] / 2
    bxc = p["board_x"]
    sh = p["standoff_height"]
    for x in (bxc - hx, bxc + hx):
        for y in (-hy, hy):
            so = Pos(x, y, t + sh / 2) * Cylinder(3.25, sh)
            so = fillet(so.edges().group_by(Axis.Z)[-1], 0.5)
            lid += so
            lid -= Pos(x, y, t + sh) * Cylinder(2.0, 8.0)
    lid -= Pos(bxc - 4.0, 0, 0) * extrude(SlotCenterToCenter(14.0, 5.0), 3 * t, both=True)
    # Vents behind the board.
    for i in range(6):
        y = -25.0 + i * 10.0
        lid -= Pos(-63.0, y, 0) * extrude(SlotCenterToCenter(12.0, 3.0), 3 * t, both=True)
    lid.label = "petg-white"
    return lid
