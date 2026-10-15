from __future__ import annotations

import unittest
from pathlib import Path

from qmclient_scripts import check_ci_deps


REPO_ROOT = Path(__file__).resolve().parents[2]

# 测试全部走内存输入（check_sources），不创建临时目录；
# 仓库现状由 test_current_repository_passes_the_guard 直接覆盖真实文件。
ACTION_TEMPLATE = """name: 'Install Linux build dependencies'
inputs:
  extra:
    required: false
    default: ''
runs:
  using: 'composite'
  steps:
    - shell: bash
      run: |
        sudo apt-get update -y
        sudo apt-get install -y \\
          cmake ninja-build pkg-config \\
          libcurl4-openssl-dev \\
          ${{ inputs.extra }}
    - name: Verify system curl dev probe
      shell: bash
      run: |
        cat > /tmp/qm_curl_probe.c <<'EOF'
        #include <curl/curl.h>
        int main(void)
        {
        	CURLSH *share = curl_share_init();
        	if(!share)
        		return 1;
        	curl_share_cleanup(share);
        	return 0;
        }
        EOF
        cc /tmp/qm_curl_probe.c -lcurl -o /tmp/qm_curl_probe
        /tmp/qm_curl_probe
"""

BUILD_EXTRA = "libdbus-1-dev libogg-dev libopus-dev libopusfile-dev libwavpack-dev"


def workflow_with_action(extra: str = "", cmake_line: str = "") -> str:
	lines = [
		"jobs:",
		"  build:",
		"    runs-on: ubuntu-24.04",
		"    steps:",
	]
	if cmake_line:
		lines.append(f"      - name: Configure\n        run: {cmake_line}")
	lines.append("      - name: Prepare Linux")
	lines.append("        uses: ./.github/actions/linux-deps")
	if extra:
		lines.append("        with:")
		lines.append(f"          extra: {extra}")
	return "\n".join(lines) + "\n"


def sources_with(action_text: str = ACTION_TEMPLATE, **workflows: str) -> dict[str, str]:
	sources = {check_ci_deps.ACTION_RELATIVE: action_text}
	sources.update(workflows)
	return sources


class CheckCiDepsTest(unittest.TestCase):
	def test_current_repository_passes_the_guard(self) -> None:
		self.assertEqual(check_ci_deps.run_checks(REPO_ROOT), [])

	def test_standalone_apt_dev_package_outside_registry_fails(self) -> None:
		workflow = workflow_with_action(extra=BUILD_EXTRA) + ("  lint:\n    steps:\n      - name: Install libfoo\n        run: sudo apt-get install libfoo-dev -y\n")
		failures = check_ci_deps.check_sources(sources_with(**{"build.yml": workflow}))
		self.assertEqual(len(failures), 1)
		self.assertIn("libfoo-dev", failures[0])

	def test_install_extraction_ignores_following_commands(self) -> None:
		body = "        run: |\n          sudo apt-get update\n          sudo apt-get install -y p7zip-full\n          python3 -m venv .update-signing-venv\n          .update-signing-venv/bin/python -m pip install cryptography==45.0.6\n"
		self.assertEqual(check_ci_deps.extract_install_packages(body), {"p7zip-full"})

	def test_unregistered_extra_package_fails(self) -> None:
		workflow = workflow_with_action(extra=BUILD_EXTRA + " libsecret-1-dev")
		failures = check_ci_deps.check_sources(sources_with(**{"build.yml": workflow}))
		self.assertTrue(any("libsecret-1-dev" in failure and "EXPECTED_EXTRA" in failure for failure in failures))

	def test_missing_registered_extra_package_fails(self) -> None:
		workflow = workflow_with_action(extra="libmariadb-dev libwebsockets-dev")
		failures = check_ci_deps.check_sources(sources_with(**{"clang-tidy.yml": workflow}))
		self.assertEqual(len(failures), 1)
		self.assertIn("libminiupnpc-dev", failures[0])

	def test_allowed_standalone_tool_package_passes(self) -> None:
		workflow = workflow_with_action(extra=BUILD_EXTRA) + ("  release:\n    steps:\n      - name: Prepare signing\n        run: sudo apt-get update && sudo apt-get install -y p7zip-full\n")
		failures = check_ci_deps.check_sources(sources_with(**{"build.yml": workflow}))
		self.assertEqual(failures, [])

	def test_optional_feature_without_matching_extra_package_fails(self) -> None:
		# CMake 配置启用 UPNP 时 extra 必须包含 libminiupnpc-dev；这里省掉它暴露缺口。
		workflow = workflow_with_action(
			extra="libmariadb-dev libwebsockets-dev",
			cmake_line="cmake -DUPNP=ON ..",
		)
		failures = check_ci_deps.check_sources(sources_with(**{"codeql-analysis.yml": workflow}))
		self.assertEqual(len(failures), 1)
		self.assertIn("UPNP", failures[0])
		self.assertIn("libminiupnpc-dev", failures[0])

	def test_action_without_curl_probe_fails(self) -> None:
		start = ACTION_TEMPLATE.index("    - name: Verify system curl dev probe")
		stripped = ACTION_TEMPLATE[:start]
		workflow = workflow_with_action(extra=BUILD_EXTRA)
		failures = check_ci_deps.check_sources(sources_with(action_text=stripped, **{"build.yml": workflow}))
		self.assertEqual(len(failures), 1)
		self.assertIn("curl_share_init", failures[0])

	def test_action_missing_libcurl_dev_fails(self) -> None:
		stripped = ACTION_TEMPLATE.replace("          libcurl4-openssl-dev \\\n", "")
		workflow = workflow_with_action(extra=BUILD_EXTRA)
		failures = check_ci_deps.check_sources(sources_with(action_text=stripped, **{"build.yml": workflow}))
		self.assertEqual(len(failures), 1)
		self.assertIn("libcurl4-openssl-dev", failures[0])

	def test_missing_action_reports_single_failure(self) -> None:
		failures = check_ci_deps.check_sources({"build.yml": workflow_with_action(extra=BUILD_EXTRA)})
		self.assertEqual(len(failures), 1)
		self.assertIn("action.yml", failures[0])


if __name__ == "__main__":
	unittest.main()
