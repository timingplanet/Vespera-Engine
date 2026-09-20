#!/usr/bin/env python3
"""Extract and validate a Vespera Engine portable release artifact.

Validating the archive rather than only the staging directory catches packaging
mistakes such as missing files, the wrong root folder, or lost Linux executable
mode bits before a release is published.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import stat
import tarfile
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def canonical_member_name(member: str) -> str:
    if "\x00" in member or "\\" in member:
        raise SystemExit(f"Archive contains a non-canonical path: {member!r}")
    pure = pathlib.PurePosixPath(member)
    if pure.is_absolute() or not pure.parts:
        raise SystemExit(f"Archive contains an unsafe path: {member}")
    if any(part in {"", ".", ".."} for part in pure.parts):
        raise SystemExit(f"Archive contains an unsafe path: {member}")
    if len(pure.parts[0]) == 2 and pure.parts[0][1] == ":" and pure.parts[0][0].isalpha():
        raise SystemExit(f"Archive contains a drive-qualified path: {member}")
    return pure.as_posix()


def safe_member_path(root: pathlib.Path, member: str) -> pathlib.Path:
    normalized = canonical_member_name(member)
    candidate = (root / pathlib.PurePosixPath(normalized)).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError:
        raise SystemExit(f"Archive contains an unsafe path: {member}")
    return candidate


def register_member(member: str, seen: set[str], seen_casefold: dict[str, str]) -> str:
    normalized = canonical_member_name(member)
    if normalized in seen:
        raise SystemExit(f"Portable release archive contains a duplicate entry: {normalized}")
    folded = normalized.casefold()
    previous = seen_casefold.get(folded)
    if previous is not None and previous != normalized:
        raise SystemExit(f"Portable release archive contains case-colliding entries: {previous} and {normalized}")
    seen.add(normalized)
    seen_casefold[folded] = normalized
    return normalized


def _top_level_member(name: str) -> str:
    return canonical_member_name(name).split("/", 1)[0]


def extract_archive(archive: pathlib.Path, destination: pathlib.Path) -> None:
    lower = archive.name.lower()
    roots: set[str] = set()
    seen: set[str] = set()
    seen_casefold: dict[str, str] = {}
    if lower.endswith(".zip"):
        with zipfile.ZipFile(archive) as zf:
            for info in zf.infolist():
                normalized = register_member(info.filename, seen, seen_casefold)
                safe_member_path(destination, normalized)
                root = _top_level_member(normalized)
                if root:
                    roots.add(root)
                unix_mode = (info.external_attr >> 16) & 0xFFFF
                file_type = stat.S_IFMT(unix_mode) if unix_mode else 0
                if file_type and not (stat.S_ISREG(unix_mode) or stat.S_ISDIR(unix_mode)):
                    raise SystemExit(f"Portable release archive contains an unsupported ZIP entry: {info.filename}")
            if len(roots) != 1:
                raise SystemExit(f"Portable release archive must contain exactly one top-level folder, found {sorted(roots)!r}")
            zf.extractall(destination)
        return
    if lower.endswith(".tar.gz") or lower.endswith(".tgz"):
        with tarfile.open(archive, "r:gz") as tf:
            for member in tf.getmembers():
                normalized = register_member(member.name, seen, seen_casefold)
                safe_member_path(destination, normalized)
                root = _top_level_member(normalized)
                if root:
                    roots.add(root)
                if member.issym() or member.islnk():
                    raise SystemExit(f"Portable release archive must not contain links: {member.name}")
                if not (member.isfile() or member.isdir()):
                    raise SystemExit(f"Portable release archive contains an unsupported entry: {member.name}")
            if len(roots) != 1:
                raise SystemExit(f"Portable release archive must contain exactly one top-level folder, found {sorted(roots)!r}")
            # We already reject traversal, links and special entries above. Use
            # fully_trusted so executable mode bits survive extraction exactly as
            # they will for users of the Linux portable archive.
            tf.extractall(destination, filter="fully_trusted")
        return
    raise SystemExit(f"Unsupported release archive: {archive}")


def find_distribution_root(extracted: pathlib.Path) -> pathlib.Path:
    roots = [path.parent for path in extracted.rglob("Vespera.DistributionManifest.txt") if path.is_file()]
    unique = sorted(set(path.resolve() for path in roots))
    if len(unique) != 1:
        raise SystemExit(f"Expected exactly one Vespera distribution root in archive, found {len(unique)}")
    return unique[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--expect-version")
    parser.add_argument("--require-dotnet", action="store_true")
    parser.add_argument("--run-tool-smoke", action="store_true")
    args = parser.parse_args()

    archive = pathlib.Path(args.archive).resolve()
    if not archive.is_file():
        raise SystemExit(f"Release archive does not exist: {archive}")

    with tempfile.TemporaryDirectory(prefix="vespera-release-verify-") as temp:
        extracted = pathlib.Path(temp)
        extract_archive(archive, extracted)
        distribution = find_distribution_root(extracted)
        command = [
            sys.executable,
            str(ROOT / "tools" / "validate-distribution.py"),
            "--root", str(distribution),
            "--platform", args.platform,
            "--reject-unlisted",
        ]
        if args.expect_version:
            command += ["--expect-version", args.expect_version]
        if args.require_dotnet:
            command.append("--require-dotnet")
        if args.run_tool_smoke:
            command.append("--run-tool-smoke")
        completed = subprocess.run(command, cwd=ROOT, timeout=90, check=False)
        if completed.returncode != 0:
            return completed.returncode

    print(f"Vespera release archive validation passed: {archive.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
