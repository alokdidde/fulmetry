// Ball caster: a 5/8" (Ø16 mm) chrome-steel ball in a black glass-filled nylon housing (Ø19 mm), as bought.
//
// Frame: origin at the ball's centre, +Z up. The flange's top face (the mounting face against the tub's caster
// recess) is at z = mount_z; two M3 clearance holes on the Y axis. The ball's lowest point (z = -8) is the floor.
// Matches the Ø16 caster sphere of fixtures/rover/rover-wheels-*.mjcf (centre 22 mm below the axle).
//
// Built by SolidCraft's kernel (cad-rs/). Shapes are labelled with material keys (worker/fulmetry_worker/mesh.py).

fn params() {
    #{
        ball_diameter: 16.0,
        housing_diameter: 19.0,
        lip_z: -2.6,          // the housing wraps below the ball's equator to keep it in
        mount_z: 9.0,
        flange_thickness: 2.0,
        hole_spacing: 25.0,
        ribs: 8,
    }
}

fn build(p) {
    let rb = p.ball_diameter / 2.0;
    let rh = p.housing_diameter / 2.0;
    let mz = p.mount_z;
    let ft = p.flange_thickness;
    let body_h = mz - ft - p.lip_z;

    let housing = cylinder(rh, body_h).at(0, 0, p.lip_z + body_h / 2.0);
    let bottom = housing.edges().filter(|e| e.z < p.lip_z + 0.01);
    housing = chamfer(housing, bottom, 1.2);
    housing += extrude(slot(p.hole_spacing, 8.5 + 4.5), ft).rotate("z", 90).at(0, 0, mz - ft);

    for i in 0..p.ribs {
        let deg = 360.0 * i / p.ribs + 22.5;
        if deg.to_radians().sin().abs() > 0.9 { continue; }  // leave the screw lugs clear
        let z0 = p.lip_z + 1.4;
        let z1 = mz - ft;
        housing += box(1.2, 1.4, z1 - z0).at(rh + 0.2, 0, (z0 + z1) / 2.0).rotate("z", deg);
    }
    for s in [-1, 1] {
        housing -= cylinder(1.65, 10).at(0, s * p.hole_spacing / 2.0, mz);
    }
    housing -= sphere(rb + 0.15);
    housing -= cylinder(2.5, 1.0).at(0, 0, mz);  // grease relief on top

    [housing.label("plastic-black"), sphere(rb).label("chrome")]
}
