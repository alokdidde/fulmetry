"""Chassis tub: the rover's printed PETG body (160 x 120 x 40 mm), open at the top for the lid.

Frame: centred on the origin, +X forward, +Y left, +Z up; the tub's rim is at z = +20 and its underside at z = -20
(the manifest places it with the underside 10 mm above the floor). All internal features are positioned for the
parts placed in fulmetry.project.json:

- 2.4 mm walls and floor, R10 vertical corners, R4 underside, a 0.6 mm parting groove 6 mm below the rim.
- Four corner screw bosses (Ø9) with Ø4.0 x 8 mm holes for M3 heat-set inserts; the lid screws into them.
- N20 motor mounts: a cradle with side cheeks under each motor and, in each side wall, a Ø5 shaft hole plus two
  Ø1.8 holes, counterbored 0.6 mm, for the gearbox's M1.6 screws (9 mm apart).
- Battery bay behind the axle for the 2S 18650 pack (lying across the tub): corner stops and two slots for a
  hook-and-loop strap.
- Caster mount: a 7 mm recess in the underside at x = -65 for the ball caster's housing and flange, under a round
  boss with two M3 insert holes.
- Four M2 standoffs for the motor-driver board, a slide-switch cut-out in the rear wall and vent slots in the
  side panels ahead of the wheels, and a matching slotted panel on the front.
"""
from build123d import (Axis, Box, Cylinder, Part, Pos, RectangleRounded, Rot, Sketch, SlotCenterToCenter, chamfer,
                       extrude, fillet)

PARAMS = {
    "length": 160.0,          # X
    "width": 120.0,           # Y
    "height": 40.0,           # Z
    "wall": 2.4,
    "floor": 2.4,
    "corner_radius": 10.0,
    "bottom_radius": 4.0,
    "boss_diameter": 9.0,
    "boss_inset": 8.0,        # boss centre from the outer corner faces
    "insert_hole": 4.0,
    "insert_depth": 8.0,     # 5.7 mm insert + room for the screw tip
    "motor_z": 0.0,           # motor axis height (tub frame)
    "motor_inner_y": 29.2,    # rear end of each motor, |y|
    "battery_x": -32.0,       # battery centre
    "battery_size": [37.3, 67.0],
    "caster_x": -65.0,
    "caster_recess": 7.0,     # depth of the flange recess in the underside
    "caster_hole_spacing": 25.0,
    "driver_x": 35.0,
    "driver_holes": [25.0, 20.0],
    "driver_standoff": 4.0,
}


def _rrect_solid(lx, ly, h, r, z0):
    return Pos(0, 0, z0) * extrude(Sketch() + RectangleRounded(lx, ly, r), h)


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    L, W, H = p["length"], p["width"], p["height"]
    wall, flo = p["wall"], p["floor"]
    zb, zt = -H / 2, H / 2
    zf = zb + flo  # floor top

    outer = _rrect_solid(L, W, H, p["corner_radius"], zb)
    outer = fillet(outer.edges().group_by(Axis.Z)[0], p["bottom_radius"])
    inner = _rrect_solid(L - 2 * wall, W - 2 * wall, H, p["corner_radius"] - wall, zf)
    inner = fillet(inner.edges().group_by(Axis.Z)[0], 1.2)
    tub = outer - inner
    tub = chamfer(tub.edges().group_by(Axis.Z)[-1], 0.5)

    # Parting groove all round, 6 mm below the rim.
    g = _rrect_solid(L + 2, W + 2, 0.6, p["corner_radius"], zt - 6.3) - _rrect_solid(L - 1.2, W - 1.2, 0.6,
                                                                                      p["corner_radius"] - 0.6,
                                                                                      zt - 6.3)
    tub -= g

    # Corner bosses for M3 heat-set inserts.
    bx, by = L / 2 - p["boss_inset"], W / 2 - p["boss_inset"]
    bh = zt - zf
    for x in (-bx, bx):
        for y in (-by, by):
            boss = Pos(x, y, zf + bh / 2) * Cylinder(p["boss_diameter"] / 2, bh)
            tub += boss
            tub -= Pos(x, y, zt - p["insert_depth"] / 2) * Cylinder(p["insert_hole"] / 2, p["insert_depth"])
            tub = tub  # keep the boss flush with the rim
    # Motor mounts.
    mz = p["motor_z"]
    for s in (-1, 1):
        y_in, y_wall = s * p["motor_inner_y"], s * (W / 2 - wall)
        y0, y1 = sorted((y_in + s * 3.0, y_wall))
        cradle = Pos(0, (y0 + y1) / 2, (zf + mz - 5.0) / 2) * Box(18.0, y1 - y0, mz - 5.0 - zf)
        for cx in (-7.4, 7.4):
            cradle += Pos(cx, (y0 + y1) / 2, (zf + mz + 2.0) / 2) * Box(2.8, y1 - y0, mz + 2.0 - zf)
        cradle = fillet(cradle.edges().group_by(Axis.Z)[-1].filter_by(Axis.Y), 0.6)
        for cx in (-7.4, 7.4):
            cradle -= Pos(cx, (y0 + y1) / 2 - s * 4, mz + 2.0) * Cylinder(0.8, 8)  # clamp-plate screw holes
        tub += cradle
        tub -= Pos(0, s * W / 2, mz) * Rot(90, 0, 0) * Cylinder(2.5, 3 * wall)
        for hx in (-4.5, 4.5):
            tub -= Pos(hx, s * W / 2, mz) * Rot(90, 0, 0) * Cylinder(0.9, 3 * wall)
            tub -= Pos(hx, s * W / 2, mz) * Rot(90, 0, 0) * Cylinder(1.8, 1.2)  # head counterbore, 0.6 deep
    # Battery bay: corner stops and strap slots.
    bxc = p["battery_x"]
    blx, bly = p["battery_size"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = bxc + sx * (blx / 2 + 0.5 + 0.8), sy * (bly / 2 - 6)
            stop = Pos(cx, cy, zf + 4.0) * Box(1.6, 10.0, 8.0)
            tub += fillet(stop.edges().group_by(Axis.Z)[-1], 0.6)
    tub += Pos(bxc, -(bly / 2 + 1.3), zf + 3.0) * Box(20.0, 1.6, 6.0)  # end stop (the leads exit at +Y)
    for sx in (-1, 1):
        for sy in (-1, 1):
            tub -= Pos(bxc + sx * (blx / 2 + 2.85), sy * 22.0, zb) * Box(2.0, 14.0, 3 * flo)
    # Caster recess (underside) and its insert bosses (inside).
    cxp = p["caster_x"]
    flange = SlotCenterToCenter(p["caster_hole_spacing"], 13.6).rotate(Axis.Z, 90)
    boss_h = p["caster_recess"] + 3.0 - flo
    boss = Pos(cxp, 0, zf) * extrude(SlotCenterToCenter(p["caster_hole_spacing"], 17.0).rotate(Axis.Z, 90), boss_h)
    boss += Pos(cxp, 0, zf + boss_h / 2) * Cylinder(12.3, boss_h)
    tub += fillet(boss.edges().group_by(Axis.Z)[-1], 1.0)
    tub -= Pos(cxp, 0, zb - 1) * extrude(flange, p["caster_recess"] + 1)
    tub -= Pos(cxp, 0, zb) * Cylinder(10.8, 2 * p["caster_recess"])
    for sy in (-1, 1):
        tub -= Pos(cxp, sy * p["caster_hole_spacing"] / 2, zb + p["caster_recess"] + 2.5) * Cylinder(2.0, 5.0)
    # Motor-driver standoffs (M2 self-tapping).
    dhx, dhy = p["driver_holes"]
    for x in (p["driver_x"] - dhx / 2, p["driver_x"] + dhx / 2):
        for y in (-dhy / 2, dhy / 2):
            so = Pos(x, y, zf + p["driver_standoff"] / 2) * Cylinder(2.5, p["driver_standoff"])
            tub += so
            tub -= Pos(x, y, zf + p["driver_standoff"]) * Cylinder(0.8, 6)
    # Rear wall: slide-switch cut-out (SS12D00 actuator window) with a chamfered surround.
    tub -= Pos(-L / 2, -24.0, 2.0) * Box(3 * wall, 9.0, 4.4)
    tub -= Pos(-L / 2, -24.0, 2.0) * Rot(0, 90, 0) * extrude(Sketch() + RectangleRounded(7.2, 13.0, 1.0), 0.6,
                                                             both=True)
    # Side panels ahead of the wheels: a shallow recess with five vent slots.
    for s in (-1, 1):
        y_face = s * W / 2
        panel = Pos(56.0, y_face, 0.0) * Rot(90, 0, 0) * extrude(Sketch() + RectangleRounded(34.0, 20.0, 3.0), 0.6,
                                                                  both=True)
        tub -= panel
        for i in range(5):
            tub -= Pos(44.0 + i * 6.0, y_face, 0.0) * Rot(90, 0, 0) * extrude(
                SlotCenterToCenter(10.0, 2.4).rotate(Axis.Z, 90), 3 * wall, both=True)
    # Front panel: the same recess with three horizontal slots.
    front = Pos(L / 2, 0, 1.0) * Rot(0, 90, 0) * extrude(Sketch() + RectangleRounded(20.0, 80.0, 3.0), 0.6,
                                                          both=True)
    tub -= front
    for i in range(3):
        tub -= Pos(L / 2, 0, -5.0 + i * 5.0) * Rot(0, 90, 0) * extrude(
            Rot(0, 0, 90) * SlotCenterToCenter(60.0, 2.4), 3 * wall, both=True)
    tub.label = "petg-white"
    return tub
