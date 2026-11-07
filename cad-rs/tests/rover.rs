//! The ported rover parts build valid solids that match their build123d originals.

use fulmetry_cad::{build, default_params};
use serde_json::json;

const CASTER: &str = include_str!("../../fixtures/rover/cad/rover/ball-caster.rhai");

fn volume(s: &fulmetry_cad::Shape) -> f64 {
    solidcraft_kernel::measure(&s.body).expect("measure").volume
}

#[test]
fn ball_caster_matches_build123d() {
    let shapes = build(CASTER, &json!({})).expect("build");
    assert_eq!(shapes.iter().map(|s| s.label.as_deref()).collect::<Vec<_>>(), [Some("plastic-black"), Some("chrome")]);
    for s in &shapes {
        assert!(solidcraft_kernel::validity(&s.body).ok(), "{:?} is not a valid solid", s.label);
    }
    // build123d/OCCT give 1931.3 mm³ for the housing of ball-caster.py at its default params.
    assert!((volume(&shapes[0]) - 1931.3).abs() < 0.5, "housing volume {}", volume(&shapes[0]));
    let (lo, hi) = fulmetry_cad::bounds(&shapes).expect("bounds");
    for (got, want) in lo.iter().chain(&hi).zip([-9.7838, -19.0, -8.0, 9.7838, 19.0, 9.0]) {
        assert!((got - want).abs() < 1e-3, "bounds {lo:?}..{hi:?}");
    }
}

#[test]
fn overrides_change_the_part() {
    let shapes = build(CASTER, &json!({"ball_diameter": 12, "housing_diameter": 15.0})).expect("build");
    let (lo, _) = fulmetry_cad::bounds(&shapes).expect("bounds");
    assert!((lo[2] + 6.0).abs() < 1e-6, "ball bottom at {}", lo[2]);
    assert_eq!(default_params(CASTER).expect("params")["ribs"], json!(8));
}

#[test]
fn script_errors_are_reported_not_panics() {
    let bad = "fn params() { #{} } fn build(p) { fillet(box(10, 10, 10), [], -1) }";
    assert!(build(bad, &json!({})).unwrap_err().contains("fillet"));
    let runaway = "fn params() { #{} } fn build(p) { loop {} }";
    assert!(build(runaway, &json!({})).is_err());
    let no_build = "fn params() { #{} }";
    assert!(build(no_build, &json!({})).is_err());
}
