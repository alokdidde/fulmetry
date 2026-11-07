//! fulmetry-cad: the worker's SolidCraft engine.
//!
//!   fulmetry-cad version                 prints {"fulmetry-cad", "solidcraft"} versions
//!   fulmetry-cad params <part.rhai>      prints the script's default params as JSON
//!   fulmetry-cad build < request.json    builds one part; prints {"bounds", "triangles", "materials"}
//!
//! The build request: {"source": path, "overrides": {..}, "step": path, "glb": path, "name": str,
//! "palette": {key: material}, "default": material, "chord": mm, "angle": rad}. Errors go to stderr, exit 1.

use std::io::Read;
use std::path::PathBuf;

use fulmetry_cad::{MeshTolerance, build, default_params, glb};
use serde::Deserialize;
use serde_json::{Value, json};

/// Keep in step with the solidcraft-kernel rev in Cargo.toml.
const SOLIDCRAFT_REV: &str = "0.40.0-rc.2+9325e3a";

#[derive(Deserialize)]
struct Request {
    source: PathBuf,
    #[serde(default)]
    overrides: Value,
    step: PathBuf,
    glb: PathBuf,
    name: String,
    #[serde(default)]
    palette: serde_json::Map<String, Value>,
    default: glb::Material,
    chord: f64,
    angle: f64,
}

fn run() -> Result<Value, String> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.iter().map(String::as_str).collect::<Vec<_>>().as_slice() {
        ["version"] => Ok(json!({"fulmetry-cad": env!("CARGO_PKG_VERSION"), "solidcraft": SOLIDCRAFT_REV})),
        ["params", path] => {
            let src = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
            default_params(&src)
        }
        ["build"] => {
            let mut raw = String::new();
            std::io::stdin().read_to_string(&mut raw).map_err(|e| e.to_string())?;
            let req: Request = serde_json::from_str(&raw).map_err(|e| format!("request: {e}"))?;
            let src = std::fs::read_to_string(&req.source).map_err(|e| format!("{}: {e}", req.source.display()))?;
            let shapes = build(&src, &req.overrides)?;
            let (lo, hi) = fulmetry_cad::bounds(&shapes)?;
            let bodies: Vec<_> = shapes.iter().map(|s| &*s.body).collect();
            let step = solidcraft_kernel::step_export(&bodies, "Fulmetry").map_err(|e| format!("STEP export: {e}"))?;
            std::fs::write(&req.step, step).map_err(|e| format!("{}: {e}", req.step.display()))?;
            let tol = MeshTolerance { chord: req.chord, angle: Some(req.angle) };
            let w = glb::write(&shapes, &req.glb, &req.palette, &req.default, &tol, &req.name)?;
            let r4 = |v: [f64; 3]| v.map(|x| (x * 1e4).round() / 1e4);
            Ok(json!({"bounds": {"min": r4(lo), "max": r4(hi)}, "triangles": w.triangles, "bytes": w.bytes, "materials": w.materials}))
        }
        _ => Err("usage: fulmetry-cad version | fulmetry-cad params <part.rhai> | fulmetry-cad build < request.json".into()),
    }
}

fn main() {
    match run() {
        Ok(v) => println!("{v}"),
        Err(e) => {
            eprintln!("{e}");
            std::process::exit(1);
        }
    }
}
