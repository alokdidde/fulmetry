import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("own", Path(__file__).resolve().parents[2] / "tools" / "check_ownership.py")
own = importlib.util.module_from_spec(spec)
spec.loader.exec_module(own)


def test_stream_stays_in_its_directory():
    assert own.check("s2/pick-face-map", ["app/viewport/src/Pick.cpp", "docs/streams/s2.md"]) == []
    assert own.check("s2/pick-face-map", ["app/cad/src/Occt.cpp"]) == ["app/cad/src/Occt.cpp"]


def test_shared_files_belong_to_s10():
    assert own.check("s4/mujoco", ["CMakeLists.txt"]) == ["CMakeLists.txt"]
    assert own.check("s10/vcpkg-mujoco", ["vcpkg.json"]) == []


def test_contract_branches_and_integrator():
    assert own.check("contract/viewport-v1", ["app/core/include/fulmetry/core/IViewport.h"]) == []
    assert own.check("contract/viewport-v1", ["app/viewport/x.cpp"]) == ["app/viewport/x.cpp"]
    assert own.check("int/wire-shell", ["anything/at/all"]) == []
    assert own.check("main", ["anything"]) == []
