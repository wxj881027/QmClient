#!/usr/bin/env python3
# 请抬头享受阳光｜日子很好 我很我---------致咩子
from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
import os
import shutil
import stat
import zipfile
from pathlib import Path, PurePosixPath
from typing import NamedTuple

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat


MAX_PACKAGE_SIZE = 5 * 1024 * 1024 * 1024
MAX_MANIFEST_SIZE = 32 * 1024 * 1024
MAX_FILE_SIZE = 2 * 1024 * 1024 * 1024
MAX_TOTAL_SIZE = 10 * 1024 * 1024 * 1024
MAX_FILE_COUNT = 100_000
REQUIRED_FILES = {"DDNet.exe", "DDNet-Server.exe", "QmClient-Updater.exe"}
PACKAGE_SIGNATURE_CONTEXT = b"QmClient update package SHA-256\0"
EXPECTED_PUBLIC_KEY = bytes(
    [
        88,
        90,
        115,
        247,
        47,
        84,
        75,
        33,
        140,
        114,
        135,
        159,
        223,
        101,
        133,
        81,
        150,
        125,
        144,
        190,
        246,
        220,
        67,
        117,
        92,
        49,
        2,
        211,
        109,
        81,
        45,
        156,
    ]
)


class SignedReleaseOutputs(NamedTuple):
    manifest: Path
    manifest_signature: Path
    package_signature: Path


def _normalize_version(version: str) -> str:
    normalized = version.strip()
    if normalized[:1] in {"v", "V"}:
        normalized = normalized[1:]
    parts = normalized.split(".")
    if len(parts) not in {1, 2, 3, 4} or any(
        not part.isascii() or not part.isdigit() for part in parts
    ):
        raise ValueError(f"invalid stable version: {version}")
    if any(int(part) > 2_147_483_647 for part in parts):
        raise ValueError(f"stable version component is too large: {version}")
    if len(normalized) >= 32:
        raise ValueError(f"stable version is too long: {version}")
    return normalized


def _validate_archive_path(name: str) -> str:
    if not name or "\\" in name or "\0" in name or ":" in name:
        raise ValueError(f"unsafe archive path: {name!r}")
    raw_parts = name.split("/")
    path = PurePosixPath(name)
    if path.is_absolute() or any(
        part in {"", ".", ".."} or part.endswith((".", " ")) for part in raw_parts
    ):
        raise ValueError(f"unsafe archive path: {name!r}")
    normalized = path.as_posix()
    if len(normalized.encode("utf-8")) > 1024:
        raise ValueError(f"archive path is too long: {name!r}")
    for part in raw_parts:
        stem = part.split(".", 1)[0].upper()
        if stem in {"CON", "PRN", "AUX", "NUL"} or (
            len(stem) == 4 and stem[:3] in {"COM", "LPT"} and stem[3] in "123456789"
        ):
            raise ValueError(f"reserved Windows archive path: {name!r}")
    return normalized


def normalize_windows_package(package: Path) -> None:
    """Remove the single CPack top-level directory from a Windows ZIP in place."""
    package = package.resolve(strict=True)
    temporary = package.with_name(f".{package.name}.normalized.tmp")
    try:
        with zipfile.ZipFile(package, "r") as source:
            file_names = [
                info.filename.rstrip("/")
                for info in source.infolist()
                if not info.is_dir()
            ]
            if not file_names:
                raise ValueError("Windows package is empty")
            if REQUIRED_FILES.issubset(file_names):
                return
            first_parts = {name.split("/", 1)[0] for name in file_names if "/" in name}
            if len(first_parts) != 1 or any("/" not in name for name in file_names):
                raise ValueError("Windows package must contain one top-level directory")
            prefix = next(iter(first_parts)) + "/"
            seen: set[str] = set()
            with zipfile.ZipFile(temporary, "w", allowZip64=True) as target:
                for info in source.infolist():
                    if not info.filename.startswith(prefix):
                        raise ValueError(
                            "Windows package has entries outside its top-level directory"
                        )
                    stripped = info.filename[len(prefix) :].rstrip("/")
                    if not stripped:
                        continue
                    normalized = _validate_archive_path(stripped)
                    folded = normalized.casefold()
                    if folded in seen:
                        raise ValueError(
                            f"duplicate archive path after normalization: {normalized}"
                        )
                    seen.add(folded)
                    output_info = copy.copy(info)
                    output_info.filename = normalized + ("/" if info.is_dir() else "")
                    if info.is_dir():
                        target.writestr(output_info, b"")
                    else:
                        with (
                            source.open(info, "r") as input_stream,
                            target.open(
                                output_info, "w", force_zip64=True
                            ) as output_stream,
                        ):
                            shutil.copyfileobj(input_stream, output_stream, 1024 * 1024)
        temporary.replace(package)
    finally:
        temporary.unlink(missing_ok=True)


def _hash_stream(stream) -> str:
    digest = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        digest.update(chunk)
    return digest.hexdigest()


def build_manifest(package: Path, version: str) -> dict[str, object]:
    package = package.resolve(strict=True)
    package_size = package.stat().st_size
    if package.name != "QmClient-windows.zip":
        raise ValueError("package must be named QmClient-windows.zip")
    if package_size <= 0 or package_size > MAX_PACKAGE_SIZE:
        raise ValueError("package size is outside the supported range")

    files: list[dict[str, object]] = []
    seen_paths: set[str] = set()
    total_size = 0
    with zipfile.ZipFile(package, "r") as archive:
        for info in archive.infolist():
            path = _validate_archive_path(info.filename.rstrip("/"))
            if info.is_dir():
                continue
            unix_mode = info.external_attr >> 16
            if unix_mode and stat.S_ISLNK(unix_mode):
                raise ValueError(f"symbolic links are not allowed: {path}")
            folded = path.casefold()
            if folded in seen_paths:
                raise ValueError(f"duplicate archive path: {path}")
            seen_paths.add(folded)
            if info.file_size > MAX_FILE_SIZE:
                raise ValueError(f"archive file is too large: {path}")
            total_size += info.file_size
            if total_size > MAX_TOTAL_SIZE or len(files) >= MAX_FILE_COUNT:
                raise ValueError("archive extraction limits exceeded")
            with archive.open(info, "r") as source:
                sha256 = _hash_stream(source)
            files.append({"path": path, "size": info.file_size, "sha256": sha256})

    paths = {entry["path"] for entry in files}
    missing = sorted(REQUIRED_FILES - paths)
    if missing:
        raise ValueError(f"package is missing required files: {', '.join(missing)}")
    files.sort(key=lambda entry: str(entry["path"]))
    with package.open("rb") as source:
        package_sha256 = _hash_stream(source)
    return {
        "schema": 1,
        "version": _normalize_version(version),
        "package": {
            "name": package.name,
            "size": package_size,
            "sha256": package_sha256,
        },
        "files": files,
    }


def _load_private_key(
    private_key_base64: str, expected_public_key: bytes
) -> Ed25519PrivateKey:
    try:
        key = base64.b64decode(private_key_base64.strip(), validate=True)
    except ValueError as error:
        raise ValueError("private key must be valid Base64") from error
    if len(key) != 32:
        raise ValueError("private key must encode a 32-byte Ed25519 seed")
    private_key = Ed25519PrivateKey.from_private_bytes(key)
    public_key = private_key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    if public_key != expected_public_key:
        raise ValueError(
            "private key does not match the Ed25519 public key embedded in QmClient"
        )
    return private_key


def sign_release(
    *,
    package: Path,
    version: str,
    private_key_base64: str,
    output_dir: Path,
    expected_public_key: bytes = EXPECTED_PUBLIC_KEY,
) -> SignedReleaseOutputs:
    manifest = build_manifest(package, version)
    manifest_bytes = (
        json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    if len(manifest_bytes) > MAX_MANIFEST_SIZE:
        raise ValueError("update manifest exceeds the supported size")
    private_key = _load_private_key(private_key_base64, expected_public_key)
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = output_dir / "QmClient-windows-update.json"
    manifest_signature_path = output_dir / "QmClient-windows-update.json.sig"
    package_signature_path = output_dir / "QmClient-windows.zip.sig"
    manifest_path.write_bytes(manifest_bytes)
    manifest_signature_path.write_bytes(private_key.sign(manifest_bytes))
    package_signature_path.write_bytes(
        private_key.sign(
            PACKAGE_SIGNATURE_CONTEXT
            + bytes.fromhex(str(manifest["package"]["sha256"]))
        )
    )
    return SignedReleaseOutputs(
        manifest=manifest_path,
        manifest_signature=manifest_signature_path,
        package_signature=package_signature_path,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create and Ed25519-sign the QmClient Windows update manifest"
    )
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--version", required=True)
    key_group = parser.add_mutually_exclusive_group(required=True)
    key_group.add_argument("--private-key-base64")
    key_group.add_argument("--private-key-env")
    parser.add_argument("--normalize-package", action="store_true")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.normalize_package:
        normalize_windows_package(args.package)
    private_key = (
        args.private_key_base64
        if args.private_key_base64 is not None
        else os.environ.get(args.private_key_env, "")
    )
    if not private_key:
        raise ValueError(
            f"private key environment variable is empty: {args.private_key_env}"
        )
    sign_release(
        package=args.package,
        version=args.version,
        private_key_base64=private_key,
        output_dir=args.output_dir,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
