//! GLB output in the same layout as worker/fulmetry_worker/mesh.py: one node, one mesh, one primitive per
//! material; children whose label is a palette key share that material, the rest use the default.

use serde::Deserialize;
use serde_json::{Value, json};

use crate::{MeshTolerance, Shape};

#[derive(Clone, Debug, Deserialize)]
pub struct Material {
    pub name: String,
    pub color: String,
    #[serde(default = "rough")]
    pub roughness: f64,
    #[serde(default)]
    pub metallic: f64,
    #[serde(default)]
    pub emissive: Option<String>,
}

fn rough() -> f64 {
    0.6
}

/// sRGB hex to linear RGB, as mesh.py's hex_rgb.
pub fn hex_rgb(hex: &str) -> Result<[f64; 3], String> {
    let h = hex.trim_start_matches('#');
    if h.len() != 6 {
        return Err(format!("colour must be #rrggbb, got {hex:?}"));
    }
    let mut out = [0.0; 3];
    for (i, o) in out.iter_mut().enumerate() {
        let c = u8::from_str_radix(&h[2 * i..2 * i + 2], 16).map_err(|e| e.to_string())? as f64 / 255.0;
        let lin = if c <= 0.04045 { c / 12.92 } else { ((c + 0.055) / 1.055).powf(2.4) };
        *o = (lin * 1e5).round() / 1e5;
    }
    Ok(out)
}

pub struct Written {
    pub triangles: usize,
    pub bytes: usize,
    pub materials: Vec<Value>,
}

/// Writes `shapes` to `path`; `palette` maps label keys to materials, `default` covers everything else.
pub fn write(
    shapes: &[Shape],
    path: &std::path::Path,
    palette: &serde_json::Map<String, Value>,
    default: &Material,
    tol: &MeshTolerance,
    name: &str,
) -> Result<Written, String> {
    let mut groups: Vec<(String, Material, Vec<&Shape>)> = Vec::new();
    for s in shapes {
        let (key, mat) = match s.label.as_deref().and_then(|l| palette.get(l).map(|m| (l, m))) {
            Some((l, m)) => (l.to_string(), serde_json::from_value::<Material>(m.clone()).map_err(|e| format!("palette {l}: {e}"))?),
            None => ("_default".to_string(), default.clone()),
        };
        match groups.iter_mut().find(|g| g.0 == key) {
            Some(g) => g.2.push(s),
            None => groups.push((key, mat, vec![s])),
        }
    }

    let mut bin: Vec<u8> = Vec::new();
    let (mut views, mut accessors, mut prims, mut materials) = (vec![], vec![], vec![], vec![]);
    let mut add_view = |bin: &mut Vec<u8>, data: &[u8], target: u32| -> usize {
        views.push(json!({"buffer": 0, "byteOffset": bin.len(), "byteLength": data.len(), "target": target}));
        bin.extend_from_slice(data);
        bin.resize(bin.len().next_multiple_of(4), 0);
        views.len() - 1
    };
    let mut tris_total = 0;
    for (key, mat, members) in &groups {
        let (mut pos, mut nor, mut idx): (Vec<f32>, Vec<f32>, Vec<u32>) = (vec![], vec![], vec![]);
        for s in members {
            let m = s.body.mesh(tol).map_err(|e| format!("mesh: {e}"))?;
            let base = (pos.len() / 3) as u32;
            for p in &m.positions {
                pos.extend([p.x as f32, p.y as f32, p.z as f32]);
            }
            for n in &m.normals {
                nor.extend([n.x as f32, n.y as f32, n.z as f32]);
            }
            idx.extend(m.triangles.iter().flat_map(|t| t.map(|i| i + base)));
        }
        if idx.is_empty() {
            continue;
        }
        tris_total += idx.len() / 3;
        let count = pos.len() / 3;
        let (mut lo, mut hi) = ([f32::INFINITY; 3], [f32::NEG_INFINITY; 3]);
        for p in pos.chunks(3) {
            for i in 0..3 {
                lo[i] = lo[i].min(p[i]);
                hi[i] = hi[i].max(p[i]);
            }
        }
        let bytes = |v: &[f32]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<u8>>();
        let pv = add_view(&mut bin, &bytes(&pos), 34962);
        let nv = add_view(&mut bin, &bytes(&nor), 34962);
        let (itype, idata): (u32, Vec<u8>) = if count < 65536 {
            (5123, idx.iter().flat_map(|&i| (i as u16).to_le_bytes()).collect())
        } else {
            (5125, idx.iter().flat_map(|i| i.to_le_bytes()).collect())
        };
        let iv = add_view(&mut bin, &idata, 34963);
        accessors.push(json!({"bufferView": pv, "componentType": 5126, "count": count, "type": "VEC3", "min": lo, "max": hi}));
        accessors.push(json!({"bufferView": nv, "componentType": 5126, "count": count, "type": "VEC3"}));
        accessors.push(json!({"bufferView": iv, "componentType": itype, "count": idx.len(), "type": "SCALAR"}));
        let a = accessors.len();
        let rgb = hex_rgb(&mat.color)?;
        let mut m = json!({
            "name": mat.name,
            "pbrMetallicRoughness": {"baseColorFactor": [rgb[0], rgb[1], rgb[2], 1.0], "metallicFactor": mat.metallic, "roughnessFactor": mat.roughness},
            "extras": {"key": key, "color": mat.color},
        });
        if let Some(e) = &mat.emissive {
            m["emissiveFactor"] = json!(hex_rgb(e)?);
        }
        materials.push(m);
        prims.push(
            json!({"attributes": {"POSITION": a - 3, "NORMAL": a - 2}, "indices": a - 1, "material": materials.len() - 1, "mode": 4}),
        );
    }

    let gltf = json!({
        "asset": {"version": "2.0", "generator": "fulmetry-cad (solidcraft-kernel)", "extras": {"fulmetry": {"units": "mm", "up": "+Z", "frame": "part"}}},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": prims}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": bin.len()}],
    });
    let mut js = serde_json::to_vec(&gltf).map_err(|e| e.to_string())?;
    js.resize(js.len().next_multiple_of(4), b' ');
    let total = 12 + 8 + js.len() + 8 + bin.len();
    let mut out = Vec::with_capacity(total);
    out.extend_from_slice(b"glTF");
    out.extend(2u32.to_le_bytes());
    out.extend((total as u32).to_le_bytes());
    out.extend((js.len() as u32).to_le_bytes());
    out.extend_from_slice(b"JSON");
    out.extend(js);
    out.extend((bin.len() as u32).to_le_bytes());
    out.extend_from_slice(b"BIN\0");
    out.extend(bin);
    std::fs::write(path, &out).map_err(|e| format!("{}: {e}", path.display()))?;

    let hints = materials
        .iter()
        .map(|m| {
            let p = &m["pbrMetallicRoughness"];
            json!({"key": m["extras"]["key"], "name": m["name"], "color": m["extras"]["color"], "baseColor": p["baseColorFactor"],
                   "roughness": p["roughnessFactor"], "metallic": p["metallicFactor"]})
        })
        .collect();
    Ok(Written { triangles: tris_total, bytes: total, materials: hints })
}
