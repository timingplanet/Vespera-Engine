#!/usr/bin/env python3
"""Assemble prebuilt Vespera Engine distributions from an existing native build.

This intentionally packages Vespera itself, not a game project. The produced
layout is what the Hub/Editor discover at runtime, so normal users do not need
CMake, Visual Studio, Ninja, PowerShell, or a system .NET SDK.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import shutil
import stat
import sys
import tarfile
import tempfile
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FILE_MANIFEST_NAME = "Vespera.FileManifest.sha256"


def version_label() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(VESPERA_VERSION_LABEL\s+"([^"]+)"\)', text)
    if not match:
        raise SystemExit("Could not read VESPERA_VERSION_LABEL from CMakeLists.txt")
    return match.group(1)


def require_file(path: pathlib.Path, label: str) -> pathlib.Path:
    if not path.is_file():
        raise SystemExit(f"Missing {label}: {path}")
    return path


def candidate_binary(build: pathlib.Path, platform: str, configuration: str, target: str) -> pathlib.Path:
    ext = ".exe" if platform == "windows" else ""
    locations = {
        "vespera_project_hub": pathlib.Path("examples/project_hub") / ("vespera_project_hub" + ext),
        "vespera_editor": pathlib.Path("editor") / ("vespera_editor" + ext),
        "vespera_player": pathlib.Path("runtime/player") / ("vespera_player" + ext),
        "vespera_builder": pathlib.Path("tools/builder") / ("vespera_builder" + ext),
        "vespera_packager": pathlib.Path("tools/packager") / ("vespera_packager" + ext),
    }
    base = build / locations[target]
    if platform == "windows":
        configured = base.parent / configuration / base.name
        if configured.is_file():
            return configured
    return base


def copy_tree(source: pathlib.Path, destination: pathlib.Path) -> None:
    if not source.is_dir():
        raise SystemExit(f"Required directory is missing: {source}")
    shutil.copytree(source, destination, dirs_exist_ok=True, symlinks=False)


def copy_source_tree(source: pathlib.Path, destination: pathlib.Path) -> None:
    """Copy distributable source inputs without local build/cache debris."""
    if not source.is_dir():
        raise SystemExit(f"Required source directory is missing: {source}")
    ignore = shutil.ignore_patterns(
        "bin", "obj", ".vs", ".idea", ".vscode", "__pycache__",
        "*.user", "*.suo", "*.pyc", "*.pdb", "*.tmp", "*.bak",
    )
    shutil.copytree(source, destination, dirs_exist_ok=True, symlinks=False, ignore=ignore)


def detect_dotnet_root(explicit: str | None) -> pathlib.Path | None:
    if explicit:
        return pathlib.Path(explicit).expanduser().resolve()
    for name in ("DOTNET_ROOT", "DOTNET_ROOT_X64"):
        value = os.environ.get(name)
        if value:
            candidate = pathlib.Path(value).expanduser().resolve()
            if candidate.is_dir():
                return candidate
    executable = shutil.which("dotnet")
    if executable:
        return pathlib.Path(executable).resolve().parent
    return None


def validate_dotnet_root(root: pathlib.Path, platform: str) -> None:
    executable = root / ("dotnet.exe" if platform == "windows" else "dotnet")
    if not executable.is_file():
        raise SystemExit(f"dotnet root has no launcher: {executable}")
    sdk = root / "sdk"
    fxr = root / "host" / "fxr"
    if not sdk.is_dir() or not any(p.is_dir() for p in sdk.iterdir()):
        raise SystemExit(f"dotnet root has no SDK: {sdk}")
    if not fxr.is_dir() or not any(p.is_dir() for p in fxr.iterdir()):
        raise SystemExit(f"dotnet root has no host/fxr: {fxr}")


def dotnet_sdk_versions(root: pathlib.Path) -> list[str]:
    sdk = root / "sdk"
    if not sdk.is_dir():
        return []
    return sorted((p.name for p in sdk.iterdir() if p.is_dir()), reverse=True)


def copy_executable(source: pathlib.Path, destination: pathlib.Path, platform: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if platform == "linux":
        destination.chmod(destination.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def write_manifest(stage: pathlib.Path, version: str, platform: str, dotnet_sdk: str) -> None:
    (stage / "Vespera.DistributionManifest.txt").write_text(
        "vespera_distribution 1\n"
        f'version "{version}"\n'
        f'platform "{platform}-x64"\n'
        f'dotnet_sdk "{dotnet_sdk}"\n'
        'entry "VesperaHub"\n'
        'editor "VesperaEditor"\n'
        'player "runtime/VesperaPlayer"\n'
        'builder "tools/VesperaBuilder"\n'
        'dotnet "dotnet"\n'
        "end_distribution\n",
        encoding="utf-8",
    )


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_file_manifest(stage: pathlib.Path) -> None:
    lines: list[str] = []
    for path in sorted(stage.rglob("*")):
        if path.is_symlink():
            raise SystemExit(f"Distribution payload must not contain symlinks: {path.relative_to(stage)}")
        if not path.is_file():
            continue
        relative = path.relative_to(stage).as_posix()
        if relative == FILE_MANIFEST_NAME:
            continue
        lines.append(f"{sha256_file(path)}  {relative}")
    if not lines:
        raise SystemExit("Distribution payload manifest would be empty")
    (stage / FILE_MANIFEST_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")


ARCHIVE_EPOCH = 946684800  # 2000-01-01T00:00:00Z; deterministic and never "Tomorrow".
ARCHIVE_ZIP_TIME = (2000, 1, 1, 0, 0, 0)


def archive_zip(stage: pathlib.Path, output: pathlib.Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for path in sorted(stage.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(stage.parent).as_posix()
            info = zipfile.ZipInfo(relative, date_time=ARCHIVE_ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (path.stat().st_mode & 0xFFFF) << 16
            with path.open("rb") as source:
                zf.writestr(info, source.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)


def archive_tar(stage: pathlib.Path, output: pathlib.Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)

    def normalized(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.mtime = ARCHIVE_EPOCH
        info.uid = 0
        info.gid = 0
        info.uname = "root"
        info.gname = "root"
        return info

    import gzip
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=6, mtime=ARCHIVE_EPOCH) as gz:
            with tarfile.open(fileobj=gz, mode="w") as tf:
                tf.add(stage, arcname=stage.name, recursive=True, filter=normalized)


def source_archive(output: pathlib.Path, version: str) -> None:
    excluded_top = {
        ".git", ".vs", ".idea", ".vscode", ".qa-hotfix-backups",
        "dist", "release", ".cache", ".venv", "node_modules",
    }
    excluded_files = {"HANDOFF.md", "LICENSE-NOTE.txt", "desktop.ini", "Thumbs.db"}
    excluded_suffixes = {".user", ".suo", ".pdb", ".obj", ".ilk", ".pyc", ".zip", ".log", ".tmp", ".bak", ".orig"}
    prefix = f"vespera-engine-{version}"
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for path in sorted(ROOT.rglob("*")):
            relative = path.relative_to(ROOT)
            if not relative.parts:
                continue
            top = relative.parts[0]
            top_path = ROOT / top
            if top in excluded_top or (top_path.is_dir() and (top.startswith("build") or top.startswith("release-") or top.startswith("dist-"))):
                continue
            if any(part in {"__pycache__", ".vespera", "bin", "obj"} for part in relative.parts):
                continue
            if path.name in excluded_files or (len(relative.parts) == 1 and path.name.upper().endswith("-HANDOFF.MD")):
                continue
            if path.is_symlink() or not path.is_file() or path.suffix.lower() in excluded_suffixes:
                continue
            archive_name = (pathlib.PurePosixPath(prefix) / relative.as_posix()).as_posix()
            info = zipfile.ZipInfo(archive_name, date_time=ARCHIVE_ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (path.stat().st_mode & 0xFFFF) << 16
            with path.open("rb") as source:
                zf.writestr(info, source.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)


def validate_stage(stage: pathlib.Path, platform: str, require_dotnet: bool) -> None:
    suffix = ".exe" if platform == "windows" else ""
    required_files = [
        stage / ("VesperaHub" + suffix),
        stage / ("VesperaEditor" + suffix),
        stage / "runtime" / ("VesperaPlayer" + suffix),
        stage / "tools" / ("VesperaBuilder" + suffix),
        stage / "tools" / ("VesperaPackager" + suffix),
        stage / "templates" / "3d" / "Template.vesperaproject",
        stage / "templates" / "2d" / "Template.vesperaproject",
        stage / "templates" / "empty" / "Template.vesperaproject",
        stage / "ui" / "project_hub.rml",
        stage / "sdk" / "Vespera.NET" / "Vespera.NET.csproj",
        stage / "sdk" / "Vespera.ScriptTool" / "Vespera.ScriptTool.csproj",
        stage / "branding" / "vespera_icon.png",
        stage / "runtime" / "branding" / "vespera_icon.png",
        stage / "runtime" / "legal" / "Vespera.LICENSE.txt",
        stage / "legal" / "Vespera.LICENSE.txt",
        stage / "legal" / "Vespera.ThirdPartyNotices.md",
        stage / "Vespera.DistributionManifest.txt",
        stage / FILE_MANIFEST_NAME,
    ]
    missing = [str(path.relative_to(stage)) for path in required_files if not path.is_file()]
    if missing:
        raise SystemExit("Distribution self-check failed; missing: " + ", ".join(missing))
    if require_dotnet:
        launcher = stage / "dotnet" / ("dotnet.exe" if platform == "windows" else "dotnet")
        if not launcher.is_file():
            raise SystemExit(f"Distribution self-check failed; bundled .NET launcher is missing: {launcher}")
        if not (stage / "dotnet" / "sdk").is_dir() or not (stage / "dotnet" / "host" / "fxr").is_dir():
            raise SystemExit("Distribution self-check failed; bundled .NET SDK/host payload is incomplete")
    print("Distribution self-check: PASS")


def assemble(args: argparse.Namespace) -> tuple[pathlib.Path, pathlib.Path]:
    platform = args.platform
    version = args.version or version_label()
    build = pathlib.Path(args.build_dir).resolve()
    out = pathlib.Path(args.output_dir).resolve()
    stage_parent = out / "stage"
    stage = stage_parent / f"VesperaEngine-{version}-{platform.capitalize()}-x64"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    names = {
        "vespera_project_hub": "VesperaHub.exe" if platform == "windows" else "VesperaHub",
        "vespera_editor": "VesperaEditor.exe" if platform == "windows" else "VesperaEditor",
        "vespera_player": "VesperaPlayer.exe" if platform == "windows" else "VesperaPlayer",
        "vespera_builder": "VesperaBuilder.exe" if platform == "windows" else "VesperaBuilder",
        "vespera_packager": "VesperaPackager.exe" if platform == "windows" else "VesperaPackager",
    }
    destinations = {
        "vespera_project_hub": stage / names["vespera_project_hub"],
        "vespera_editor": stage / names["vespera_editor"],
        "vespera_player": stage / "runtime" / names["vespera_player"],
        "vespera_builder": stage / "tools" / names["vespera_builder"],
        "vespera_packager": stage / "tools" / names["vespera_packager"],
    }
    for target, destination in destinations.items():
        source = require_file(candidate_binary(build, platform, args.configuration, target), target)
        copy_executable(source, destination, platform)

    copy_tree(ROOT / "examples" / "project_hub" / "ui", stage / "ui")
    copy_source_tree(ROOT / "templates", stage / "templates")
    copy_tree(ROOT / "branding", stage / "branding")
    copy_tree(ROOT / "branding", stage / "runtime" / "branding")
    (stage / "legal").mkdir(parents=True, exist_ok=True)
    (stage / "runtime" / "legal").mkdir(parents=True, exist_ok=True)
    for source, name in (
        (ROOT / "LICENSE", "Vespera.LICENSE.txt"),
        (ROOT / "THIRD_PARTY_NOTICES.md", "Vespera.ThirdPartyNotices.md"),
    ):
        shutil.copy2(source, stage / "legal" / name)
        shutil.copy2(source, stage / "runtime" / "legal" / name)

    copy_source_tree(ROOT / "managed" / "Vespera.NET", stage / "sdk" / "Vespera.NET")
    copy_source_tree(ROOT / "managed" / "Vespera.ScriptTool", stage / "sdk" / "Vespera.ScriptTool")

    dotnet_root = detect_dotnet_root(args.dotnet_root)
    bundled_dotnet_sdk = "not-bundled"
    if args.skip_dotnet:
        (stage / "dotnet-NOT-BUNDLED.txt").write_text(
            "This validation package was assembled without the private .NET SDK. Official release packages must include dotnet/.\n",
            encoding="utf-8",
        )
    else:
        if dotnet_root is None:
            raise SystemExit("No .NET SDK root found. Pass --dotnet-root or use --skip-dotnet only for structural validation.")
        validate_dotnet_root(dotnet_root, platform)
        sdk_versions = dotnet_sdk_versions(dotnet_root)
        if args.expect_dotnet_sdk:
            if args.expect_dotnet_sdk not in sdk_versions:
                raise SystemExit(
                    f"Requested .NET SDK {args.expect_dotnet_sdk} is not present in {dotnet_root}; found {sdk_versions}")
            unexpected = [version for version in sdk_versions if version != args.expect_dotnet_sdk]
            if unexpected:
                raise SystemExit(
                    "Official release .NET root must be clean; unexpected SDKs are present: " + ", ".join(unexpected))
            bundled_dotnet_sdk = args.expect_dotnet_sdk
        else:
            bundled_dotnet_sdk = sdk_versions[0]
        copy_tree(dotnet_root, stage / "dotnet")

    write_manifest(stage, version, platform, bundled_dotnet_sdk)
    shutil.copy2(ROOT / "README.md", stage / "README.md")
    write_file_manifest(stage)
    validate_stage(stage, platform, require_dotnet=not args.skip_dotnet)

    if platform == "windows":
        archive = out / f"VesperaEngine-{version}-Windows-x64-Portable.zip"
        archive_zip(stage, archive)
    else:
        archive = out / f"VesperaEngine-{version}-Linux-x64.tar.gz"
        archive_tar(stage, archive)

    if args.source_archive:
        source_archive(out / f"VesperaEngine-{version}-Source.zip", version)

    print(f"Distribution stage: {stage}")
    print(f"Release archive: {archive}")
    return stage, archive


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--configuration", choices=("Debug", "Development", "Release"), default="Release")
    parser.add_argument("--output-dir", default="release")
    parser.add_argument("--version")
    parser.add_argument("--dotnet-root")
    parser.add_argument("--expect-dotnet-sdk", help="Require a clean dotnet root containing exactly this SDK version")
    parser.add_argument("--skip-dotnet", action="store_true", help="Structural packaging only; not valid for public releases")
    parser.add_argument("--source-archive", action="store_true")
    args = parser.parse_args()
    assemble(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
