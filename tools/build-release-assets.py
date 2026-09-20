#!/usr/bin/env python3
"""Build and assemble official Vespera Engine binary release artifacts.

This is the single release entry point used by CI. It configures a clean native
Release build, builds the installed-product targets, assembles the portable
Vespera distribution, and on Windows compiles the installer from that exact
portable stage.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

# Release tooling must not dirty a clean source checkout with __pycache__.
sys.dont_write_bytecode = True

from release_config import DOTNET_SDK_VERSION

ROOT = pathlib.Path(__file__).resolve().parents[1]


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def version_label() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(VESPERA_VERSION_LABEL\s+"([^"]+)"\)', text)
    if not match:
        fail("Could not read VESPERA_VERSION_LABEL from CMakeLists.txt")
    return match.group(1)


def paths_overlap(a: pathlib.Path, b: pathlib.Path) -> bool:
    a = a.resolve(); b = b.resolve()
    return a == b or a in b.parents or b in a.parents

def ensure_safe_release_paths(build_dir: pathlib.Path, output_dir: pathlib.Path, dotnet_root: pathlib.Path) -> None:
    source_root = ROOT.resolve()
    for path, label in ((build_dir.resolve(), "release build"), (output_dir.resolve(), "release output")):
        if path == source_root or path in source_root.parents:
            fail(f"Refusing to use an unsafe {label} directory: {path}")
        try:
            relative = path.relative_to(source_root)
        except ValueError:
            relative = None
        if relative is not None:
            top = relative.parts[0] if relative.parts else ""
            if not (top.startswith("build") or top.startswith("release") or top.startswith("dist") or top == "out"):
                fail(f"Refusing to delete source-owned path for {label}: {path}. Use a build*/release*/dist*/out directory or a path outside the checkout.")
        if path == dotnet_root.resolve() or path in dotnet_root.resolve().parents:
            fail(f"Refusing to use {label} directory that contains the bundled .NET root: {path}")
    if paths_overlap(build_dir, output_dir):
        fail(f"Release build and output directories must not overlap: {build_dir} vs {output_dir}")


def run(command: list[str], *, cwd: pathlib.Path = ROOT, timeout: int = 1800) -> None:
    print("+", " ".join(command), flush=True)
    try:
        completed = subprocess.run(command, cwd=cwd, check=False, timeout=timeout)
    except subprocess.TimeoutExpired:
        fail(f"Release command timed out after {timeout}s: {command[0]}")
    if completed.returncode != 0:
        fail(f"Release command failed with exit code {completed.returncode}: {command[0]}")




def find_cmake() -> pathlib.Path:
    on_path = shutil.which("cmake")
    if on_path:
        return pathlib.Path(on_path).resolve()
    if os.name == "nt":
        roots = [
            pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Microsoft Visual Studio",
            pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio",
        ]
        candidates: list[pathlib.Path] = []
        for root in roots:
            if root.is_dir():
                candidates.extend(root.glob("*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"))
        if candidates:
            # Prefer the newest Visual Studio/CMake installation by lexical path.
            return sorted(candidates, reverse=True)[0].resolve()
    fail("CMake was not found on PATH or in a Visual Studio installation")


def find_iscc(explicit: str | None) -> pathlib.Path:
    if explicit:
        path = pathlib.Path(explicit).expanduser().resolve()
        if path.is_file():
            return path
        fail(f"Inno Setup compiler not found: {path}")
    which = shutil.which("iscc") or shutil.which("ISCC.exe")
    if which:
        return pathlib.Path(which).resolve()
    candidates = [
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Inno Setup 6" / "ISCC.exe",
        pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Inno Setup 6" / "ISCC.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    fail("Inno Setup compiler (ISCC.exe) was not found")


def configure_and_build(platform: str, build_dir: pathlib.Path, configuration: str) -> None:
    # Official release assembly must never inherit a stale CMake cache, old
    # configuration, or binaries from a previous checkout. --skip-build is the
    # explicit escape hatch used by structural tooling tests.
    resolved_root = ROOT.resolve()
    resolved_build = build_dir.resolve()
    if resolved_build == resolved_root or resolved_build in resolved_root.parents:
        fail(f"Refusing to use an unsafe release build directory: {resolved_build}")
    if build_dir.exists():
        print(f"Removing stale release build tree: {build_dir}", flush=True)
        shutil.rmtree(build_dir)
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    cmake = str(find_cmake())
    configure = [
        cmake, "-S", str(ROOT), "-B", str(build_dir),
        "-DVESPERA_BUILD_EDITOR=ON",
        "-DVESPERA_BUILD_EXAMPLES=ON",
        "-DVESPERA_BUILD_TOOLS=ON",
        "-DVESPERA_RMLUI_UI=ON",
        "-DVESPERA_LUA_SCRIPTING=ON",
        "-DVESPERA_VULKAN_RENDERER=ON",
        "-DVESPERA_WARNINGS_AS_ERRORS=ON",
    ]
    if platform == "windows":
        configure.append("-DVESPERA_STATIC_MSVC_RUNTIME=ON")
    if platform == "linux":
        configure += ["-G", "Ninja", f"-DCMAKE_BUILD_TYPE={configuration}"]
    run(configure, timeout=900)

    targets = ["vespera_player", "vespera_project_hub", "vespera_editor", "vespera_builder", "vespera_packager"]
    build = [cmake, "--build", str(build_dir), "--target", *targets]
    if platform == "windows":
        build += ["--config", configuration]
    run(build, timeout=2400)


def package_distribution(platform: str, build_dir: pathlib.Path, configuration: str,
                         output_dir: pathlib.Path, dotnet_root: pathlib.Path, version: str) -> pathlib.Path:
    command = [
        sys.executable, str(ROOT / "tools" / "package-engine.py"),
        "--platform", platform,
        "--build-dir", str(build_dir),
        "--configuration", configuration,
        "--output-dir", str(output_dir),
        "--version", version,
        "--dotnet-root", str(dotnet_root),
        "--expect-dotnet-sdk", DOTNET_SDK_VERSION,
    ]
    run(command, timeout=1200)
    platform_name = "Windows" if platform == "windows" else "Linux"
    stage = output_dir / "stage" / f"VesperaEngine-{version}-{platform_name}-x64"
    if not stage.is_dir():
        fail(f"Distribution stage was not produced: {stage}")
    return stage


def build_installer(stage: pathlib.Path, output_dir: pathlib.Path, version: str, iscc: pathlib.Path) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    basename = f"VesperaEngine-{version}-Windows-x64-Setup"
    script = ROOT / "packaging" / "windows" / "VesperaEngine.iss"
    command = [
        str(iscc),
        f"/DSourceDir={stage}",
        f"/DVersion={version}",
        f"/DOutputDir={output_dir}",
        f"/DOutputBase={basename}",
        str(script),
    ]
    run(command, timeout=1200)
    installer = output_dir / f"{basename}.exe"
    if not installer.is_file():
        fail(f"Installer compiler completed but output is missing: {installer}")
    return installer


def validate_outputs(platform: str, output_dir: pathlib.Path, version: str, require_installer: bool) -> None:
    portable = output_dir / (
        f"VesperaEngine-{version}-Windows-x64-Portable.zip" if platform == "windows"
        else f"VesperaEngine-{version}-Linux-x64.tar.gz"
    )
    if not portable.is_file():
        fail(f"Portable release artifact is missing: {portable}")
    if platform == "windows" and require_installer:
        installer = output_dir / f"VesperaEngine-{version}-Windows-x64-Setup.exe"
        if not installer.is_file():
            fail(f"Windows installer is missing: {installer}")
    print(f"Release artifact: {portable}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--configuration", choices=("Debug", "Development", "Release"), default="Release")
    parser.add_argument("--output-dir", default="release")
    parser.add_argument("--dotnet-root", required=True)
    parser.add_argument("--require-installer", action="store_true")
    parser.add_argument("--iscc")
    parser.add_argument("--skip-build", action="store_true", help="Package an already-built native tree")
    args = parser.parse_args()

    if args.platform != "windows" and args.require_installer:
        fail("--require-installer is only valid for Windows")

    version = version_label()
    build_dir = pathlib.Path(args.build_dir).expanduser().resolve()
    output_dir = pathlib.Path(args.output_dir).expanduser().resolve()
    dotnet_root = pathlib.Path(args.dotnet_root).expanduser().resolve()
    if not dotnet_root.is_dir():
        fail(f"Bundled .NET root does not exist: {dotnet_root}")
    ensure_safe_release_paths(build_dir, output_dir, dotnet_root)

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    if not args.skip_build:
        configure_and_build(args.platform, build_dir, args.configuration)
    stage = package_distribution(args.platform, build_dir, args.configuration, output_dir, dotnet_root, version)

    if args.platform == "windows":
        if args.require_installer or args.iscc:
            installer = build_installer(stage, output_dir, version, find_iscc(args.iscc))
            print(f"Windows installer: {installer}")

    validate_outputs(args.platform, output_dir, version, args.require_installer)
    print(f"Vespera Engine {version} release assembly: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
