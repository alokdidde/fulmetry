//! Part scripts on SolidCraft's kernel.
//!
//! A part is a Rhai script (`cad/<object>/<slug>.rhai`) with two functions: `params()` returns the default
//! parameter map and `build(p)` returns one shape or an array of shapes, each optionally labelled with a
//! material key (`shape.label("chrome")`). Scripts are sandboxed: no file, network or process access, and a
//! cap on operations so a runaway loop fails instead of hanging the worker.
//!
//! Script API (mm, degrees, +Z up; primitives are centred on the origin like build123d's):
//! - solids: `box(x, y, z)`, `cylinder(r, h)`, `sphere(r)`, `torus(major, minor)`
//! - profiles on XY: `rect(w, h)`, `rrect(w, h, r)`, `circle(r)`, `slot(centres, width)`, `polygon([[x, y], ..])`,
//!   `profile.at(x, y)`, `profile - hole_profile`; `extrude(profile, h)` (z = 0..h), `revolve(profile, deg)`
//!   (profile x = radius, y = z, about the Z axis)
//! - placement: `s.at(x, y, z)`, `s.rotate("x" | "y" | "z", deg)`
//! - booleans: `a + b`, `a - b`, `a & b`
//! - topology: `s.edges()` / `s.faces()` give maps `#{x, y, z, length | area}` (midpoint / centroid) to filter
//!   with closures; `fillet(s, edges, r)`, `chamfer(s, edges, d)`, `shell(s, open_faces, thickness)`
//! - queries: `s.volume()`, `s.bounds()` (`#{min: [..], max: [..]}`)

use std::sync::Arc;

use rhai::{Array, Dynamic, Engine, EvalAltResult, Map, Position, Scope};
use solidcore_record::{Loop2, Plane, Region2, Seg2, Vec2, Vec3};
use solidcraft_kernel as k;

pub use solidcraft_kernel::{Body, MeshTolerance};

type Res<T> = Result<T, Box<EvalAltResult>>;

/// One solid of a part, with its material key if the script set one.
#[derive(Clone, Debug)]
pub struct Shape {
    pub body: Arc<Body>,
    pub label: Option<String>,
}

/// A closed 2D region on the XY plane: one outer loop and any holes.
#[derive(Clone, Debug)]
pub struct Profile(Region2);

fn err<T>(msg: impl Into<String>) -> Res<T> {
    Err(Box::new(EvalAltResult::ErrorRuntime(msg.into().into(), Position::NONE)))
}

fn kernel<T>(what: &str, r: k::Result<T>) -> Res<T> {
    r.or_else(|e| err(format!("{what}: {e}")))
}

fn num(d: &Dynamic) -> Res<f64> {
    if let Ok(f) = d.as_float() {
        Ok(f)
    } else if let Ok(i) = d.as_int() {
        Ok(i as f64)
    } else {
        err(format!("expected a number, got {}", d.type_name()))
    }
}

fn v3(x: f64, y: f64, z: f64) -> Vec3 {
    Vec3::new(x, y, z)
}

fn shape(body: Body) -> Shape {
    Shape { body: Arc::new(body), label: None }
}

fn first(what: &str, r: k::Result<Vec<Body>>) -> Res<Shape> {
    let bodies = kernel(what, r)?;
    match bodies.len() {
        0 => err(format!("{what} made no solid")),
        1 => Ok(shape(bodies.into_iter().next().expect("one body"))),
        _ => Ok(shape(kernel(what, k::union_all(&bodies.iter().collect::<Vec<_>>()))?)),
    }
}

fn boolean(a: &Shape, b: &Shape, op: k::BoolOp, what: &str) -> Res<Shape> {
    match kernel(what, k::boolean(&a.body, &b.body, op))? {
        Some(body) => Ok(Shape { body: Arc::new(body), label: a.label.clone() }),
        None => err(format!("{what} left nothing")),
    }
}

fn line(a: (f64, f64), b: (f64, f64)) -> Seg2 {
    Seg2::Line { a: Vec2::new(a.0, a.1), b: Vec2::new(b.0, b.1) }
}

fn arc(c: (f64, f64), radius: f64, start_deg: f64, sweep_deg: f64) -> Seg2 {
    Seg2::Arc { center: Vec2::new(c.0, c.1), radius, start: start_deg.to_radians(), sweep: sweep_deg.to_radians() }
}

fn outline(segs: Vec<Seg2>) -> Profile {
    Profile(Region2 { outer: Loop2 { segs }, holes: vec![] })
}

fn rrect(w: f64, h: f64, r: f64) -> Res<Profile> {
    let (x, y) = (w / 2.0, h / 2.0);
    if !(w > 0.0 && h > 0.0) || r < 0.0 || r * 2.0 > w.min(h) {
        return err(format!("rrect({w}, {h}, {r}): sizes must be positive and r at most half the short side"));
    }
    if r == 0.0 {
        return Ok(outline(vec![line((-x, -y), (x, -y)), line((x, -y), (x, y)), line((x, y), (-x, y)), line((-x, y), (-x, -y))]));
    }
    let mut segs = Vec::new();
    let side = |segs: &mut Vec<Seg2>, a: (f64, f64), b: (f64, f64)| {
        if (a.0 - b.0).hypot(a.1 - b.1) > 1e-9 {
            segs.push(line(a, b));
        }
    };
    side(&mut segs, (-x + r, -y), (x - r, -y));
    segs.push(arc((x - r, -y + r), r, -90.0, 90.0));
    side(&mut segs, (x, -y + r), (x, y - r));
    segs.push(arc((x - r, y - r), r, 0.0, 90.0));
    side(&mut segs, (x - r, y), (-x + r, y));
    segs.push(arc((-x + r, y - r), r, 90.0, 90.0));
    side(&mut segs, (-x, y - r), (-x, -y + r));
    segs.push(arc((-x + r, -y + r), r, 180.0, 90.0));
    Ok(outline(segs))
}

fn slot(centres: f64, width: f64) -> Res<Profile> {
    let (c, r) = (centres / 2.0, width / 2.0);
    if !(c > 0.0 && r > 0.0) {
        return err("slot(centres, width): both must be positive");
    }
    Ok(outline(vec![line((-c, -r), (c, -r)), arc((c, 0.0), r, -90.0, 180.0), line((c, r), (-c, r)), arc((-c, 0.0), r, 90.0, 180.0)]))
}

fn polygon(points: Array) -> Res<Profile> {
    let pts = points
        .iter()
        .map(|p| {
            let a = p.clone().into_array().or_else(|_| err("polygon points are [x, y] arrays"))?;
            if a.len() != 2 {
                return err("polygon points are [x, y] arrays");
            }
            Ok((num(&a[0])?, num(&a[1])?))
        })
        .collect::<Res<Vec<_>>>()?;
    if pts.len() < 3 {
        return err("polygon needs at least 3 points");
    }
    Ok(outline((0..pts.len()).map(|i| line(pts[i], pts[(i + 1) % pts.len()])).collect()))
}

fn shift(s: &Seg2, dx: f64, dy: f64) -> Seg2 {
    match *s {
        Seg2::Line { a, b } => line((a.x + dx, a.y + dy), (b.x + dx, b.y + dy)),
        Seg2::Arc { center, radius, start, sweep } => Seg2::Arc { center: Vec2::new(center.x + dx, center.y + dy), radius, start, sweep },
        _ => *s,
    }
}

fn profile_at(p: &mut Profile, dx: f64, dy: f64) -> Profile {
    let mv = |l: &Loop2| Loop2 { segs: l.segs.iter().map(|s| shift(s, dx, dy)).collect() };
    Profile(Region2 { outer: mv(&p.0.outer), holes: p.0.holes.iter().map(mv).collect() })
}

fn rotated(s: &Shape, axis: &str, deg: f64) -> Res<Shape> {
    let ax = match axis {
        "x" | "X" => Vec3::X,
        "y" | "Y" => Vec3::Y,
        "z" | "Z" => Vec3::Z,
        _ => return err(format!("rotate axis must be \"x\", \"y\" or \"z\", got {axis:?}")),
    };
    let body = kernel("rotate", k::transform(&s.body, Vec3::ZERO, Vec3::ZERO, ax, deg.to_radians()))?;
    Ok(Shape { body: Arc::new(body), label: s.label.clone() })
}

fn picks(items: &Array) -> Res<Vec<Vec3>> {
    items
        .iter()
        .map(|d| {
            let m = d.read_lock::<Map>().ok_or_else(|| Box::new(EvalAltResult::from("expected edge/face maps from edges() or faces()")))?;
            let g = |key: &str| m.get(key).map(num).unwrap_or_else(|| err(format!("edge/face map has no {key}")));
            Ok(v3(g("x")?, g("y")?, g("z")?))
        })
        .collect()
}

fn point_map(p: Vec3, extra: (&str, f64)) -> Dynamic {
    let mut m = Map::new();
    m.insert("x".into(), p.x.into());
    m.insert("y".into(), p.y.into());
    m.insert("z".into(), p.z.into());
    m.insert(extra.0.into(), extra.1.into());
    m.into()
}

/// Topology queries resolve to 0.01 mm: fine enough to tell features apart, coarse enough to be stable.
const TOPO_TOL: f64 = 0.01;

pub fn engine() -> Engine {
    let mut e = Engine::new();
    e.set_max_operations(5_000_000);
    e.set_max_call_levels(64);
    e.set_max_array_size(100_000);
    e.on_print(|s| eprintln!("[part] {s}"));
    e.register_type_with_name::<Shape>("Shape").register_type_with_name::<Profile>("Profile");

    e.register_fn("box", |x: Dynamic, y: Dynamic, z: Dynamic| -> Res<Shape> {
        let (x, y, z) = (num(&x)? / 2.0, num(&y)? / 2.0, num(&z)? / 2.0);
        Ok(shape(kernel("box", k::box_solid(v3(-x, -y, -z), v3(x, y, z)))?))
    });
    e.register_fn("cylinder", |r: Dynamic, h: Dynamic| -> Res<Shape> {
        let h = num(&h)?;
        Ok(shape(kernel("cylinder", k::cylinder(v3(0.0, 0.0, -h / 2.0), Vec3::Z, num(&r)?, h))?))
    });
    e.register_fn("sphere", |r: Dynamic| -> Res<Shape> { Ok(shape(kernel("sphere", k::sphere(Vec3::ZERO, num(&r)?))?)) });
    e.register_fn("torus", |a: Dynamic, b: Dynamic| -> Res<Shape> {
        Ok(shape(kernel("torus", k::torus(Vec3::ZERO, num(&a)?, num(&b)?))?))
    });

    e.register_fn("rect", |w: Dynamic, h: Dynamic| rrect(num(&w)?, num(&h)?, 0.0));
    e.register_fn("rrect", |w: Dynamic, h: Dynamic, r: Dynamic| rrect(num(&w)?, num(&h)?, num(&r)?));
    e.register_fn("circle", |r: Dynamic| -> Res<Profile> {
        Ok(Profile(Region2 { outer: Loop2::circle(Vec2::new(0.0, 0.0), num(&r)?), holes: vec![] }))
    });
    e.register_fn("slot", |c: Dynamic, w: Dynamic| slot(num(&c)?, num(&w)?));
    e.register_fn("polygon", polygon);
    e.register_fn("at", |p: &mut Profile, x: Dynamic, y: Dynamic| -> Res<Profile> { Ok(profile_at(p, num(&x)?, num(&y)?)) });
    e.register_fn("-", |a: Profile, b: Profile| -> Profile {
        let mut r = a.0;
        r.holes.push(b.0.outer);
        r.holes.extend(b.0.holes);
        Profile(r)
    });
    e.register_fn("extrude", |p: Profile, h: Dynamic| -> Res<Shape> {
        let h = num(&h)?;
        let (a, b) = if h >= 0.0 { (0.0, h) } else { (h, 0.0) };
        first("extrude", k::extrude(&Plane::XY, &[p.0], a, b))
    });
    e.register_fn("revolve", |p: Profile, deg: Dynamic| -> Res<Shape> {
        // Sketch x = radius (+X), sketch y = +Z; the axis is the sketch's y axis.
        let plane = Plane { origin: Vec3::ZERO, x: Vec3::X, y: Vec3::Z };
        first("revolve", k::revolve(&plane, &[p.0], Vec2::new(0.0, 0.0), Vec2::new(0.0, 1.0), num(&deg)?.to_radians()))
    });

    e.register_fn("at", |s: &mut Shape, x: Dynamic, y: Dynamic, z: Dynamic| -> Res<Shape> {
        let body = kernel("at", k::transform(&s.body, v3(num(&x)?, num(&y)?, num(&z)?), Vec3::ZERO, Vec3::Z, 0.0))?;
        Ok(Shape { body: Arc::new(body), label: s.label.clone() })
    });
    e.register_fn("rotate", |s: &mut Shape, axis: &str, deg: Dynamic| rotated(s, axis, num(&deg)?));
    e.register_fn("+", |a: Shape, b: Shape| boolean(&a, &b, k::BoolOp::Union, "union"));
    e.register_fn("-", |a: Shape, b: Shape| boolean(&a, &b, k::BoolOp::Cut, "cut"));
    e.register_fn("&", |a: Shape, b: Shape| boolean(&a, &b, k::BoolOp::Intersect, "intersect"));
    e.register_fn("label", |s: &mut Shape, key: &str| -> Shape { Shape { body: s.body.clone(), label: Some(key.to_string()) } });

    e.register_fn("edges", |s: &mut Shape| -> Res<Array> {
        Ok(kernel("edges", s.body.edges(TOPO_TOL))?.into_iter().map(|e| point_map(e.mid, ("length", e.length))).collect())
    });
    e.register_fn("faces", |s: &mut Shape| -> Res<Array> {
        Ok(kernel("faces", s.body.faces(TOPO_TOL))?.into_iter().map(|f| point_map(f.centroid, ("area", f.area))).collect())
    });
    e.register_fn("fillet", |s: Shape, edges: Array, r: Dynamic| -> Res<Shape> {
        let body = kernel("fillet", k::fillet(&s.body, &picks(&edges)?, num(&r)?))?;
        Ok(Shape { body: Arc::new(body), label: s.label })
    });
    e.register_fn("chamfer", |s: Shape, edges: Array, d: Dynamic| -> Res<Shape> {
        let body = kernel("chamfer", k::chamfer(&s.body, &picks(&edges)?, num(&d)?))?;
        Ok(Shape { body: Arc::new(body), label: s.label })
    });
    e.register_fn("shell", |s: Shape, faces: Array, t: Dynamic| -> Res<Shape> {
        let body = kernel("shell", k::shell(&s.body, &picks(&faces)?, num(&t)?))?;
        Ok(Shape { body: Arc::new(body), label: s.label })
    });
    e.register_fn("volume", |s: &mut Shape| -> Res<f64> { Ok(kernel("volume", k::measure(&s.body))?.volume) });
    e.register_fn("bounds", |s: &mut Shape| -> Res<Map> { bounds_map(std::slice::from_ref(s)) });
    e
}

/// Axis-aligned bounds of all shapes: (min, max).
pub fn bounds(shapes: &[Shape]) -> Result<([f64; 3], [f64; 3]), String> {
    let mut lo = [f64::INFINITY; 3];
    let mut hi = [f64::NEG_INFINITY; 3];
    for s in shapes {
        let b = k::measure(&s.body).map_err(|e| e.to_string())?.bbox;
        for (i, (a, z)) in [(b.min.x, b.max.x), (b.min.y, b.max.y), (b.min.z, b.max.z)].into_iter().enumerate() {
            lo[i] = lo[i].min(a);
            hi[i] = hi[i].max(z);
        }
    }
    Ok((lo, hi))
}

fn bounds_map(shapes: &[Shape]) -> Res<Map> {
    let (lo, hi) = bounds(shapes).or_else(err)?;
    let arr = |v: [f64; 3]| -> Dynamic { v.iter().map(|&x| Dynamic::from(x)).collect::<Array>().into() };
    let mut m = Map::new();
    m.insert("min".into(), arr(lo));
    m.insert("max".into(), arr(hi));
    Ok(m)
}

/// The script's default parameters (`params()`), as JSON.
pub fn default_params(source: &str) -> Result<serde_json::Value, String> {
    let e = engine();
    let ast = e.compile(source).map_err(|x| format!("compile: {x}"))?;
    let p: Map = e.call_fn(&mut Scope::new(), &ast, "params", ()).map_err(|x| format!("params(): {x}"))?;
    rhai::serde::from_dynamic(&p.into()).map_err(|x| x.to_string())
}

/// Runs `build(params() + overrides)` and returns its shapes.
pub fn build(source: &str, overrides: &serde_json::Value) -> Result<Vec<Shape>, String> {
    let _kernel = k::hold_a_kernel();
    let e = engine();
    let ast = e.compile(source).map_err(|x| format!("compile: {x}"))?;
    let mut scope = Scope::new();
    let mut p: Map = e.call_fn(&mut scope, &ast, "params", ()).map_err(|x| format!("params(): {x}"))?;
    if let Some(o) = overrides.as_object() {
        for (key, val) in o {
            p.insert(key.as_str().into(), rhai::serde::to_dynamic(val).map_err(|x| x.to_string())?);
        }
    }
    let out: Dynamic = e.call_fn(&mut scope, &ast, "build", (p,)).map_err(|x| format!("build(): {x}"))?;
    let shapes: Vec<Shape> = if out.is::<Shape>() {
        vec![out.cast()]
    } else if out.is_array() {
        out.cast::<Array>()
            .into_iter()
            .map(|d| d.try_cast::<Shape>().ok_or_else(|| "build() array must hold only shapes".to_string()))
            .collect::<Result<_, _>>()?
    } else {
        return Err(format!("build() must return a shape or an array of shapes, got {}", out.type_name()));
    };
    if shapes.is_empty() {
        return Err("build() returned no shapes".into());
    }
    for (i, s) in shapes.iter().enumerate() {
        let v = k::validity(&s.body);
        if !v.ok() {
            return Err(format!("build() shape {i} ({}) is not a valid solid: {v:?}", s.label.as_deref().unwrap_or("unlabelled")));
        }
    }
    Ok(shapes)
}

pub mod glb;
