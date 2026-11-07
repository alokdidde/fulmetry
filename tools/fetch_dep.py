#!/usr/bin/env python3
"""Fetch a pinned heavy-engine dependency from tools/deps.json (owner: S10).

  python3 tools/fetch_dep.py fetch <name> <dest>   download/check out <name> into <dest> (idempotent)
  python3 tools/fetch_dep.py key <name>            print a cache key: <name>-<pin>[-<patch hash>]
  python3 tools/fetch_dep.py get <name> <field>    print one field of the pin

Names: mujoco (release tarball, sha256 checked), mujoco-macos (the release .dmg, macOS only: <dest> gets
mujoco.framework plus include/mujoco and lib/libmujoco.dylib pointing into it), wicked and raven (git commit; wicked also gets
app/viewport/wicked/patches/*.patch applied). A fetched <dest> carries a marker with its key, so a
second fetch (or a dir restored from a CI cache) is left untouched, mtimes included.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEPS = json.loads((ROOT / "tools" / "deps.json").read_text())


def pin(name):
    if name not in DEPS or name.startswith("_"):
        sys.exit(f"fetch_dep: unknown dependency '{name}' (known: {', '.join(k for k in DEPS if k[0] != '_')})")
    return DEPS[name]


def patches(dep):
    if "patches" not in dep:
        return []
    return sorted((ROOT / dep["patches"]).glob("*.patch"))


def key(name):
    dep = pin(name)
    k = f"{name}-{dep.get('commit') or dep['version']}"
    ps = patches(dep)
    if ps:
        h = hashlib.sha256()
        for p in ps:
            h.update(p.name.encode() + b"\0" + p.read_bytes())
        k += "-" + h.hexdigest()[:16]
    return k


def run(*args, cwd=None):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def fetch_tarball(dep, dest):
    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / "dep.tar.gz"
        print(f"download {dep['url']}", flush=True)
        with urllib.request.urlopen(dep["url"]) as r, open(archive, "wb") as f:
            shutil.copyfileobj(r, f)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != dep["sha256"]:
            sys.exit(f"fetch_dep: sha256 mismatch for {dep['url']}: got {digest}, pinned {dep['sha256']}")
        out = Path(tmp) / "x"
        with tarfile.open(archive) as t:
            t.extractall(out, filter="data")
        top = [p for p in out.iterdir()]
        src = top[0] if len(top) == 1 and top[0].is_dir() else out  # strip a single top-level directory
        if dest.exists():
            shutil.rmtree(dest)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), dest)


def download(dep, path):
    print(f"download {dep['url']}", flush=True)
    with urllib.request.urlopen(dep["url"]) as r, open(path, "wb") as f:
        shutil.copyfileobj(r, f)
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if digest != dep["sha256"]:
        sys.exit(f"fetch_dep: sha256 mismatch for {dep['url']}: got {digest}, pinned {dep['sha256']}")


def fetch_dmg(dep, dest):
    with tempfile.TemporaryDirectory() as tmp:
        image = Path(tmp) / "dep.dmg"
        download(dep, image)
        mount = Path(tmp) / "mnt"
        run("hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint", mount, image)
        try:
            found = sorted(mount.rglob("mujoco.framework"))
            if not found:
                sys.exit(f"fetch_dep: no mujoco.framework in {dep['url']}")
            if dest.exists():
                shutil.rmtree(dest)
            dest.mkdir(parents=True)
            run("ditto", found[0], dest / "mujoco.framework")
        finally:
            run("hdiutil", "detach", "-quiet", mount)
    fw = dest / "mujoco.framework"
    (dest / "include").mkdir()
    (dest / "include" / "mujoco").symlink_to(fw / "Headers", target_is_directory=True)
    libs = sorted((fw / "Versions" / "A").glob("libmujoco*.dylib"))
    if not libs:
        sys.exit("fetch_dep: no libmujoco*.dylib in mujoco.framework")
    (dest / "lib").mkdir()
    (dest / "lib" / "libmujoco.dylib").symlink_to(libs[0])


def fetch_git(dep, dest):
    if not (dest / ".git").exists():
        dest.mkdir(parents=True, exist_ok=True)
        run("git", "init", "-q", dest)
    run("git", "-C", dest, "fetch", "-q", "--depth", "1", dep["repo"], dep["commit"])
    run("git", "-C", dest, "checkout", "-q", "--force", "FETCH_HEAD")
    run("git", "-C", dest, "clean", "-q", "-fdx")
    for p in patches(dep):
        run("git", "-C", dest, "apply", p)


def fetch(name, dest):
    dep = pin(name)
    dest = Path(dest).expanduser().resolve()
    want = key(name)
    marker = dest / (".git/fulmetry-dep-key" if "repo" in dep else ".fulmetry-dep-key")
    if marker.exists() and marker.read_text().strip() == want:
        print(f"{name}: {dest} already at {want}")
        return
    if "repo" in dep:
        fetch_git(dep, dest)
    elif dep["url"].endswith(".dmg"):
        fetch_dmg(dep, dest)
    else:
        fetch_tarball(dep, dest)
    marker.write_text(want + "\n")
    print(f"{name}: {dest} at {want}")


def main(argv):
    if len(argv) == 3 and argv[0] == "fetch":
        fetch(argv[1], argv[2])
    elif len(argv) == 2 and argv[0] == "key":
        print(key(argv[1]))
    elif len(argv) == 3 and argv[0] == "get":
        print(pin(argv[1])[argv[2]])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
