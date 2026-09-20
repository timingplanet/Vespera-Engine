#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]

EXCLUDED_NAMES = {
    ".git", ".vs", ".idea", ".vscode", ".qa-hotfix-backups", ".vespera", "__pycache__",
    "build", "build-tests", "out",
}
EXCLUDED_FILES = {
    "HANDOFF.md", "LICENSE-NOTE.txt", "desktop.ini", "Thumbs.db",
}
EXCLUDED_SUFFIXES = {".pyc", ".zip", ".log", ".tmp", ".bak", ".orig"}


def ignored(directory: str, names: list[str]) -> set[str]:
    result: set[str] = set()
    at_root = Path(directory).resolve() == ROOT.resolve()
    for name in names:
        if name in EXCLUDED_NAMES or name in EXCLUDED_FILES:
            result.add(name)
            continue
        if at_root and name.upper().endswith("-HANDOFF.MD"):
            result.add(name)
            continue
        if at_root and (ROOT / name).is_dir() and (name.startswith("build") or name.startswith("release-") or name.startswith("dist-")):
            result.add(name)
            continue
        source_directory = Path(directory)
        candidate = source_directory / name
        if name in {"bin", "obj"} and candidate.is_dir() and any(source_directory.glob("*.csproj")):
            result.add(name)
            continue
        if Path(name).suffix.lower() in EXCLUDED_SUFFIXES:
            result.add(name)
    return result


def paths_overlap(a: Path, b: Path) -> bool:
    a = a.resolve(); b = b.resolve()
    return a == b or a in b.parents or b in a.parents


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a clean public Vespera source tree")
    parser.add_argument("destination", type=Path, help="destination directory (must not contain valuable files)")
    parser.add_argument("--overwrite", action="store_true", help="remove an existing destination first")
    args = parser.parse_args()

    destination = args.destination.resolve()
    if paths_overlap(destination, ROOT):
        raise SystemExit("Destination must not overlap the source tree or any of its parent directories.")

    if destination.exists():
        if not args.overwrite:
            raise SystemExit(f"Destination already exists: {destination} (use --overwrite)")
        shutil.rmtree(destination)

    shutil.copytree(ROOT, destination, ignore=ignored)

    validator = destination / "tools" / "validate_public_release.py"
    result = subprocess.run(
        [sys.executable, str(validator)],
        cwd=destination,
        text=True,
        capture_output=True,
    )
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if result.returncode != 0:
        print(f"Staged public tree retained for inspection: {destination}", file=sys.stderr)
        raise SystemExit(result.returncode)

    print(f"Public source tree ready: {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
