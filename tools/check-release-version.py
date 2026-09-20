#!/usr/bin/env python3
"""Verify that a release tag/expected label matches Vespera's source version."""
from __future__ import annotations

import argparse
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]


def source_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(VESPERA_VERSION_LABEL\s+"([^"]+)"\)', text)
    if not match:
        raise SystemExit("Could not read VESPERA_VERSION_LABEL from CMakeLists.txt")
    return match.group(1)


def normalize_tag(tag: str) -> str:
    value = tag.strip()
    if value.startswith("refs/tags/"):
        value = value[len("refs/tags/"):]
    if value.startswith("v"):
        value = value[1:]
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--tag", help="Git/release tag, for example v1.1.0")
    group.add_argument("--expected", help="Exact expected version label")
    args = parser.parse_args()

    expected = normalize_tag(args.tag) if args.tag is not None else args.expected.strip()
    actual = source_version()
    if expected != actual:
        raise SystemExit(
            f"Release version mismatch: source is '{actual}' but release expects '{expected}'. "
            "Promote VESPERA_VERSION_LABEL before publishing this release."
        )
    print(f"Vespera release version check: PASS ({actual})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
