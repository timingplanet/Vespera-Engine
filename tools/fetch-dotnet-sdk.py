#!/usr/bin/env python3
"""Fetch a clean, verified .NET SDK payload for Vespera release packaging.

The archive URL and SHA-512 digest are obtained from Microsoft's official
release metadata. This keeps official Vespera portable/installer packages from
copying arbitrary SDKs that happen to be installed on the build machine.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import stat
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

# Release tooling must not dirty a clean source checkout with __pycache__.
sys.dont_write_bytecode = True

from release_config import DOTNET_CHANNEL, DOTNET_SDK_VERSION

METADATA_URL = "https://dotnetcli.blob.core.windows.net/dotnet/release-metadata/{channel}/releases.json"


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def ensure_safe_output(output: pathlib.Path) -> None:
    source_root = pathlib.Path(__file__).resolve().parents[1]
    output = output.resolve()
    if output == source_root or output in source_root.parents or source_root in output.parents:
        fail(f"Refusing to replace .NET SDK output that overlaps the Vespera source tree: {output}")


def read_json(url: str) -> dict:
    print(f"Fetching {url}", flush=True)
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.load(response)


def find_sdk_file(metadata: dict, version: str, rid: str) -> tuple[str, str]:
    archive_suffix = ".zip" if rid.startswith("win-") else ".tar.gz"
    expected_basename = f"dotnet-sdk-{version}-{rid}{archive_suffix}"
    for release in metadata.get("releases", []):
        sdks = []
        if isinstance(release.get("sdk"), dict):
            sdks.append(release["sdk"])
        sdks.extend(x for x in release.get("sdks", []) if isinstance(x, dict))
        for sdk in sdks:
            if sdk.get("version") != version:
                continue
            for item in sdk.get("files", []):
                if item.get("rid") != rid:
                    continue
                url = str(item.get("url", ""))
                digest = str(item.get("hash", "")).lower()
                if pathlib.PurePosixPath(url).name == expected_basename and len(digest) == 128:
                    return url, digest
    fail(f".NET SDK {version} for {rid} was not found in Microsoft release metadata")


def download_verified(url: str, digest: str, destination: pathlib.Path) -> None:
    print(f"Downloading {url}", flush=True)
    hasher = hashlib.sha512()
    with urllib.request.urlopen(url, timeout=120) as response, destination.open("wb") as out:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)
            hasher.update(chunk)
    actual = hasher.hexdigest().lower()
    if actual != digest:
        destination.unlink(missing_ok=True)
        fail(f".NET SDK SHA-512 mismatch: expected {digest}, got {actual}")


def safe_target(root: pathlib.Path, member: str) -> pathlib.Path:
    target = (root / member).resolve()
    try:
        target.relative_to(root.resolve())
    except ValueError:
        fail(f"Unsafe path in .NET SDK archive: {member}")
    return target


def extract_zip(archive: pathlib.Path, output: pathlib.Path) -> None:
    with zipfile.ZipFile(archive) as zf:
        for info in zf.infolist():
            safe_target(output, info.filename)
        zf.extractall(output)


def extract_tar(archive: pathlib.Path, output: pathlib.Path) -> None:
    with tarfile.open(archive, "r:gz") as tf:
        for member in tf.getmembers():
            safe_target(output, member.name)
            if member.issym() or member.islnk():
                # Microsoft SDK archives may use links. Reject links that could
                # resolve outside the destination tree.
                base = (output / pathlib.PurePosixPath(member.name).parent).resolve()
                link_target = (base / member.linkname).resolve()
                try:
                    link_target.relative_to(output.resolve())
                except ValueError:
                    fail(f"Unsafe link in .NET SDK archive: {member.name} -> {member.linkname}")
        tf.extractall(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--version", default=DOTNET_SDK_VERSION)
    parser.add_argument("--channel", default=DOTNET_CHANNEL)
    args = parser.parse_args()

    rid = "win-x64" if args.platform == "windows" else "linux-x64"
    extension = ".zip" if args.platform == "windows" else ".tar.gz"
    output = pathlib.Path(args.output).expanduser().resolve()
    ensure_safe_output(output)
    metadata = read_json(METADATA_URL.format(channel=args.channel))
    url, digest = find_sdk_file(metadata, args.version, rid)

    with tempfile.TemporaryDirectory(prefix="vespera-dotnet-") as temp_dir:
        archive = pathlib.Path(temp_dir) / f"dotnet-sdk-{args.version}-{rid}{extension}"
        download_verified(url, digest, archive)
        if output.exists():
            shutil.rmtree(output)
        output.mkdir(parents=True)
        if args.platform == "windows":
            extract_zip(archive, output)
        else:
            extract_tar(archive, output)

    launcher = output / ("dotnet.exe" if args.platform == "windows" else "dotnet")
    if not launcher.is_file():
        fail(f"Extracted .NET SDK has no launcher: {launcher}")
    if args.platform == "linux":
        launcher.chmod(launcher.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    sdk_dir = output / "sdk" / args.version
    if not sdk_dir.is_dir():
        fail(f"Extracted .NET SDK is missing sdk/{args.version}")
    (output / ".vespera-sdk-version").write_text(args.version + "\n", encoding="utf-8")
    print(f"Verified .NET SDK {args.version} staged at {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
