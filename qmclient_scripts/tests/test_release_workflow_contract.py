from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ReleaseWorkflowContractTest(unittest.TestCase):
    def test_tag_release_uses_a_production_android_key(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")

        self.assertIn("QM_ANDROID_KEYSTORE_BASE64", workflow)
        self.assertIn("Required Android release secret", workflow)
        self.assertIn('ANDROID_PACKAGE_NAME: com.qmclient.client', workflow)

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
        workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("Windows package has lossy file names", workflow)
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
