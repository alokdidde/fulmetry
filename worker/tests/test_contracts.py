import json
from pathlib import Path

import jsonschema
import pytest

from fulmetry_worker.contracts import ROOT, errors, method_contracts, project_schema

FIXTURE = ROOT.parent / "fixtures" / "rover" / "fulmetry.project.json"


def test_schemas_are_valid_json_schema():
    jsonschema.Draft202012Validator.check_schema(project_schema())
    for c in method_contracts().values():
        jsonschema.Draft202012Validator.check_schema(c["params"])
        jsonschema.Draft202012Validator.check_schema(c["result"])
    ext = json.loads((ROOT / "agent" / "fulmetry-ext.schema.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(ext)


def test_rover_fixture_matches_schema():
    assert errors(project_schema(), json.loads(FIXTURE.read_text(encoding="utf-8"))) == []


def test_every_contract_names_its_milestone():
    for name, c in method_contracts().items():
        assert c["since"] in {"M0", "M1", "M2", "M3", "M4", "M5", "M6"}, name
        assert Path(ROOT / "worker" / f"{name}.json").exists()


@pytest.mark.parametrize("bad", [
    {"schemaVersion": 2, "name": "x", "objects": []},
    {"schemaVersion": 1, "name": "", "objects": []},
    {"schemaVersion": 1, "name": "x", "objects": [{"id": "Rover", "name": "r", "parts": []}]},
])
def test_bad_manifests_are_rejected(bad):
    assert errors(project_schema(), bad)
