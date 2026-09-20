#!/usr/bin/env python3
"""Build and stage a Vespera managed game runtime on Windows or Linux.

This is intentionally smaller than the editor hot-reload helper: it performs an
atomic last-good runtime stage suitable for standalone/player launches and CI.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def ensure_safe_stage(root: pathlib.Path, project: pathlib.Path, stage: pathlib.Path) -> None:
    root = root.resolve(); project = project.resolve(); stage = stage.resolve()
    if stage == root or stage in root.parents:
        raise SystemExit(f"Managed stage must not replace the source tree or one of its parents: {stage}")
    if stage == project or stage in project.parents:
        raise SystemExit(f"Managed stage must not replace the managed project or one of its parents: {stage}")


def project_value(project: pathlib.Path, tag: str, default: str = "") -> str:
    root = ET.parse(project).getroot()
    for group in root.findall("PropertyGroup"):
        for child in group:
            if child.tag == tag and child.text and not child.attrib.get("Condition"):
                return child.text.strip()
    # AssemblyName is normally unconditional. TargetFramework in Vespera is
    # conditional so use the first literal net* value when no unconditional one exists.
    for group in root.findall("PropertyGroup"):
        for child in group:
            if child.tag == tag and child.text:
                value = child.text.strip()
                if value and "$(" not in value:
                    return value
    return default


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="examples/reference_game/managed/ReferenceGame.Scripts.csproj")
    parser.add_argument("--stage", default="build-linux/examples/reference_game/managed")
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--framework", default="net8.0")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[1]
    project = (root / args.project).resolve() if not pathlib.Path(args.project).is_absolute() else pathlib.Path(args.project)
    stage = (root / args.stage).resolve() if not pathlib.Path(args.stage).is_absolute() else pathlib.Path(args.stage)
    if not project.is_file():
        raise SystemExit(f"Managed project not found: {project}")
    ensure_safe_stage(root, project, stage)
    if shutil.which("dotnet") is None:
        raise SystemExit("dotnet was not found on PATH")

    assembly_name = project_value(project, "AssemblyName", project.stem)
    framework = args.framework or project_value(project, "TargetFramework", "net8.0")
    runtime_major = framework.removeprefix("net").split(".", 1)[0]
    runtime_version = f"{runtime_major}.0.0"

    with tempfile.TemporaryDirectory(prefix="vespera-managed-") as temp_text:
        temp = pathlib.Path(temp_text)
        command = [
            "dotnet", "build", str(project),
            "-c", args.configuration,
            "-f", framework,
            "-o", str(temp),
            f"-p:VesperaTargetFramework={framework}",
            "--nologo",
        ]
        completed = subprocess.run(command, cwd=root)
        if completed.returncode != 0:
            return completed.returncode

        required = [temp / "Vespera.NET.dll", temp / f"{assembly_name}.dll"]
        missing = [path.name for path in required if not path.is_file()]
        if missing:
            raise SystemExit("Managed build succeeded but required outputs are missing: " + ", ".join(missing))

        runtime_config = {
            "runtimeOptions": {
                "tfm": framework,
                "framework": {"name": "Microsoft.NETCore.App", "version": runtime_version},
                "rollForward": "LatestPatch",
            }
        }
        (temp / "Vespera.Managed.runtimeconfig.json").write_text(
            json.dumps(runtime_config, indent=2) + "\n", encoding="utf-8")

        # Only replace the committed stage after the entire build is known-good.
        pending = stage.with_name(stage.name + ".pending")
        if pending.exists():
            shutil.rmtree(pending)
        shutil.copytree(temp, pending)
        if stage.exists():
            shutil.rmtree(stage)
        pending.replace(stage)

    print(f"Vespera managed runtime staged: {stage}")
    print(f"  Framework: {framework}")
    print(f"  Game assembly: {assembly_name}.dll")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
