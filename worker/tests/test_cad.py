import importlib.util
import json
import shutil
from pathlib import Path

import pytest

from fulmetry_worker import cad
from fulmetry_worker.contracts import errors, method_contracts
from fulmetry_worker.errors import BUILD_FAILED, ENGINE_MISSING, INVALID_PARAMS, SOURCE_NOT_FOUND
from fulmetry_worker.server import handle

ROVER = Path(__file__).resolve().parents[2] / "fixtures" / "rover"
WHEEL = "part:rover/wheel-l"
HAS_CAD = importlib.util.find_spec("build123d") is not None


def call(params, id_=1):
    return handle({"jsonrpc": "2.0", "id": id_, "method": "cad.build", "params": params})


def manifest_bounds(part_id):
    manifest = json.loads((ROVER / "fulmetry.project.json").read_text(encoding="utf-8"))
    return next(p["bounds"] for o in manifest["objects"] for p in o["parts"] if p["id"] == part_id)


@pytest.fixture
def project(tmp_path):
    """A copy of the rover's CAD sources, so builds never write into the repo."""
    shutil.copytree(ROVER / "cad", tmp_path / "cad")
    return tmp_path


# --- No engine needed -------------------------------------------------------------------------------------------

def test_missing_source_is_an_rpc_error(project):
    r = call({"projectRoot": str(project), "part": "part:rover/no-such-part"})
    assert r["error"]["code"] == SOURCE_NOT_FOUND
    assert r["error"]["data"]["expected"].endswith("no-such-part.py")


@pytest.mark.parametrize("part", ["wheel-l", "part:rover/../../etc", "part:Rover/wheel-l", "part:rover/a/b"])
def test_bad_part_ids_are_rejected(project, part):
    assert call({"projectRoot": str(project), "part": part})["error"]["code"] == INVALID_PARAMS


def test_hash_covers_source_and_params():
    assert cad.source_hash(b"x", {"a": 1}) == cad.source_hash(b"x", {"a": 1})
    assert cad.source_hash(b"x", {"a": 1}) != cad.source_hash(b"x", {"a": 2})
    assert cad.source_hash(b"x", {"a": 1}) != cad.source_hash(b"y", {"a": 1})
    assert cad.source_hash(b"x", {"a": 1, "b": 2}) == cad.source_hash(b"x", {"b": 2, "a": 1})


@pytest.mark.skipif(HAS_CAD, reason="build123d is installed")
def test_without_build123d_the_worker_says_so(project):
    assert call({"projectRoot": str(project), "part": WHEEL})["error"]["code"] == ENGINE_MISSING


# --- build123d ------------------------------------------------------------------------------------------------

@pytest.fixture(scope="module")
def built(tmp_path_factory):
    pytest.importorskip("build123d")
    root = tmp_path_factory.mktemp("rover")
    shutil.copytree(ROVER / "cad", root / "cad")
    r = call({"projectRoot": str(root), "part": WHEEL})
    assert "result" in r, r
    return root, r["result"]


def test_wheel_builds_to_step_and_glb(built):
    root, result = built
    assert errors(method_contracts()["cad.build"]["result"], result) == []
    step, glb = Path(result["stepFile"]), Path(result["meshFile"])
    assert step == root / ".fulmetry" / "build" / "rover" / "wheel-l.step"
    assert step.read_bytes().startswith(b"ISO-10303-21")
    assert glb.suffix == ".glb" and glb.read_bytes()[:4] == b"glTF"
    assert result["sourceHash"].startswith("sha256:")


def test_wheel_bounds_match_the_manifest(built):
    _, result = built
    want = manifest_bounds(WHEEL)  # 60 x 14 x 60, axle along Y
    for key in ("min", "max"):
        assert result["bounds"][key] == pytest.approx(want[key], abs=0.5)


def test_second_call_hits_the_cache(built):
    root, first = built
    files = [Path(first["stepFile"]), Path(first["meshFile"])]
    stamps = [f.stat().st_mtime_ns for f in files]
    again = call({"projectRoot": str(root), "part": WHEEL})["result"]
    assert again == first
    assert [f.stat().st_mtime_ns for f in files] == stamps


def test_changing_a_param_changes_the_hash_and_rebuilds(project):
    pytest.importorskip("build123d")
    base = cad.build(str(project), WHEEL)
    wider = cad.build(str(project), WHEEL, {"width": 20.0})
    assert wider["sourceHash"] != base["sourceHash"]
    assert wider["bounds"]["max"][1] - wider["bounds"]["min"][1] == pytest.approx(20.0, abs=0.01)
    # Editing PARAMS in the source changes the hash on the RPC path too.
    src = project / "cad" / "rover" / "wheel-l.py"
    src.write_text(src.read_text(encoding="utf-8").replace('"grooves": 20', '"grooves": 16'), encoding="utf-8")
    edited = call({"projectRoot": str(project), "part": WHEEL})["result"]
    assert edited["sourceHash"] not in {base["sourceHash"], wider["sourceHash"]}


def test_build_exception_is_an_rpc_error_and_the_worker_lives(project):
    pytest.importorskip("build123d")
    bad = project / "cad" / "rover" / "broken.py"
    bad.write_text("PARAMS = {}\ndef build(params):\n    raise ValueError('nope')\n", encoding="utf-8")
    r = call({"projectRoot": str(project), "part": "part:rover/broken"})
    assert r["error"]["code"] == BUILD_FAILED and "nope" in r["error"]["message"]
    bad.write_text("this is not python(\n", encoding="utf-8")
    assert call({"projectRoot": str(project), "part": "part:rover/broken"})["error"]["code"] == BUILD_FAILED
    assert "result" in handle({"jsonrpc": "2.0", "id": 2, "method": "worker.ping"})


# --- SolidCraft (cad-rs/, the fulmetry-cad binary) ----------------------------------------------------------------

CASTER = "part:rover/ball-caster"
needs_solidcraft = pytest.mark.skipif(cad.solidcraft_bin() is None, reason="fulmetry-cad is not built")


@needs_solidcraft
def test_rhai_part_builds_with_solidcraft_and_caches(project):
    r = call({"projectRoot": str(project), "part": CASTER})
    assert "result" in r, r
    result = r["result"]
    assert errors(method_contracts()["cad.build"]["result"], result) == []
    assert Path(result["stepFile"]).read_bytes().startswith(b"ISO-10303-21")
    glb = Path(result["meshFile"]).read_bytes()
    assert glb[:4] == b"glTF"
    gltf = json.loads(glb[20:20 + int.from_bytes(glb[12:16], "little")])
    assert [m["extras"]["key"] for m in gltf["materials"]] == ["plastic-black", "chrome"]
    for key in ("min", "max"):
        assert result["bounds"][key] == pytest.approx(manifest_bounds(CASTER)[key], abs=0.5)
    assert call({"projectRoot": str(project), "part": CASTER})["result"] == result
    assert "solidcraft" in cad.engines()


@needs_solidcraft
def test_rhai_overrides_and_errors(project):
    small = cad.build(str(project), CASTER, {"ball_diameter": 12.0, "housing_diameter": 15.0})
    assert small["bounds"]["min"][2] == pytest.approx(-6.0, abs=1e-3)
    bad = project / "cad" / "rover" / "broken.rhai"
    bad.write_text("fn params() { #{} }\nfn build(p) { box(10, 10, 10) - box(20, 20, 20) }\n", encoding="utf-8")
    r = call({"projectRoot": str(project), "part": "part:rover/broken"})
    assert r["error"]["code"] == BUILD_FAILED and "cut" in r["error"]["message"]
