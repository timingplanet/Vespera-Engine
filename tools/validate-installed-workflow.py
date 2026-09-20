#!/usr/bin/env python3
"""Smoke the public workflow from an assembled Vespera installation.

This intentionally uses only files inside the staged/installed distribution:
Hub template creation -> managed editor build -> standalone Build Game export.
It is suitable for Windows portable/installer and Linux portable validation.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys

SOURCE_ROOT = pathlib.Path(__file__).resolve().parents[1]


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def require_file(path: pathlib.Path, label: str) -> pathlib.Path:
    if not path.is_file():
        fail(f"Missing {label}: {path}")
    return path


def paths_overlap(a: pathlib.Path, b: pathlib.Path) -> bool:
    a = a.resolve(); b = b.resolve()
    return a == b or a in b.parents or b in a.parents

def ensure_safe_work_dir(work: pathlib.Path, distribution_root: pathlib.Path) -> None:
    if paths_overlap(work, distribution_root):
        fail(f"Installed-workflow scratch directory must not overlap the distribution: {work}")
    if paths_overlap(work, SOURCE_ROOT):
        fail(f"Installed-workflow scratch directory must not overlap the Vespera source tree: {work}")


def run(command: list[str], *, cwd: pathlib.Path, env: dict[str, str], timeout: int) -> None:
    print("+", " ".join(command), flush=True)
    try:
        completed = subprocess.run(command, cwd=cwd, env=env, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        fail(f"Installed workflow command timed out after {timeout}s: {command[0]}")
    if completed.returncode != 0:
        fail(f"Installed workflow command failed with exit code {completed.returncode}: {command[0]}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, help="Assembled/installed Vespera distribution root")
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--work-dir", help="Scratch directory; defaults beside the system temp directory")
    parser.add_argument("--configuration", choices=("Debug", "Development", "Release"), default="Development")
    parser.add_argument("--managed-deployment", choices=("framework-dependent", "portable"), default="framework-dependent")
    args = parser.parse_args()

    root = pathlib.Path(args.root).expanduser().resolve()
    if not root.is_dir():
        fail(f"Distribution root does not exist: {root}")

    suffix = ".exe" if args.platform == "windows" else ""
    hub = require_file(root / f"VesperaHub{suffix}", "Vespera Hub")
    player = require_file(root / "runtime" / f"VesperaPlayer{suffix}", "Vespera Player")
    builder = require_file(root / "tools" / f"VesperaBuilder{suffix}", "Vespera Builder")
    dotnet = require_file(root / "dotnet" / ("dotnet.exe" if args.platform == "windows" else "dotnet"), "bundled dotnet")
    sdk_project = require_file(root / "sdk" / "Vespera.NET" / "Vespera.NET.csproj", "Vespera.NET SDK project")
    script_tool = require_file(root / "sdk" / "Vespera.ScriptTool" / "Vespera.ScriptTool.csproj", "Vespera ScriptTool project")

    if args.work_dir:
        work = pathlib.Path(args.work_dir).expanduser().resolve()
    else:
        work = pathlib.Path(os.environ.get("RUNNER_TEMP") or os.environ.get("TMPDIR") or os.environ.get("TEMP") or "/tmp") / "vespera-installed-workflow"
        work = work.resolve()
    ensure_safe_work_dir(work, root)
    if work.exists():
        shutil.rmtree(work)
    project_parent = work / "projects"
    export_dir = work / "export"
    managed_dir = work / "managed"
    result_file = work / "project-result.txt"
    work.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["DOTNET_ROOT"] = str(root / "dotnet")
    env["DOTNET_MULTILEVEL_LOOKUP"] = "0"
    env["DOTNET_NOLOGO"] = "1"
    env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    env["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1"
    env["PATH"] = str(root / "dotnet") + os.pathsep + env.get("PATH", "")

    run([
        str(hub),
        "--create-template=3d",
        "--project-name=InstalledSmoke",
        f"--project-location={project_parent}",
        f"--result={result_file}",
        "--no-launch-editor",
    ], cwd=root, env=env, timeout=60)

    if not result_file.is_file():
        fail("Installed Hub did not write its project result file")
    project_text = result_file.read_text(encoding="utf-8").strip()
    if not project_text:
        fail("Installed Hub wrote an empty project result file")
    project = pathlib.Path(project_text)
    if not project.is_absolute():
        project = (root / project).resolve()
    project = project.resolve()
    require_file(project, "fresh project file")

    common_managed = [
        "--dotnet", str(dotnet),
        "--sdk-project", str(sdk_project),
        "--script-tool-project", str(script_tool),
    ]

    # Mirror the editor's Build C# path before the full export. This verifies
    # that an installed editor has all managed SDK/tool inputs it depends on.
    run([
        str(builder),
        "--project", str(project),
        "--managed-only",
        "--managed-output", str(managed_dir),
        "--configuration", args.configuration,
        *common_managed,
    ], cwd=root, env=env, timeout=900)
    require_file(managed_dir / "VesperaGame.Scripts.dll", "managed game assembly")
    require_file(managed_dir / "Vespera.ScriptMetadata.txt", "managed script metadata")

    run([
        str(builder),
        "--project", str(project),
        "--output", str(export_dir),
        "--runtime", str(player),
        "--configuration", args.configuration,
        "--managed-deployment", args.managed_deployment,
        *common_managed,
    ], cwd=root, env=env, timeout=1200)

    # The shared player export is named after the project created by the Hub.
    # Derive the expected runtime name from the actual .vesperaproject filename
    # instead of relying on the old hard-coded Vespera3DGame name.
    game_name = project.stem
    game = require_file(export_dir / f"{game_name}{suffix}", "exported game")
    require_file(export_dir / "managed" / "VesperaGame.Scripts.dll", "exported managed game assembly")
    packaged_projects = sorted(export_dir.glob("*.vesperaproject"))
    if len(packaged_projects) != 1:
        fail(f"Expected exactly one packaged .vesperaproject, found {len(packaged_projects)} in {export_dir}")
    if args.platform == "linux" and not os.access(game, os.X_OK):
        fail(f"Exported Linux game is not executable: {game}")
    if args.managed_deployment == "portable":
        # A packaged game needs the local hostfxr + Microsoft.NETCore.App runtime
        # payload used by Vespera's native managed host. It does not require the
        # dotnet CLI launcher (dotnet/dotnet[.exe]), which is an SDK/CLI entry
        # point and is intentionally not part of the minimal exported runtime.
        portable_root = export_dir / "dotnet"
        fxr_root = portable_root / "host" / "fxr"
        if not fxr_root.is_dir() or not any(fxr_root.iterdir()):
            fail("Portable export is missing dotnet/host/fxr payload")
        runtime_root = portable_root / "shared" / "Microsoft.NETCore.App"
        if not runtime_root.is_dir() or not any(runtime_root.iterdir()):
            fail("Portable export is missing Microsoft.NETCore.App payload")

    print(f"Installed workflow project: {project}")
    print(f"Installed workflow export: {export_dir}")
    print("Installed Vespera workflow validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
