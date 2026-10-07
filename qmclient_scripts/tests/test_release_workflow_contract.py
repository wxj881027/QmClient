from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ReleaseWorkflowContractTest(unittest.TestCase):
	def test_release_stages_signed_assets_before_publishing_the_selected_channel(self) -> None:
		# GitHub 的上传顺序与发布标志属于 CI 接线合同，本地签名单测无法观察。
		workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
		prepare_step = workflow.index("Prepare Windows update signing environment")
		signing_step = workflow.index("Sign Windows automatic update assets")
		release_step = workflow.index("Stage all platform assets in a draft release")
		publish_step = workflow.index("Verify every uploaded asset and publish")
		self.assertLess(prepare_step, signing_step)
		self.assertLess(signing_step, release_step)
		self.assertLess(release_step, publish_step)
		self.assertNotIn(
			"secrets.QM_UPDATE_ED25519_PRIVATE_KEY",
			workflow[prepare_step:signing_step],
		)
		self.assertIn(
			"secrets.QM_UPDATE_ED25519_PRIVATE_KEY",
			workflow[signing_step:release_step],
		)
		self.assertIn("--normalize-package", workflow[signing_step:release_step])
		draft_step = workflow[release_step:publish_step]
		self.assertIn("draft: true", draft_step)
		self.assertIn("prerelease: ${{ contains(env.RELEASE_TAG, '-preview.') }}", draft_step)
		self.assertIn("make_latest: false", draft_step)
		for asset in (
			"QmClient-windows.zip",
			"QmClient-windows.zip.sig",
			"QmClient-windows-update.json",
			"QmClient-windows-update.json.sig",
			"QmClient-windows.7z",
			"QmClient-windows.7z.sig",
			"QmClient-windows-7z-update.json",
			"QmClient-windows-7z-update.json.sig",
			"QmClient-windows-portable.zip",
			"QmClient-windows-portable.zip.sig",
			"QmClient-windows-portable-update.json",
			"QmClient-windows-portable-update.json.sig",
			"QmClient-windows-portable.7z",
			"QmClient-windows-portable.7z.sig",
			"QmClient-windows-portable-7z-update.json",
			"QmClient-windows-portable-7z-update.json.sig",
			"QmClient-Setup.exe",
			"QmClient-Setup.exe.sig",
			"QmClient-windows-setup-update.json",
			"QmClient-windows-setup-update.json.sig",
			"QmClient-ubuntu.tar.xz",
			"QmClient-macOS.dmg",
			"QmClient-android.apk",
			"QmClient-android.aab",
		):
			self.assertIn(f"release-assets/{asset}", draft_step)
		self.assertIn("--draft=false --prerelease --latest=false", workflow[publish_step:])
		self.assertIn("--draft=false --prerelease=false --latest", workflow[publish_step:])

	def test_tag_release_uses_a_production_android_key(self) -> None:
		workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")

		self.assertIn("QM_ANDROID_KEYSTORE_BASE64", workflow)
		self.assertIn("Required Android release secret", workflow)
		self.assertIn("ANDROID_PACKAGE_NAME: com.qmclient.client", workflow)

	def test_tag_release_verifies_the_macos_dmg(self) -> None:
		workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")

		self.assertIn("  pull_request:", workflow)
		self.assertIn("Verify macOS DMG contents", workflow)
		self.assertIn("qmclient_scripts/verify_macos_dmg.sh", workflow)

	def test_android_versions_are_derived_from_qmclient_version(self) -> None:
		script = (REPO_ROOT / "scripts/android/cmake_android.sh").read_text(encoding="utf-8")

		self.assertIn("QMCLIENT_VERSION=", script)
		self.assertIn("QMCLIENT_VERSION_CODE", script)
		self.assertNotIn("DDNET_VERSION_NUMBER", script)
		self.assertNotIn("GAME_RELEASE_VERSION_INTERNAL", script)

	def test_portable_zip_is_written_with_utf8_names(self) -> None:
		# `cmake -E tar --format=zip` 按宿主代码页写归档名，Windows 上会把
		# `霞鹜新致宋.ttf` 写成 `?????.ttf`；便携包必须改用 UTF-8 写入器。
		cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

		self.assertIn("qmclient_scripts/zip_pack.py", cmake)
		self.assertNotIn("tar c ../${CPACK_PACKAGE_FILE_NAME}.${ext} --format=zip", cmake)

	def test_windows_package_verification_rejects_lossy_names(self) -> None:
		workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")

		self.assertIn("qmclient_scripts/verify_windows_package.py", workflow)
		self.assertIn("--tools-manifest build/qmclient-archive-tools.txt", workflow)
		# 该步骤会打印非 ASCII 文件名，而 Windows 运行器的 stdout 是 cp1252：
		# 没有显式 UTF-8 输出时会以 UnicodeEncodeError 让构建失败。
		step = workflow.index("Verify Windows update package contents")
		self.assertIn("PYTHONIOENCODING: utf-8", workflow[step : step + 400])

	def test_legacy_delta_updater_requires_explicit_deployment_paths(self) -> None:
		script = (REPO_ROOT / "qmclient_scripts/update.zsh").read_text(encoding="utf-8")

		self.assertIn("QM_UPDATE_SCRIPTS_DIR", script)
		self.assertIn("QM_UPDATE_OUTPUT_DIR", script)
		self.assertIn("wxj881027/QmClient", script)
		self.assertNotIn("sjrc6/TaterClient-ddnet", script)


if __name__ == "__main__":
	unittest.main()
