from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from qmclient_scripts import bump_version
from qmclient_scripts.versioning import next_preview_tag, parse_version, version_from_header


class BumpVersionTest(unittest.TestCase):
	def test_stable_and_preview_metadata_share_the_base_version(self) -> None:
		stable = parse_version("v3.3")
		preview = parse_version("v3.3-preview.2")
		self.assertEqual(stable.base, preview.base)
		self.assertEqual(stable.display, "3.3")
		self.assertEqual(preview.display, "3.3 Preview 2")
		self.assertEqual(preview.tag, "v3.3-preview.2")
		self.assertGreater(stable.sort_key, preview.sort_key)
		self.assertGreater(parse_version("3.10").sort_key, parse_version("3.9").sort_key)

	def test_rejects_old_version_shapes_and_invalid_preview_numbers(self) -> None:
		for version in ("3", "3.3.1", "3.3.0.1", "3.3-rc1", "nightly", "03.3", "3.03", "3.3-preview.0", "3.3-preview.01", "3.3-preview.-1", "3.3-preview.1junk"):
			with self.subTest(version=version), self.assertRaises(ValueError):
				parse_version(version)

	def test_android_version_order_preserves_preview_promotion_and_legacy_upgrade(self) -> None:
		versions = ("3.3-preview.1", "3.3-preview.2", "3.3", "3.4-preview.1", "3.4")
		codes = [parse_version(version).android_version_code for version in versions]
		self.assertEqual(codes, sorted(set(codes)))
		self.assertGreater(codes[0], 3_013_001)

	def test_android_version_bounds_prevent_collisions_and_overflow(self) -> None:
		for version in ("3.10000", "3.3-preview.9999", "21.0"):
			with self.subTest(version=version), self.assertRaises(ValueError):
				_ = parse_version(version).android_version_code

	def test_updates_legacy_header_and_preserves_encoding_and_line_endings(self) -> None:
		source = """\ufeff#ifndef GAME_VERSION_H
#define GAME_VERSION_H
// QmClient
#define QMCLIENT_STABLE_VERSION "3.2"
#define QMCLIENT_DEV_VERSION "3.13.1"
#if defined(QMCLIENT_STABLE_BUILD)
#define QMCLIENT_VERSION QMCLIENT_STABLE_VERSION
#else
#define QMCLIENT_VERSION QMCLIENT_DEV_VERSION
#endif
#define CLIENT_NAME "QmClient"
#define CLIENT_RELEASE_VERSION "V" QMCLIENT_VERSION

#endif
"""
		for newline in ("\n", "\r\n"):
			with self.subTest(newline=newline), tempfile.TemporaryDirectory() as directory:
				path = Path(directory) / "version.h"
				path.write_bytes(source.replace("\n", newline).encode("utf-8"))
				with mock.patch.object(bump_version, "VERSION_H_PATH", path):
					bump_version.update_version_h("3.3-preview.2")
					first = path.read_bytes()
					self.assertEqual(version_from_header(first.decode("utf-8")), parse_version("3.3-preview.2"))
					bump_version.update_version_h("3.3")
				result = path.read_bytes()
				self.assertTrue(result.startswith(b"\xef\xbb\xbf"))
				self.assertEqual(result.count(b"\n"), result.count(newline.encode()))
				self.assertEqual(version_from_header(result.decode("utf-8")), parse_version("3.3"))
				self.assertNotIn(b"QMCLIENT_DEV_VERSION", result)
				self.assertEqual(result.count(b"#define CLIENT_NAME"), 1)

	def test_dry_run_does_not_modify_header(self) -> None:
		with mock.patch.object(sys, "argv", ["bump_version.py", "--tag", "v3.3", "--dry-run"]), mock.patch.object(bump_version, "update_version_h") as update, contextlib.redirect_stdout(io.StringIO()):
			self.assertEqual(bump_version.main(), 0)
			update.assert_not_called()

	def test_tag_requires_canonical_lowercase_prefix(self) -> None:
		for tag in ("3.3", "V3.3"):
			with self.subTest(tag=tag), self.assertRaises(ValueError):
				bump_version.normalize_version(None, tag)

	def test_preview_retries_reuse_the_same_tag_and_new_commits_increment(self) -> None:
		tags = [("v3.4-preview.1", "first"), ("v3.4-preview.2", "second"), ("nightly", "third")]
		self.assertEqual(next_preview_tag(parse_version("3.3"), "second", tags), "v3.4-preview.2")
		self.assertEqual(next_preview_tag(parse_version("3.3"), "third", tags), "v3.4-preview.3")
		self.assertEqual(next_preview_tag(parse_version("3.4"), "fourth", tags), "v3.5-preview.1")
		self.assertEqual(next_preview_tag(parse_version("3.4-preview.2"), "third", tags), "v3.4-preview.3")


if __name__ == "__main__":
	unittest.main()
