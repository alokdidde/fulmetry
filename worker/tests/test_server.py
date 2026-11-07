import json
import subprocess
import sys
from pathlib import Path

from fulmetry_worker.server import INVALID_PARAMS, METHOD_NOT_FOUND, NOT_IMPLEMENTED, handle

FIXTURE = Path(__file__).resolve().parents[2] / "fixtures" / "rover" / "fulmetry.project.json"


def call(method, params=None, id_=1):
    return handle({"jsonrpc": "2.0", "id": id_, "method": method, "params": params or {}})


def test_ping():
    r = call("worker.ping")
    assert r["result"]["version"]


def test_validate_fixture():
    manifest = json.loads(FIXTURE.read_text(encoding="utf-8"))
    assert call("project.validate", {"manifest": manifest})["result"] == {"valid": True, "errors": []}


def test_validate_reports_paths():
    r = call("project.validate", {"manifest": {"schemaVersion": 1, "name": "x", "objects": [{"id": "BAD"}]}})
    assert r["result"]["valid"] is False
    assert any(e["path"].startswith("/objects/0") for e in r["result"]["errors"])


def test_params_are_checked_against_the_contract():
    assert call("project.validate", {})["error"]["code"] == INVALID_PARAMS


def test_unknown_and_unimplemented_methods():
    assert call("nope")["error"]["code"] == METHOD_NOT_FOUND
    r = call("print.slice", {"projectRoot": ".", "part": "part:rover/wheel-l", "material": "PLA"})
    assert r["error"]["code"] == NOT_IMPLEMENTED


def test_stdio_round_trip():
    worker_dir = Path(__file__).resolve().parents[1]
    p = subprocess.run([sys.executable, "-m", "fulmetry_worker.server"], cwd=worker_dir,
                       input='{"jsonrpc":"2.0","id":7,"method":"worker.ping"}\nnot json\n',
                       capture_output=True, text=True, timeout=30)
    lines = [json.loads(l) for l in p.stdout.splitlines()]
    assert lines[0]["id"] == 7 and "result" in lines[0]
    assert lines[1]["error"]["code"] == -32700
