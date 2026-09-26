# 请抬头享受阳光｜日子很好 我很我---------致咩子
from __future__ import annotations

import base64
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import (
        Ed25519PrivateKey,
        Ed25519PublicKey,
    )
    from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
except ModuleNotFoundError:
    Ed25519PrivateKey = None
    Ed25519PublicKey = None
    Encoding = None
    PublicFormat = None
    CRYPTOGRAPHY_AVAILABLE = False
else:
    CRYPTOGRAPHY_AVAILABLE = True


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "qmclient_scripts/sign_update_release.py"
SPEC = importlib.util.spec_from_file_location("sign_update_release", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
if CRYPTOGRAPHY_AVAILABLE:
    SIGN_UPDATE_RELEASE = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(SIGN_UPDATE_RELEASE)


@unittest.skipUnless(CRYPTOGRAPHY_AVAILABLE, "cryptography is required for release signing tests")
class SignUpdateReleaseTest(unittest.TestCase):
    PRIVATE_SEED = bytes(range(32))
    PUBLIC_KEY = (
        Ed25519PrivateKey.from_private_bytes(PRIVATE_SEED)
        .public_key()
        .public_bytes(Encoding.Raw, PublicFormat.Raw)
        if CRYPTOGRAPHY_AVAILABLE
        else b""
    )

    def _write_package(self, path: Path, *, include_server: bool = True) -> None:
        files = {
            "DDNet.exe": b"client",
            "QmClient-Updater.exe": b"updater",
            "data/languages/simplified_chinese.txt": b"language",
        }
        if include_server:
            files["DDNet-Server.exe"] = b"server"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, content in files.items():
                archive.writestr(name, content)

    def test_builds_deterministic_manifest_and_two_valid_signatures(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-update-sign-") as temp_dir:
            root = Path(temp_dir)
            package = root / "QmClient-windows.zip"
            self._write_package(package)

            outputs = SIGN_UPDATE_RELEASE.sign_release(
                package=package,
                version="v2.80.0",
                private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(),
                output_dir=root,
                expected_public_key=self.PUBLIC_KEY,
            )

            manifest_bytes = outputs.manifest.read_bytes()
            manifest = json.loads(manifest_bytes)
            self.assertEqual(manifest["schema"], 1)
            self.assertEqual(manifest["version"], "2.80.0")
            self.assertEqual(manifest["package"]["name"], "QmClient-windows.zip")
            self.assertEqual(
                [entry["path"] for entry in manifest["files"]],
                sorted(entry["path"] for entry in manifest["files"]),
            )
            self.assertEqual(
                {entry["path"] for entry in manifest["files"]},
                {
                    "DDNet-Server.exe",
                    "DDNet.exe",
                    "QmClient-Updater.exe",
                    "data/languages/simplified_chinese.txt",
                },
            )

            public_key = Ed25519PrivateKey.from_private_bytes(
                self.PRIVATE_SEED
            ).public_key()
            self.assertIsInstance(public_key, Ed25519PublicKey)
            public_key.verify(outputs.manifest_signature.read_bytes(), manifest_bytes)
            public_key.verify(
                outputs.package_signature.read_bytes(),
                SIGN_UPDATE_RELEASE.PACKAGE_SIGNATURE_CONTEXT
                + bytes.fromhex(manifest["package"]["sha256"]),
            )
            self.assertEqual(outputs.manifest_signature.stat().st_size, 64)
            self.assertEqual(outputs.package_signature.stat().st_size, 64)

    def test_rejects_package_without_server(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-update-sign-") as temp_dir:
            root = Path(temp_dir)
            package = root / "QmClient-windows.zip"
            self._write_package(package, include_server=False)

            with self.assertRaisesRegex(ValueError, "DDNet-Server.exe"):
                SIGN_UPDATE_RELEASE.sign_release(
                    package=package,
                    version="2.80.0",
                    private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(),
                    output_dir=root,
                    expected_public_key=self.PUBLIC_KEY,
                )

    def test_rejects_unstable_or_oversized_version(self) -> None:
        for version in (
            "2.80.0-rc1",
            "2.2147483648",
            "2." + "1" * 32,
        ):
            with self.subTest(version=version):
                with self.assertRaises(ValueError):
                    SIGN_UPDATE_RELEASE._normalize_version(version)

    def test_accepts_major_only_stable_version(self) -> None:
        self.assertEqual(SIGN_UPDATE_RELEASE._normalize_version("v3"), "3")

    def test_rejects_unsafe_or_case_insensitive_duplicate_paths(self) -> None:
        for entries in (
            [("../DDNet.exe", b"bad")],
            [("DDNet.exe", b"one"), ("ddnet.exe", b"two")],
            [("data//file.txt", b"bad")],
            [("data/NUL.txt", b"bad")],
            [("data/file. ", b"bad")],
            [("data/" + "界" * 400 + ".txt", b"bad")],
        ):
            with self.subTest(entries=[name for name, _ in entries]):
                with tempfile.TemporaryDirectory(prefix="qm-update-sign-") as temp_dir:
                    package = Path(temp_dir) / "QmClient-windows.zip"
                    with zipfile.ZipFile(package, "w") as archive:
                        for name, content in entries:
                            archive.writestr(name, content)
                    with self.assertRaises(ValueError):
                        SIGN_UPDATE_RELEASE.build_manifest(package, "2.80.0")

    def test_rejects_manifest_larger_than_client_limit(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-update-sign-") as temp_dir:
            root = Path(temp_dir)
            package = root / "QmClient-windows.zip"
            self._write_package(package)
            with (
                mock.patch.object(SIGN_UPDATE_RELEASE, "MAX_MANIFEST_SIZE", 1),
                self.assertRaisesRegex(ValueError, "manifest exceeds"),
            ):
                SIGN_UPDATE_RELEASE.sign_release(
                    package=package,
                    version="2.80.0",
                    private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(),
                    output_dir=root,
                    expected_public_key=self.PUBLIC_KEY,
                )

    def test_rejects_private_key_that_does_not_match_embedded_public_key(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-update-key-") as temp_dir:
            root = Path(temp_dir)
            package = root / "QmClient-windows.zip"
            self._write_package(package)
            with self.assertRaisesRegex(ValueError, "does not match"):
                SIGN_UPDATE_RELEASE.sign_release(
                    package=package,
                    version="2.80.0",
                    private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(),
                    output_dir=root,
                )

    def test_normalizes_single_cpack_top_level_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-update-normalize-") as temp_dir:
            package = Path(temp_dir) / "QmClient-windows.zip"
            with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("QmClient-2.80.0-win64/DDNet.exe", b"client")
                archive.writestr("QmClient-2.80.0-win64/DDNet-Server.exe", b"server")
                archive.writestr(
                    "QmClient-2.80.0-win64/QmClient-Updater.exe", b"updater"
                )
                archive.writestr("QmClient-2.80.0-win64/data/file.txt", b"data")

            SIGN_UPDATE_RELEASE.normalize_windows_package(package)

            with zipfile.ZipFile(package) as archive:
                self.assertEqual(
                    set(archive.namelist()),
                    {
                        "DDNet.exe",
                        "DDNet-Server.exe",
                        "QmClient-Updater.exe",
                        "data/file.txt",
                    },
                )
            manifest = SIGN_UPDATE_RELEASE.build_manifest(package, "2.80.0")
            self.assertEqual(len(manifest["files"]), 4)

    def test_release_workflow_signs_and_uploads_all_update_assets(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text(
            encoding="utf-8"
        )
        prepare_step = workflow.index("Prepare Windows update signing environment")
        signing_step = workflow.index("Sign Windows automatic update assets")
        release_step = workflow.index("Create release and upload desktop assets")
        self.assertLess(prepare_step, signing_step)
        self.assertLess(signing_step, release_step)
        self.assertNotIn(
            "secrets.QM_UPDATE_ED25519_PRIVATE_KEY",
            workflow[prepare_step:signing_step],
        )
        self.assertIn(
            "secrets.QM_UPDATE_ED25519_PRIVATE_KEY",
            workflow[signing_step:release_step],
        )
        self.assertIn("--normalize-package", workflow)
        self.assertIn("Verify Windows update package contents", workflow)
        self.assertIn("prerelease: false", workflow[release_step:])
        for asset in (
            "QmClient-windows.zip",
            "QmClient-windows.zip.sig",
            "QmClient-windows-update.json",
            "QmClient-windows-update.json.sig",
        ):
            self.assertIn(f"release-assets/{asset}", workflow[signing_step:])

    def test_windows_updater_links_only_the_dedicated_update_library(self) -> None:
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        updater_start = cmake.index("add_executable(qm-client-updater")
        updater_end = cmake.index("list(APPEND TARGETS_OWN qm-client-updater)")
        updater_block = cmake[updater_start:updater_end]
        self.assertIn("rust_qm_update", updater_block)
        self.assertNotIn("rust_engine_shared", updater_block)
        self.assertIn('rust_target STREQUAL "qm_update"', cmake)
        self.assertIn("CMAKE_STATIC_LIBRARY_PREFIX}qm_update", cmake)
        update_library = (REPO_ROOT / "src/qm-update/lib.rs").read_text(
            encoding="utf-8"
        )
        self.assertIn('extern "C" fn qm_update_apply', update_library)


if __name__ == "__main__":
    unittest.main()
