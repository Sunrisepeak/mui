"""The build backend of the ``openmui`` wheel (PEP 517), in the tree.

    pip install ./lang/python                  build with mcpp, install
    python -m build --wheel lang/python        a wheel in lang/python/dist

The extension is built by mcpp (``mcpp build -p openmui.entry.python``) and
found through mcpp's result record, never by guessing a path. The wheel is the
``openmui`` package (lang/python/python/openmui), the extension as
``_openmui.abi3.so`` (``.pyd`` on Windows), the shared libraries mcpp placed
beside it (they are found in the extension's own directory), the typing stubs
made from the extension's component catalogue, and the notices of the third
parties it carries. It is tagged ``cp310-abi3-<platform>``: the stable ABI, one
wheel for every CPython 3.10+ of a platform.

config_settings: ``profile`` (``release``, the default, or ``dev``) and
``mcpp`` (the program; else ``$OMUI_MCPP``, else ``mcpp`` on PATH).
"""

import base64
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import tempfile
import zipfile
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[1]  # lang/python
AGENT = "openmui_agent.py"                      # the agent's client: standard library only
ROOT = PACKAGE.parents[1]                       # the workspace (or the sdist)
NAME = "openmui"
TAG = "cp310-abi3"
# Entries of a wheel or an sdist carry this time unless SOURCE_DATE_EPOCH says
# another: the same sources make the same archive.
EPOCH = 315532800  # 1980-01-01, the earliest a zip can say


def _version():
    text = (PACKAGE / "mcpp.toml").read_text(encoding="utf-8")
    m = re.search(r'^\[package\][^\[]*?^version\s*=\s*"([^"]+)"', text, re.M | re.S)
    if not m:
        raise RuntimeError("lang/python/mcpp.toml states no [package] version")
    return m.group(1)


def _epoch():
    return int(os.environ.get("SOURCE_DATE_EPOCH", EPOCH))


def _platform():
    return re.sub(r"[-.]", "_", sysconfig.get_platform())


def _mcpp(settings):
    program = (settings or {}).get("mcpp") or os.environ.get("OMUI_MCPP") or shutil.which("mcpp")
    if not program:
        raise RuntimeError("openmui is built with mcpp: install it (xlings install mcpp), or say where it is "
                           "(--config-settings mcpp=PATH, or OMUI_MCPP)")
    return program


def _build_extension(settings):
    """Builds the extension; answers the artifacts of the build (mcpp's result record)."""
    profile = (settings or {}).get("profile", "release")
    if profile not in ("release", "dev"):
        raise RuntimeError(f"profile is release or dev, not {profile!r}")
    command = [_mcpp(settings), "build", "-p", "openmui.entry.python"] + (["--release"] if profile == "release" else [])
    print("openmui:", " ".join(command), file=sys.stderr)
    subprocess.run(command, cwd=ROOT, check=True)
    last = json.loads((ROOT / "target" / ".last-result.json").read_text(encoding="utf-8"))["data"]
    if last.get("status") != "ok":
        raise RuntimeError(f"mcpp's last build is {last.get('status')!r}")
    result = json.loads(Path(last["result"]).read_text(encoding="utf-8"))["data"]
    return [a for a in result.get("artifacts", []) if a.get("kind") == "shared"]


def _stage(artifacts, into):
    """The ``openmui`` package as the wheel holds it, in ``into/openmui``."""
    package = into / NAME
    shutil.copytree(PACKAGE / "python" / NAME, package, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    shutil.copy2(PACKAGE / "python" / AGENT, into / AGENT)
    extension = next((Path(a["path"]) for a in artifacts if a.get("name") == "_openmui"), None)
    if extension is None or not extension.is_file():
        raise RuntimeError("the build made no _openmui extension")
    windows = sys.platform == "win32"
    shutil.copy2(extension, package / ("_openmui.pyd" if windows else "_openmui.abi3.so"))
    # The shared libraries beside it, by the names the loader asks for: a
    # library's SONAME alias (libX11.so.6) when it has one, else its file.
    for a in artifacts:
        path = Path(a["path"])
        if path == extension:
            continue
        aliases = sorted(f for f in path.parent.iterdir() if f.is_file() and f.name.startswith(path.name + "."))
        for f in aliases or [path]:
            shutil.copy2(f, package / f.name)
    notices = extension.parent / "licenses"
    # The stubs, from the catalogue of the extension just staged.
    env = dict(os.environ, PYTHONPATH=str(into), OMUI_HEADLESS="1")
    subprocess.run([sys.executable, "-m", f"{NAME}._stubs", str(package / "__init__.pyi")], cwd=into, env=env, check=True)
    (package / "py.typed").write_text("", encoding="utf-8")
    # The official native modules' stubs (modules/ui/native/*/stubs/<name>.pyi):
    # `from openmui.native import fuzzy` type-checks.
    for stub in sorted((ROOT / "natives").glob("*/stubs/*.pyi")):
        shutil.copy2(stub, package / "native" / stub.name)
    return notices if notices.is_dir() else None


def _metadata(version):
    readme = (PACKAGE / "README.md").read_text(encoding="utf-8") if (PACKAGE / "README.md").exists() else ""
    lines = [
        "Metadata-Version: 2.4",
        f"Name: {NAME}",
        f"Version: {version}",
        "Summary: openmui for Python: one semantic tree for pixels, accessibility, agents and tests",
        "License-Expression: Apache-2.0",
        "Requires-Python: >=3.10",
        "Project-URL: Repository, https://github.com/Sunrisepeak/mui",
        "Classifier: Programming Language :: Python :: 3",
        "Classifier: Programming Language :: Python :: Implementation :: CPython",
        "Classifier: Topic :: Software Development :: User Interfaces",
        "Classifier: Typing :: Typed",
        "Description-Content-Type: text/markdown",
    ]
    return "\n".join(lines) + "\n\n" + readme


def _record_line(name, data):
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    return f"{name},sha256={digest},{len(data)}"


def _write_zip_entry(z, name, data, executable=False):
    info = zipfile.ZipInfo(name, date_time=_zip_time())
    info.external_attr = ((0o755 if executable else 0o644) | 0o100000) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    z.writestr(info, data)


def _zip_time():
    import time
    t = time.gmtime(max(_epoch(), EPOCH))
    return (t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec)


# ── PEP 517 ─────────────────────────────────────────────────────────────────

def get_requires_for_build_wheel(config_settings=None):
    return []


def get_requires_for_build_sdist(config_settings=None):
    return []


def _wheel_file():
    return f"Wheel-Version: 1.0\nGenerator: openmui_build\nRoot-Is-Purelib: false\nTag: {TAG}-{_platform()}\n"


def prepare_metadata_for_build_wheel(metadata_directory, config_settings=None):
    # What pip reads before it builds: no build needed for it.
    dist_info = Path(metadata_directory) / f"{NAME}-{_version()}.dist-info"
    dist_info.mkdir(parents=True, exist_ok=True)
    (dist_info / "METADATA").write_text(_metadata(_version()), encoding="utf-8")
    (dist_info / "WHEEL").write_text(_wheel_file(), encoding="utf-8")
    return dist_info.name


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    version = _version()
    artifacts = _build_extension(config_settings)
    wheel_name = f"{NAME}-{version}-{TAG}-{_platform()}.whl"
    dist_info = f"{NAME}-{version}.dist-info"
    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp)
        notices = _stage(artifacts, stage)
        entries = []
        for f in sorted(p for p in (stage / NAME).rglob("*") if p.is_file() and "__pycache__" not in p.parts):
            entries.append((f.relative_to(stage).as_posix(), f.read_bytes(), os.access(f, os.X_OK)))
        # The agent's client, a module of its own (it needs no extension).
        entries.append((AGENT, (stage / AGENT).read_bytes(), False))
        if notices:
            for f in sorted(p for p in notices.rglob("*") if p.is_file()):
                entries.append((f"{dist_info}/licenses/{f.relative_to(notices).as_posix()}", f.read_bytes(), False))
        wheel = _wheel_file()
        entries.append((f"{dist_info}/METADATA", _metadata(version).encode(), False))
        entries.append((f"{dist_info}/WHEEL", wheel.encode(), False))
        record = [_record_line(n, d) for n, d, _ in entries] + [f"{dist_info}/RECORD,,"]
        entries.append((f"{dist_info}/RECORD", ("\n".join(record) + "\n").encode(), False))
        out = Path(wheel_directory) / wheel_name
        with zipfile.ZipFile(out, "w") as z:
            for name, data, executable in entries:
                _write_zip_entry(z, name, data, executable)
    return wheel_name


def _sources():
    """The workspace's files an sdist carries: what git tracks, else the tree without build output."""
    try:
        listed = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True).stdout
        files = [ROOT / f for f in listed.decode().split("\0") if f]
    except (OSError, subprocess.CalledProcessError):
        skip = {".git", "target", "dist", "__pycache__"}
        files = [p for p in ROOT.rglob("*") if p.is_file() and not skip.intersection(p.relative_to(ROOT).parts)]
    return sorted(f for f in files if f.is_file())


def build_sdist(sdist_directory, config_settings=None):
    # The whole workspace, which the extension is built from, with this
    # package's pyproject at its root (the backend found by its new path).
    version = _version()
    base = f"{NAME}-{version}"
    name = f"{base}.tar.gz"
    pyproject = (PACKAGE / "pyproject.toml").read_text(encoding="utf-8").replace(
        'backend-path = ["packaging"]', 'backend-path = ["lang/python/packaging"]')
    pyproject = pyproject.replace('readme = "README.md"', 'readme = "lang/python/README.md"')

    def add(tar, arcname, data, mode=0o644):
        info = tarfile.TarInfo(f"{base}/{arcname}")
        info.size, info.mode, info.mtime = len(data), mode, _epoch()
        info.uname = info.gname = ""
        tar.addfile(info, io.BytesIO(data))

    with tarfile.open(Path(sdist_directory) / name, "w:gz", format=tarfile.PAX_FORMAT) as tar:
        add(tar, "PKG-INFO", _metadata(version).encode())
        add(tar, "pyproject.toml", pyproject.encode())
        for f in _sources():
            rel = f.relative_to(ROOT).as_posix()
            if rel == "pyproject.toml":
                continue
            add(tar, rel, f.read_bytes(), 0o755 if os.access(f, os.X_OK) else 0o644)
    return name
