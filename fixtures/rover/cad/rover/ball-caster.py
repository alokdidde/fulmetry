"""Ball caster: a 5/8" (Ø16 mm) chrome-steel ball in a black glass-filled nylon housing (Ø19 mm), as bought.

Frame: origin at the ball's centre, +Z up. The flange's top face (the mounting face against the tub's caster
recess) is at z = mount_z; two M3 clearance holes on the Y axis. The ball's lowest point (z = -8) is the floor.
Matches the Ø16 caster sphere of fixtures/rover/rover-wheels-*.mjcf (centre 22 mm below the axle).

Children are labelled with material keys (worker/fulmetry_worker/mesh.py).
"""
from math import cos, radians, sin

from build123d import Axis, Box, Compound, Cylinder, Part, Pos, SlotCenterToCenter, Sphere, chamfer, extrude

PARAMS = {
    "ball_diameter": 16.0,
    "housing_diameter": 19.0,
    "lip_z": -2.6,          # the housing wraps below the ball's equator to keep it in
    "mount_z": 9.0,
    "flange_thickness": 2.0,
    "hole_spacing": 25.0,
    "ribs": 8,
}


def build(params: dict) -> Part:
    p = {**PARAMS, **params}
    rb = p["ball_diameter"] / 2
    rh = p["housing_diameter"] / 2
    mz, ft = p["mount_z"], p["flange_thickness"]
    body_h = mz - ft - p["lip_z"]
    housing = Pos(0, 0, p["lip_z"] + body_h / 2) * Cylinder(rh, body_h)
    housing = chamfer(housing.edges().sort_by(Axis.Z)[:1], 1.2)
    flange = Pos(0, 0, mz - ft) * extrude(SlotCenterToCenter(p["hole_spacing"], 8.5 + 4.5), ft)
    flange = flange.rotate(Axis.Z, 90)  # lugs on the Y axis
    housing += flange
    for i in range(int(p["ribs"])):
        a = radians(360 * i / p["ribs"] + 22.5)
        if abs(sin(a)) > 0.9:  # leave the screw lugs clear
            continue
        z0, z1 = p["lip_z"] + 1.4, mz - ft
        housing += Pos((rh + 0.2) * cos(a), (rh + 0.2) * sin(a), (z0 + z1) / 2) * \
            Box(1.2, 1.4, z1 - z0).rotate(Axis.Z, 360 * i / p["ribs"] + 22.5)
    for s in (-1, 1):
        housing -= Pos(0, s * p["hole_spacing"] / 2, mz) * Cylinder(1.65, 10)
    housing -= Sphere(rb + 0.15)
    housing -= Pos(0, 0, mz) * Cylinder(2.5, 1.0)  # grease relief on top
    ball = Sphere(rb)
    housing.label, ball.label = "plastic-black", "chrome"
    return Compound(children=[housing, ball])
