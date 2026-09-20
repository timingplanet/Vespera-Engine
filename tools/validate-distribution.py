#!/usr/bin/env python3
"""Validate an assembled Vespera Engine end-user distribution.

This is intentionally independent from the source/build tree. It verifies the
installed layout that the Hub and Editor discover at runtime, including the
bundled .NET SDK expected by official packages.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import subprocess
import sys

FILE_MANIFEST_NAME = "Vespera.FileManifest.sha256"


def parse_manifest(path: pathlib.Path) -> dict[str, str]:
    if not path.is_file():
        raise SystemExit(f"Distribution manifest is missing: {path}")
    result: dict[str, str] = {}
    header_seen = False
    end_seen = False
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if end_seen:
            raise SystemExit(f"Distribution manifest has content after end_distribution at line {line_number}")
        if line == "vespera_distribution 1":
            if header_seen:
                raise SystemExit(f"Duplicate distribution manifest header at line {line_number}")
            header_seen = True
            continue
        if line == "end_distribution":
            if not header_seen:
                raise SystemExit("Distribution manifest footer appears before its header")
            end_seen = True
            continue
        if not header_seen:
            raise SystemExit(f"Distribution manifest data appears before its header at line {line_number}")
        match = re.fullmatch(r'([A-Za-z0-9_]+)\s+"([^"]*)"', line)
        if not match:
            raise SystemExit(f"Malformed distribution manifest line {line_number}: {line}")
        key, value = match.groups()
        if key in result:
            raise SystemExit(f"Duplicate distribution manifest key at line {line_number}: {key}")
        result[key] = value
    if not header_seen or not end_seen:
        raise SystemExit("Distribution manifest header/footer is invalid")
    return result


def require_file(path: pathlib.Path, label: str) -> None:
    if not path.is_file():
        raise SystemExit(f"Missing {label}: {path}")


def require_dir(path: pathlib.Path, label: str) -> None:
    if not path.is_dir():
        raise SystemExit(f"Missing {label}: {path}")


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_file_manifest(root: pathlib.Path) -> dict[str, str]:
    manifest_path = root / FILE_MANIFEST_NAME
    require_file(manifest_path, "distribution payload SHA-256 manifest")
    entries: dict[str, str] = {}
    casefolded: dict[str, str] = {}
    for line_number, raw in enumerate(manifest_path.read_text(encoding="utf-8").splitlines(), start=1):
        if not raw.strip():
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", raw)
        if not match:
            raise SystemExit(f"Malformed payload SHA-256 manifest line {line_number}: {raw!r}")
        digest, relative_text = match.groups()
        if "\\" in relative_text:
            raise SystemExit(f"Payload manifest path must use forward slashes: {relative_text}")
        relative = pathlib.PurePosixPath(relative_text)
        if relative.is_absolute() or not relative.parts or any(part in {"", ".", ".."} for part in relative.parts):
            raise SystemExit(f"Unsafe payload manifest path: {relative_text}")
        normalized = relative.as_posix()
        if normalized == FILE_MANIFEST_NAME:
            raise SystemExit(f"Payload manifest must not hash itself: {normalized}")
        if normalized in entries:
            raise SystemExit(f"Duplicate payload manifest path: {normalized}")
        folded = normalized.casefold()
        if folded in casefolded:
            raise SystemExit(f"Case-colliding payload manifest paths are not portable: {casefolded[folded]} and {normalized}")
        casefolded[folded] = normalized
        entries[normalized] = digest
    if not entries:
        raise SystemExit("Distribution payload SHA-256 manifest is empty")
    return entries


def validate_file_manifest(root: pathlib.Path, *, reject_unlisted: bool) -> None:
    entries = parse_file_manifest(root)
    for relative, expected_digest in entries.items():
        path = root.joinpath(*pathlib.PurePosixPath(relative).parts)
        if path.is_symlink() or not path.is_file():
            raise SystemExit(f"Payload manifest file is missing or not a regular file: {relative}")
        actual = sha256_file(path)
        if actual != expected_digest:
            raise SystemExit(f"Payload SHA-256 mismatch for {relative}: expected {expected_digest}, got {actual}")
    if reject_unlisted:
        listed = set(entries) | {FILE_MANIFEST_NAME}
        actual_files: set[str] = set()
        for path in root.rglob("*"):
            if path.is_symlink():
                raise SystemExit(f"Distribution contains an unexpected symlink: {path.relative_to(root)}")
            if path.is_file():
                actual_files.add(path.relative_to(root).as_posix())
        extras = sorted(actual_files - listed)
        if extras:
            preview = ", ".join(extras[:8])
            if len(extras) > 8:
                preview += f", ... ({len(extras)} total)"
            raise SystemExit(f"Distribution contains files not listed in {FILE_MANIFEST_NAME}: {preview}")


def reject_generated_source_debris(root: pathlib.Path) -> None:
    forbidden_dirs = {"bin", "obj", ".vs", ".idea", ".vscode", "__pycache__"}
    forbidden_suffixes = {".pyc", ".user", ".suo", ".tmp", ".bak"}
    for subtree_name in ("templates", "sdk"):
        subtree = root / subtree_name
        if not subtree.is_dir():
            continue
        for path in subtree.rglob("*"):
            relative = path.relative_to(root)
            if any(part in forbidden_dirs for part in relative.parts):
                raise SystemExit(f"Generated source/build directory leaked into distribution: {relative}")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes:
                raise SystemExit(f"Generated source/build file leaked into distribution: {relative}")


def check_executable(path: pathlib.Path, platform: str) -> None:
    require_file(path, "executable")
    if platform == "linux" and not os.access(path, os.X_OK):
        raise SystemExit(f"Linux executable bit is missing: {path}")


def run_identity(path: pathlib.Path, expected_prefix: str) -> None:
    completed = subprocess.run(
        [str(path), "--version"],
        cwd=path.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=15,
        check=False,
    )
    output = completed.stdout.strip()
    if completed.returncode != 0:
        raise SystemExit(f"{path.name} --version failed ({completed.returncode}): {output}")
    if not output.startswith(expected_prefix):
        raise SystemExit(f"Unexpected {path.name} identity: {output!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--expect-version")
    parser.add_argument("--require-dotnet", action="store_true")
    parser.add_argument("--run-tool-smoke", action="store_true")
    parser.add_argument("--reject-unlisted", action="store_true", help="Reject files not covered by the payload SHA-256 manifest")
    parser.add_argument(
        "--allow-noncanonical-root-name",
        action="store_true",
        help="Allow an installed distribution root whose directory name differs from the canonical package root",
    )
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    require_dir(root, "distribution root")
    manifest = parse_manifest(root / "Vespera.DistributionManifest.txt")
    version = manifest.get("version", "")
    expected_platform = f"{args.platform}-x64"
    if args.expect_version and version != args.expect_version:
        raise SystemExit(f"Version mismatch: manifest has {version!r}, expected {args.expect_version!r}")
    if manifest.get("platform") != expected_platform:
        raise SystemExit(
            f"Platform mismatch: manifest has {manifest.get('platform')!r}, expected {expected_platform!r}"
        )

    platform_label = "Windows" if args.platform == "windows" else "Linux"
    expected_root_name = f"VesperaEngine-{version}-{platform_label}-x64"
    if not args.allow_noncanonical_root_name and root.name != expected_root_name:
        raise SystemExit(
            f"Distribution root name mismatch: found {root.name!r}, expected {expected_root_name!r}"
        )

    suffix = ".exe" if args.platform == "windows" else ""
    expected_manifest_paths = {
        "entry": "VesperaHub",
        "editor": "VesperaEditor",
        "player": "runtime/VesperaPlayer",
        "builder": "tools/VesperaBuilder",
        "dotnet": "dotnet",
    }
    for key, expected in expected_manifest_paths.items():
        if manifest.get(key) != expected:
            raise SystemExit(
                f"Distribution manifest {key} mismatch: {manifest.get(key)!r}, expected {expected!r}"
            )
    required_manifest_keys = {"version", "platform", "dotnet_sdk", *expected_manifest_paths.keys()}
    missing_manifest_keys = sorted(required_manifest_keys - manifest.keys())
    if missing_manifest_keys:
        raise SystemExit("Distribution manifest is missing required keys: " + ", ".join(missing_manifest_keys))
    validate_file_manifest(root, reject_unlisted=args.reject_unlisted)

    hub = root / ("VesperaHub" + suffix)
    editor = root / ("VesperaEditor" + suffix)
    player = root / "runtime" / ("VesperaPlayer" + suffix)
    builder = root / "tools" / ("VesperaBuilder" + suffix)
    packager = root / "tools" / ("VesperaPackager" + suffix)
    for binary in (hub, editor, player, builder, packager):
        check_executable(binary, args.platform)

    reject_generated_source_debris(root)

    for path, label in (
        (root / "templates" / "3d" / "Template.vesperaproject", "3D project template"),
        (root / "templates" / "2d" / "Template.vesperaproject", "2D project template"),
        (root / "templates" / "empty" / "Template.vesperaproject", "empty project template"),
        (root / "ui" / "project_hub.rml", "Project Hub UI"),
        (root / "sdk" / "Vespera.NET" / "Vespera.NET.csproj", "Vespera.NET SDK"),
        (root / "sdk" / "Vespera.ScriptTool" / "Vespera.ScriptTool.csproj", "script metadata tool"),
        (root / "branding" / "vespera_icon.png", "branding"),
        (root / "legal" / "Vespera.LICENSE.txt", "license"),
        (root / "legal" / "Vespera.ThirdPartyNotices.md", "third-party notices"),
        (root / "runtime" / "legal" / "Vespera.LICENSE.txt", "runtime license"),
    ):
        require_file(path, label)

    if args.require_dotnet:
        dotnet = root / "dotnet" / ("dotnet.exe" if args.platform == "windows" else "dotnet")
        check_executable(dotnet, args.platform)
        require_dir(root / "dotnet" / "sdk", ".NET SDK")
        require_dir(root / "dotnet" / "host" / "fxr", ".NET host/fxr")
        sdk_versions = sorted(p.name for p in (root / "dotnet" / "sdk").iterdir() if p.is_dir())
        if not sdk_versions:
            raise SystemExit("Bundled .NET SDK directory is empty")
        if not any((root / "dotnet" / "host" / "fxr").iterdir()):
            raise SystemExit("Bundled .NET host/fxr directory is empty")
        manifest_sdk = manifest.get("dotnet_sdk", "")
        if not manifest_sdk or manifest_sdk == "not-bundled":
            raise SystemExit("Distribution manifest does not identify the bundled .NET SDK")
        if sdk_versions != [manifest_sdk]:
            raise SystemExit(
                f"Bundled .NET SDK set {sdk_versions!r} does not match manifest dotnet_sdk {manifest_sdk!r}")

    if args.run_tool_smoke:
        run_identity(hub, "Vespera Hub ")
        run_identity(editor, "Vespera Editor ")
        run_identity(player, "Vespera Player ")
        run_identity(builder, "Vespera Builder ")
        run_identity(packager, "Vespera Packager ")

    print(f"Vespera distribution validation passed: {version} ({expected_platform})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
