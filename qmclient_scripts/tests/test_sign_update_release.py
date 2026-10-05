# 请抬头享受阳光｜日子很好 我很我---------致咩子
from __future__ import annotations

import base64
import importlib.util
import hashlib
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

try:
	from cryptography.exceptions import InvalidSignature
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
	PUBLIC_KEY = Ed25519PrivateKey.from_private_bytes(PRIVATE_SEED).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw) if CRYPTOGRAPHY_AVAILABLE else b""

	def test_setup_signature_and_manifest_match_executable(self) -> None:
		with tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp") as directory:
			root = Path(directory)
			setup = root / "QmClient-Setup.exe"
			setup.write_bytes(b"MZ-test-setup")
			SIGN_UPDATE_RELEASE.sign_setup_release(setup=setup, version="v3.4", private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(), output_dir=root, expected_public_key=self.PUBLIC_KEY)
			content = (root / "QmClient-windows-setup-update.json").read_bytes()
			manifest = json.loads(content)
			digest = hashlib.sha256(setup.read_bytes()).digest()
			self.assertEqual(manifest["version"], "3.4")
			self.assertEqual(manifest["package"], {"name": setup.name, "size": setup.stat().st_size, "sha256": digest.hex()})
			self.assertEqual(manifest["files"], [])
			public = Ed25519PublicKey.from_public_bytes(self.PUBLIC_KEY)
			public.verify((root / "QmClient-windows-setup-update.json.sig").read_bytes(), content)
			public.verify((root / "QmClient-Setup.exe.sig").read_bytes(), SIGN_UPDATE_RELEASE.PACKAGE_SIGNATURE_CONTEXT + digest)
			with self.assertRaises(InvalidSignature):
				public.verify((root / "QmClient-Setup.exe.sig").read_bytes(), SIGN_UPDATE_RELEASE.PACKAGE_SIGNATURE_CONTEXT + hashlib.sha256(b"changed").digest())

	def test_release_rejects_embedded_portable_user_profile(self) -> None:
		with tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp") as directory:
			package = Path(directory) / "QmClient-windows-portable.zip"
			self._write_package(package)
			with zipfile.ZipFile(package, "a") as archive:
				archive.writestr("PROFILE/qmclient/settings.cfg", b"user data")
			with self.assertRaisesRegex(ValueError, "must not contain user profile"):
				SIGN_UPDATE_RELEASE.build_manifest(package, "v3.4")

	def test_portable_signatures_use_separate_asset_names(self) -> None:
		with tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp") as directory:
			root = Path(directory)
			package = root / "QmClient-windows-portable.zip"
			self._write_package(package)
			outputs = SIGN_UPDATE_RELEASE.sign_release(package=package, version="v3.4", private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(), output_dir=root, expected_public_key=self.PUBLIC_KEY)
			self.assertEqual(outputs.manifest.name, "QmClient-windows-portable-update.json")
			self.assertEqual(outputs.package_signature.name, "QmClient-windows-portable.zip.sig")
			content = outputs.manifest.read_bytes()
			self.assertEqual(json.loads(content)["package"]["name"], package.name)
			public = Ed25519PublicKey.from_public_bytes(self.PUBLIC_KEY)
			public.verify(outputs.manifest_signature.read_bytes(), content)
			public.verify(outputs.package_signature.read_bytes(), SIGN_UPDATE_RELEASE.PACKAGE_SIGNATURE_CONTEXT + hashlib.sha256(package.read_bytes()).digest())
			self.assertFalse((root / "QmClient-windows-update.json").exists())

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
				version="v3.3",
				private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(),
				output_dir=root,
				expected_public_key=self.PUBLIC_KEY,
			)

			manifest_bytes = outputs.manifest.read_bytes()
			manifest = json.loads(manifest_bytes)
			self.assertEqual(manifest["schema"], 1)
			self.assertEqual(manifest["version"], "3.3")
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

			public_key = Ed25519PrivateKey.from_private_bytes(self.PRIVATE_SEED).public_key()
			self.assertIsInstance(public_key, Ed25519PublicKey)
			public_key.verify(outputs.manifest_signature.read_bytes(), manifest_bytes)
			public_key.verify(
				outputs.package_signature.read_bytes(),
				SIGN_UPDATE_RELEASE.PACKAGE_SIGNATURE_CONTEXT + bytes.fromhex(manifest["package"]["sha256"]),
			)
			self.assertEqual(outputs.manifest_signature.stat().st_size, 64)
			self.assertEqual(outputs.package_signature.stat().st_size, 64)

	def test_preview_version_is_authenticated_by_manifest_signature(self) -> None:
		with tempfile.TemporaryDirectory(prefix="qm-update-preview-") as temp_dir:
			root = Path(temp_dir)
			package = root / "QmClient-windows.zip"
			self._write_package(package)
			outputs = SIGN_UPDATE_RELEASE.sign_release(
				package=package,
				version="v3.4-preview.1",
				private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(),
				output_dir=root,
				expected_public_key=self.PUBLIC_KEY,
			)
			manifest = outputs.manifest.read_bytes()
			Ed25519PublicKey.from_public_bytes(self.PUBLIC_KEY).verify(outputs.manifest_signature.read_bytes(), manifest)
			self.assertEqual(json.loads(manifest)["version"], "3.4-preview.1")

	def test_rejects_package_without_server(self) -> None:
		with tempfile.TemporaryDirectory(prefix="qm-update-sign-") as temp_dir:
			root = Path(temp_dir)
			package = root / "QmClient-windows.zip"
			self._write_package(package, include_server=False)

			with self.assertRaisesRegex(ValueError, "DDNet-Server.exe"):
				SIGN_UPDATE_RELEASE.sign_release(
					package=package,
					version="3.3",
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

	def test_accepts_two_part_stable_and_preview_versions(self) -> None:
		self.assertEqual(SIGN_UPDATE_RELEASE._normalize_version("v3.3"), "3.3")
		self.assertEqual(SIGN_UPDATE_RELEASE._normalize_version("v3.4-preview.2"), "3.4-preview.2")
		for version in ("v3", "3.3.1"):
			with self.subTest(version=version), self.assertRaises(ValueError):
				SIGN_UPDATE_RELEASE._normalize_version(version)

	def test_rejects_unsafe_or_case_insensitive_duplicate_paths(self) -> None:
		for entries in (
			[("../DDNet.exe", b"bad")],
			[("profile/qmclient/settings.cfg", b"user data")],
			[("PROFILE/settings.cfg", b"user data")],
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
					version="3.3",
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
					version="3.3",
					private_key_base64=base64.b64encode(self.PRIVATE_SEED).decode(),
					output_dir=root,
				)

	def test_normalizes_single_cpack_top_level_directory(self) -> None:
		with tempfile.TemporaryDirectory(prefix="qm-update-normalize-") as temp_dir:
			package = Path(temp_dir) / "QmClient-windows.zip"
			with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
				archive.writestr("QmClient-2.80.0-win64/DDNet.exe", b"client")
				archive.writestr("QmClient-2.80.0-win64/DDNet-Server.exe", b"server")
				archive.writestr("QmClient-2.80.0-win64/QmClient-Updater.exe", b"updater")
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
			manifest = SIGN_UPDATE_RELEASE.build_manifest(package, "3.3")
			self.assertEqual(len(manifest["files"]), 4)

	def test_normalizes_package_with_non_ascii_file_names(self) -> None:
		# V3 发布阻断回归：CPack 顶层目录里带有中文名字体时，规范化必须
		# 保持名字逐字节不变，不能出现 `?` 替换或重复条目。
		with tempfile.TemporaryDirectory(prefix="qm-update-utf8-") as temp_dir:
			root = Path(temp_dir)
			package = root / "QmClient-windows.zip"
			with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
				archive.writestr("QmClient-3.0-win64/DDNet.exe", b"client")
				archive.writestr("QmClient-3.0-win64/DDNet-Server.exe", b"server")
				archive.writestr("QmClient-3.0-win64/QmClient-Updater.exe", b"updater")
				archive.writestr(
					"QmClient-3.0-win64/data/fonts/霞鹜新致宋.ttf",
					b"zhi-song",
				)
				archive.writestr(
					"QmClient-3.0-win64/data/fonts/霞鹜新晰黑.ttf",
					b"xi-hei",
				)

			SIGN_UPDATE_RELEASE.normalize_windows_package(package)

			with zipfile.ZipFile(package) as archive:
				self.assertEqual(
					set(archive.namelist()),
					{
						"DDNet.exe",
						"DDNet-Server.exe",
						"QmClient-Updater.exe",
						"data/fonts/霞鹜新致宋.ttf",
						"data/fonts/霞鹜新晰黑.ttf",
					},
				)
				self.assertEqual(archive.read("data/fonts/霞鹜新晰黑.ttf"), b"xi-hei")
			manifest = SIGN_UPDATE_RELEASE.build_manifest(package, "v3.3")
			self.assertEqual(manifest["version"], "3.3")
			self.assertIn(
				"data/fonts/霞鹜新致宋.ttf",
				{entry["path"] for entry in manifest["files"]},
			)

	def test_rejects_names_collapsed_by_lossy_codepage_conversion(self) -> None:
		# 有损代码页转换会让两个不同字体都变成 `?????.ttf`，规范化必须拒绝。
		with tempfile.TemporaryDirectory(prefix="qm-update-lossy-") as temp_dir:
			package = Path(temp_dir) / "QmClient-windows.zip"
			with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
				archive.writestr("QmClient-3.0-win64/DDNet.exe", b"client")
				archive.writestr("QmClient-3.0-win64/DDNet-Server.exe", b"server")
				archive.writestr("QmClient-3.0-win64/QmClient-Updater.exe", b"updater")
				archive.writestr("QmClient-3.0-win64/data/fonts/?????.ttf", b"one")
				archive.writestr("QmClient-3.0-win64/data/fonts/?????.ttf", b"two")

			with self.assertRaisesRegex(ValueError, "duplicate archive path"):
				SIGN_UPDATE_RELEASE.normalize_windows_package(package)

	def test_windows_updater_links_only_the_dedicated_update_library(self) -> None:
		cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
		updater_start = cmake.index("add_executable(qm-client-updater")
		updater_end = cmake.index("list(APPEND TARGETS_OWN qm-client-updater)")
		updater_block = cmake[updater_start:updater_end]
		self.assertIn("rust_qm_update", updater_block)
		self.assertNotIn("rust_engine_shared", updater_block)
		self.assertIn('rust_target STREQUAL "qm_update"', cmake)
		self.assertIn("CMAKE_STATIC_LIBRARY_PREFIX}qm_update", cmake)
		update_library = (REPO_ROOT / "src/qm/update/lib.rs").read_text(encoding="utf-8")
		self.assertIn('extern "C" fn qm_update_apply', update_library)


if __name__ == "__main__":
	unittest.main()
