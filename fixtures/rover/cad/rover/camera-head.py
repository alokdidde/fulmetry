"""Camera head: a red PLA housing for an OV2640 camera module with an M12 lens, on the pan servo's horn.

Frame: origin on the pan axis at the underside of the head (which sits on the servo horn), +X forward (the lens),
+Z up. The housing is two printed halves split horizontally, held by four M2 screws; the lens barrel, knurled focus
ring, glass, status LED and screws are separate children with their own material keys
(worker/fulmetry_worker/mesh.py).
"""
from math import cos, radians, sin

from build123d import (Axis, Box, Compound, Cylinder, Part, Pos, RectangleRounded, Rot, Sketch, SlotCenterToCenter,
                       chamfer, extrude, fillet)

PARAMS = {
    "length": 40.0,       # X
    "width": 36.0,        # Y
    "height": 30.0,       # Z
    "corner_radius": 5.0,
    "edge_radius": 2.0,
    "split_z": 11.0,      # parting line between the two halves
    "lens_z": 15.5,
    "lens_y": 1.5,
    "lens_mount_diameter": 16.0,
    "lens_ring_diameter": 14.0,
    "lens_barrel_diameter": 10.0,
    "lens_protrusion": 8.5,
    "ring_ribs": 30,
}


def _face_x(x: float, sketch: Sketch, depth: float, y: float = 0.0, z: float = 0.0) -> Part:
    """Extrude a YZ-plane sketch from x into the part (towards -X by depth)."""
    return Pos(x, y, z) * Rot(0, -90, 0) * extrude(sketch, depth)


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    L, W, H = p["length"], p["width"], p["height"]
    shell = Pos(0, 0, H / 2) * Box(L, W, H)
    shell = fillet(shell.edges().filter_by(Axis.Z), p["corner_radius"])
    shell = fillet(shell.edges().group_by(Axis.Z)[-1], p["edge_radius"])
    shell = fillet(shell.edges().group_by(Axis.Z)[0], 1.0)

    xf = L / 2
    # Parting line between the halves, all round.
    sz = p["split_z"]
    ring = Pos(0, 0, sz) * (Box(L + 2, W + 2, 0.5) - fillet(Box(L - 0.8, W - 0.8, 0.5).edges().filter_by(Axis.Z),
                                                            p["corner_radius"] - 0.4))
    shell -= ring
    # Front bezel: a shallow rounded recess the camera looks out of.
    bezel = Sketch() + RectangleRounded(H - 9, W - 9, 3.5)  # first size -> Z, second -> Y
    shell -= _face_x(xf + 0.01, bezel, 0.8, 0, H / 2 + 0.5)
    # Side vents: three slots per side, towards the back.
    for sy in (-1, 1):
        for k in range(3):
            slot = Pos(-7, sy * W / 2, 9 + 5.0 * k) * Rot(90, 0, 0) * extrude(SlotCenterToCenter(14, 2.2), 2.4,
                                                                               both=True)
            shell -= slot
    # Back: USB-C port and a reset pinhole.
    usb = Sketch() + Rot(0, 0, 90) * SlotCenterToCenter(9.0 - 3.4, 3.4)
    shell -= _face_x(-xf + 3.0, usb, 4.0, 0, 8.0)
    shell -= Pos(-xf, 9, 8) * Rot(0, 90, 0) * Cylinder(0.7, 4)
    # Top: four countersunk M2 screw holes into the lower half's bosses.
    sx, sy_ = L / 2 - 6.5, W / 2 - 6.5
    screws = None
    for x in (-sx, sx):
        for y in (-sy_, sy_):
            shell -= Pos(x, y, H) * Cylinder(2.3, 2.4)
            head = Pos(x, y, H - 1.4) * Cylinder(1.9, 1.2)
            head -= Pos(x, y, H - 0.6) * (Box(2.4, 0.45, 0.8) + Box(0.45, 2.4, 0.8))
            screws = head if screws is None else screws + head
    # Underside: pocket for the horn screw and a shallow slot that keys the horn arms.
    shell -= Pos(0, 0, 0) * Cylinder(2.5, 2.4)
    shell -= Pos(0, 0, 0) * extrude(SlotCenterToCenter(26, 7.2), 0.6, both=True)

    # Camera: a black M12 lens mount, knurled focus ring, barrel and glass.
    lz, ly = p["lens_z"], p["lens_y"]
    shell -= Pos(xf - 1.5, ly, lz) * Rot(0, 90, 0) * Cylinder(p["lens_mount_diameter"] / 2 + 0.4, 6)
    mount_len = 3.0
    mount = Pos(xf - 1.6 + mount_len / 2, ly, lz) * Rot(0, 90, 0) * Cylinder(p["lens_mount_diameter"] / 2, mount_len + 1.4)
    mount = chamfer(mount.edges().sort_by(Axis.X)[-1:], 0.6)
    ring_len = 3.4
    x0 = xf + 1.8
    rr = p["lens_ring_diameter"] / 2
    focus = Pos(x0 + ring_len / 2, ly, lz) * Rot(0, 90, 0) * Cylinder(rr - 0.5, ring_len)
    n = int(p["ring_ribs"])
    for i in range(n):
        a = radians(360 * i / n)
        focus += Pos(x0 + ring_len / 2, ly + (rr - 0.5) * cos(a), lz + (rr - 0.5) * sin(a)) * Rot(360 * i / n, 0, 0) \
            * Box(ring_len - 0.6, 0.7, 1.0)
    barrel_len = p["lens_protrusion"] - 1.8 - ring_len
    xb = x0 + ring_len
    barrel = Pos(xb + barrel_len / 2, ly, lz) * Rot(0, 90, 0) * Cylinder(p["lens_barrel_diameter"] / 2, barrel_len)
    barrel = chamfer(barrel.edges().sort_by(Axis.X)[-1:], 0.5)
    tip = xb + barrel_len
    barrel -= Pos(tip, ly, lz) * Rot(0, 90, 0) * Cylinder(3.6, 1.6)
    glass = Pos(tip - 0.75, ly, lz) * Rot(0, 90, 0) * Cylinder(3.6, 0.3)
    lens_black = mount + focus + barrel
    # Status LED behind a small window, top right of the bezel, and two M2 screws holding the camera board.
    led = Pos(xf - 0.6, -W / 2 + 8.0, H - 7.5) * fillet(Box(1.4, 3.2, 3.2).edges().filter_by(Axis.X), 0.6)
    shell -= Pos(xf - 0.8, -W / 2 + 8.0, H - 7.5) * Box(1.6, 3.4, 3.4)
    for y in (ly - 11.0, ly + 11.0):
        shell -= Pos(xf - 0.8, y, lz) * Rot(0, 90, 0) * Cylinder(2.0, 0.6)
        head = Pos(xf - 1.0, y, lz) * Rot(0, 90, 0) * Cylinder(1.8, 1.4)
        head = fillet(head.edges().sort_by(Axis.X)[-1:], 0.5)
        head -= Pos(xf - 0.3, y, lz) * (Box(1.0, 2.0, 0.45) + Box(1.0, 0.45, 2.0))
        screws += head

    out = [(shell, "pla-red"), (lens_black, "plastic-black"), (glass, "glass"), (led, "led-amber"),
           (screws, "steel")]
    children = []
    for solid, key in out:
        solid.label = key
        children.append(solid)
    return Compound(children=children)
