"""Setup package: bundled-mod verification, deploy targets, CLI, packaging
(`install_apworld.py --bundle-mod`) and the release audit.

No Archipelago or Kivy needed; everything runs against tmp dirs with a
synthetic mod (a minimal NSO + META that pass the audit's checks).
"""

from __future__ import annotations

import importlib
import json
import struct
import sys
import zipfile
from pathlib import Path

import pytest

from _setup import bundle as bundle_mod
from _setup import cli, deploy, state
from _setup.bundle import BundleError, load_from_dir, make_manifest, verify

REPO = Path(__file__).resolve().parents[3]
TITLE_ID = 0x0100152000022000


def fake_nso(extra: bytes = b"") -> bytes:
    """Uncompressed NSO (flags 0) whose .rodata carries our module name."""
    text = b"\x1f\x20\x03\xd5" * 16
    ro = b"\x00mk8dx_archipelago\x00127.0.0.1\x00" + extra
    data = b"\x00" * 32
    hdr = bytearray(0x100)
    hdr[0:4] = b"NSO0"
    off = 0x100
    for i, seg in enumerate((text, ro, data)):
        struct.pack_into("<III", hdr, 0x10 + 0x10 * i, off, 0, len(seg))
        struct.pack_into("<I", hdr, 0x60 + 4 * i, len(seg))
        off += len(seg)
    return bytes(hdr) + text + ro + data


def fake_npdm() -> bytes:
    return b"META" + b"\x00" * 60 + struct.pack("<Q", TITLE_ID) + b"\x00" * 16


@pytest.fixture
def mod_dir(tmp_path: Path) -> Path:
    d = tmp_path / "exefs"
    d.mkdir()
    (d / "subsdk9").write_bytes(fake_nso())
    (d / "main.npdm").write_bytes(fake_npdm())
    return d


@pytest.fixture(autouse=True)
def appdata(tmp_path: Path, monkeypatch) -> Path:
    root = tmp_path / "appdata"
    monkeypatch.setenv("MK8AP_APPDATA_ROOT", str(root))
    return root


# ---- bundle ----------------------------------------------------------------

def test_manifest_round_trip(mod_dir: Path) -> None:
    b = load_from_dir(mod_dir)
    assert b.version == "local"
    files = {n: (mod_dir / n).read_bytes() for n in ("subsdk9", "main.npdm")}
    m = make_manifest(files, mod_version="1.2.3")
    assert verify(m, files).version == "1.2.3"
    assert m["title_id"] == "0100152000022000"


def test_verify_rejects_truncated_and_modified(mod_dir: Path) -> None:
    files = {n: (mod_dir / n).read_bytes() for n in ("subsdk9", "main.npdm")}
    m = make_manifest(files, mod_version="1")
    with pytest.raises(BundleError, match="truncated"):
        verify(m, {**files, "subsdk9": files["subsdk9"][:-1]})
    flipped = bytearray(files["main.npdm"])
    flipped[-1] ^= 1
    with pytest.raises(BundleError, match="SHA-256"):
        verify(m, {**files, "main.npdm": bytes(flipped)})
    with pytest.raises(BundleError, match="missing"):
        verify(m, {"subsdk9": files["subsdk9"]})


def test_source_tree_has_no_bundled_mod() -> None:
    assert not bundle_mod.has_bundled_mod()
    with pytest.raises(BundleError, match="no bundled"):
        bundle_mod.load_bundled()


# ---- deploy ----------------------------------------------------------------

@pytest.mark.parametrize("target,rel", [
    ("ryujinx", "mods/contents/0100152000022000/exefs"),
    ("sd", "atmosphere/contents/0100152000022000/exefs"),
    ("folder", "atmosphere/contents/0100152000022000/exefs"),
])
def test_deploy_layouts(tmp_path: Path, mod_dir: Path, target: str, rel: str) -> None:
    root = tmp_path / "dest"
    root.mkdir()
    b = load_from_dir(mod_dir)
    assert deploy.status(b, target, root).state == "missing"
    res = deploy.deploy(b, target, root)
    assert res.exefs == root / rel
    assert (root / rel / "subsdk9").read_bytes() == b.files["subsdk9"]
    assert {p.name for p in res.copied} == {"subsdk9", "main.npdm"}
    assert deploy.status(b, target, root).state == "current"
    again = deploy.deploy(b, target, root)
    assert not again.copied and len(again.unchanged) == 2


def test_ryujinx_portable_folder_is_followed(tmp_path: Path, mod_dir: Path) -> None:
    exe_dir = tmp_path / "ryujinx-1.3.3"
    (exe_dir / "portable").mkdir(parents=True)
    res = deploy.deploy(load_from_dir(mod_dir), "ryujinx", exe_dir)
    assert res.exefs == exe_dir / "portable" / "mods" / "contents" / "0100152000022000" / "exefs"


def test_foreign_mod_is_backed_up_and_restored(tmp_path: Path, mod_dir: Path) -> None:
    root = tmp_path / "sd"
    exefs = root / "atmosphere" / "contents" / "0100152000022000" / "exefs"
    exefs.mkdir(parents=True)
    (exefs / "subsdk9").write_bytes(b"NSO0 someone else's plugin")
    (exefs / "main.npdm").write_bytes(b"META other")
    b = load_from_dir(mod_dir)
    res = deploy.deploy(b, "sd", root)
    assert sorted(p.name for p in res.backed_up) == ["main.npdm.mk8ap-backup", "subsdk9.mk8ap-backup"]
    assert (exefs / "subsdk9").read_bytes() == b.files["subsdk9"]
    # Redeploying ours over ours must not overwrite the original backup.
    (exefs / "subsdk9").write_bytes(fake_nso(b"older build"))
    res2 = deploy.deploy(b, "sd", root)
    assert not res2.backed_up
    assert (exefs / "subsdk9.mk8ap-backup").read_bytes() == b"NSO0 someone else's plugin"
    lines = deploy.remove("sd", root)
    assert any("restored" in line for line in lines)
    assert (exefs / "subsdk9").read_bytes() == b"NSO0 someone else's plugin"
    assert (exefs / "main.npdm").read_bytes() == b"META other"


def test_remove_leaves_foreign_mod_alone(tmp_path: Path) -> None:
    root = tmp_path / "sd"
    exefs = root / "atmosphere" / "contents" / "0100152000022000" / "exefs"
    exefs.mkdir(parents=True)
    (exefs / "subsdk9").write_bytes(b"NSO0 other")
    assert "left" in deploy.remove("sd", root)[0]
    assert (exefs / "subsdk9").exists()


def test_remove_ours_cleans_up(tmp_path: Path, mod_dir: Path) -> None:
    root = tmp_path / "ryu"
    root.mkdir()
    deploy.deploy(load_from_dir(mod_dir), "ryujinx", root)
    deploy.remove("ryujinx", root)
    assert not (root / "mods" / "contents" / "0100152000022000" / "exefs").exists()


def test_deploy_errors_are_readable(tmp_path: Path, mod_dir: Path) -> None:
    b = load_from_dir(mod_dir)
    with pytest.raises(deploy.DeployError, match="Ryujinx"):
        deploy.deploy(b, "ryujinx", None)
    with pytest.raises(deploy.DeployError, match="does not exist"):
        deploy.deploy(b, "ryujinx", tmp_path / "nope")
    with pytest.raises(deploy.DeployError, match="not mounted"):
        deploy.deploy(b, "sd", tmp_path / "Z_missing")


# ---- cli + state -----------------------------------------------------------

def test_cli_deploy_status_remove(tmp_path: Path, mod_dir: Path, capsys) -> None:
    root = tmp_path / "staging"
    assert cli.main(["deploy", "folder", "--root", str(root), "--from-dir", str(mod_dir)]) == 0
    st = state.load()
    assert st["target"] == "folder" and st["deployed_version"] == "local"
    assert st["folder_root"] == str(root)
    assert not state.is_first_run()
    assert cli.main(["status"]) == 0
    out = capsys.readouterr().out
    assert "folder" in out and "bundled mod: none" in out
    assert cli.main(["remove", "folder"]) == 0  # root remembered from state
    assert state.is_first_run()


def test_cli_without_bundle_fails_cleanly(tmp_path: Path, capsys) -> None:
    assert cli.main(["deploy", "folder", "--root", str(tmp_path)]) == 1
    assert "no bundled Switch mod" in capsys.readouterr().err


# ---- packaging + audit -----------------------------------------------------

def _scripts():
    sys.path.insert(0, str(REPO / "scripts"))
    try:
        return importlib.import_module("install_apworld"), importlib.import_module("release_audit")
    finally:
        sys.path.remove(str(REPO / "scripts"))


def _versions() -> str:
    return json.loads((REPO / "apworld/mk8dx_archipelago/archipelago.json").read_text())["world_version"]


def test_bundled_zip_passes_audit_and_loads_via_zipimport(tmp_path: Path, mod_dir: Path) -> None:
    install, audit = _scripts()
    out = tmp_path / "mk8dx.apworld"
    assert install.main(["--bundle-mod", "--mod-dir", str(mod_dir), "--output", str(out)]) == 0
    a = audit.audit_zip(str(out), require_mod=True)
    assert a.errors == []
    names = zipfile.ZipFile(out).namelist()
    assert "mk8dx/_setup/mod/manifest.json" in names
    assert not any("/tests/" in n or "__pycache__" in n for n in names)
    manifest = json.loads(zipfile.ZipFile(out).read("mk8dx/_setup/mod/manifest.json"))
    assert manifest["mod_version"] == _versions()

    # Re-root mk8dx/_setup/ as its own package and import it from the zip,
    # the way Archipelago imports an .apworld (zipimport + importlib.resources).
    rerooted = tmp_path / "setup_only.zip"
    with zipfile.ZipFile(out) as src, zipfile.ZipFile(rerooted, "w") as dst:
        for n in src.namelist():
            if n.startswith("mk8dx/_setup/"):
                dst.writestr("mk8ap_zipped_setup/" + n[len("mk8dx/_setup/"):], src.read(n))
    sys.path.insert(0, str(rerooted))
    try:
        zb = importlib.import_module("mk8ap_zipped_setup.bundle")
        assert zb.has_bundled_mod()
        loaded = zb.load_bundled()
        assert loaded.version == _versions()
        assert loaded.files["subsdk9"] == (mod_dir / "subsdk9").read_bytes()
    finally:
        sys.path.remove(str(rerooted))
        for m in [m for m in sys.modules if m.startswith("mk8ap_zipped_setup")]:
            del sys.modules[m]


def test_audit_rejects_baked_ip_and_dumps(tmp_path: Path, mod_dir: Path) -> None:
    install, audit = _scripts()
    (mod_dir / "subsdk9").write_bytes(fake_nso(b"192.168.1.187\x00"))
    out = tmp_path / "dev.apworld"
    assert install.main(["--bundle-mod", "--mod-dir", str(mod_dir), "--output", str(out)]) == 0
    errs = audit.audit_zip(str(out), require_mod=True).errors
    assert any("192.168.1.187" in e for e in errs)

    with zipfile.ZipFile(out, "a") as zf:
        zf.writestr("mk8dx/data/main.nso", b"x")
        zf.writestr("mk8dx/romfs/Course.szs", b"x")
        zf.writestr("mk8dx/notes.txt", b"x")
    errs = audit.audit_zip(str(out), require_mod=True).errors
    assert any("main.nso" in e and "forbidden" in e for e in errs)
    assert any("Course.szs" in e for e in errs)
    assert any("notes.txt" in e and "allowlist" in e for e in errs)


def test_audit_requires_mod_unless_told(tmp_path: Path) -> None:
    install, audit = _scripts()
    out = tmp_path / "plain.apworld"
    assert install.main(["--output", str(out)]) == 0
    assert any("no bundled mod" in e for e in audit.audit_zip(str(out), require_mod=True).errors)
    assert audit.audit_zip(str(out), require_mod=False).errors == []


def test_audit_flags_version_drift(tmp_path: Path, mod_dir: Path) -> None:
    install, audit = _scripts()
    out = tmp_path / "drift.apworld"
    assert install.main(["--bundle-mod", "--mod-dir", str(mod_dir), "--mod-version", "9.9.9",
                         "--output", str(out)]) == 0
    assert any("version drift" in e for e in audit.audit_zip(str(out), require_mod=True).errors)


def test_lz4_decoder_matches_reference() -> None:
    lz4_block = pytest.importorskip("lz4.block")
    _, audit = _scripts()
    payload = (b"mk8dx_archipelago " * 50) + bytes(range(256)) + b"\x00" * 3000
    packed = lz4_block.compress(payload, store_size=False)
    assert audit.lz4_block_decompress(packed, len(payload)) == payload
