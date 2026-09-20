#!/usr/bin/env python3
from __future__ import annotations

import argparse
from html.parser import HTMLParser
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REQUIRED = [
    ROOT / "LICENSE",
    ROOT / "THIRD_PARTY_NOTICES.md",
    ROOT / "CONTRIBUTING.md",
    ROOT / "README.md",
    ROOT / "test-rc.ps1",
    ROOT / "docs" / "LIMITATIONS.md",
    ROOT / "website" / "index.html",
    ROOT / "website" / "getting-started.html",
    ROOT / "website" / "build-and-ship.html",
    ROOT / "templates" / "2d" / "START-HERE.md",
    ROOT / "templates" / "2d" / "managed" / "StarterGame.cs",
]

FORBIDDEN_RELATIVE = {
    "HANDOFF.md",
    "LICENSE-NOTE.txt",
    ".qa-hotfix-backups",
    "build",
    "build-tests",
    "out",
    ".vs",
    ".idea",
    ".vscode",
}

TEXT_SUFFIXES = {
    ".md", ".txt", ".py", ".ps1", ".cpp", ".hpp", ".h", ".c", ".cs",
    ".cmake", ".json", ".xml", ".html", ".js", ".css", ".rml", ".rcss", ".slscene", ".slprefab",
    ".slui", ".vesperaproject", ".vmeta", ".yml", ".yaml", ".in",
}

PERSONAL_PATTERNS = [
    re.compile(r"C:\\Users\\[^\\\s]+", re.I),
    re.compile(r"/Users/[^/\s]+", re.I),
    re.compile(r"/home/[^/\s]+", re.I),
    re.compile(r"\\Downloads\\", re.I),
]

PS_LITERAL_RE = re.compile(r"[\"']([^\"']+\.ps1)[\"']", re.I)


def iter_text_files():
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(part in {".git", "build", "build-tests", "out", "__pycache__"} for part in path.parts):
            continue
        if path.suffix.lower() in TEXT_SUFFIXES or path.name in {"CMakeLists.txt", ".gitignore"}:
            yield path


def check_required(failures: list[str]) -> None:
    for path in REQUIRED:
        if not path.is_file():
            failures.append(f"missing required public file: {path.relative_to(ROOT)}")


def check_forbidden(failures: list[str]) -> None:
    for rel in FORBIDDEN_RELATIVE:
        if (ROOT / rel).exists():
            failures.append(f"forbidden public artifact present: {rel}")
    for child in ROOT.iterdir():
        if child.is_dir() and (child.name.startswith("build") or child.name.startswith("release-") or child.name.startswith("dist-")):
            failures.append(f"generated top-level directory present: {child.name}")
    for path in ROOT.rglob("*"):
        if path.is_dir() and path.name == "__pycache__":
            failures.append(f"python cache present: {path.relative_to(ROOT)}")
        if path.is_dir() and path.name == ".vespera":
            failures.append(f"project-local generated cache present: {path.relative_to(ROOT)}")
        if path.is_dir() and path.name in {"bin", "obj"} and any(path.parent.glob("*.csproj")):
            failures.append(f"managed build intermediate present: {path.relative_to(ROOT)}")
        if path.is_file() and path.suffix.lower() in {".pyc", ".zip", ".log", ".tmp", ".bak", ".orig"}:
            failures.append(f"generated/private artifact present: {path.relative_to(ROOT)}")


def check_personal_data(failures: list[str]) -> None:
    for path in iter_text_files():
        if path.resolve() == Path(__file__).resolve():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for pattern in PERSONAL_PATTERNS:
            match = pattern.search(text)
            if match:
                failures.append(
                    f"machine/personal path marker in {path.relative_to(ROOT)}: {match.group(0)!r}"
                )
                break


def resolve_script_literal(script: Path, literal: str) -> Path | None:
    if "$" in literal or "*" in literal or "?" in literal:
        return None
    normalized = literal.replace("\\", "/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    candidates = [ROOT / normalized, script.parent / normalized]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def check_powershell_dependencies(failures: list[str]) -> None:
    scripts = list(ROOT.glob("*.ps1")) + list((ROOT / "tools").glob("*.ps1"))
    for script in scripts:
        text = script.read_text(encoding="utf-8")
        for literal in sorted(set(PS_LITERAL_RE.findall(text))):
            resolved = resolve_script_literal(script, literal)
            if resolved is not None and not resolved.exists():
                failures.append(
                    f"PowerShell dependency missing: {script.relative_to(ROOT)} -> {literal}"
                )


def check_release_content(failures: list[str], expected_version: str) -> None:
    license_text = (ROOT / "LICENSE").read_text(encoding="utf-8") if (ROOT / "LICENSE").exists() else ""
    readme = (ROOT / "README.md").read_text(encoding="utf-8") if (ROOT / "README.md").exists() else ""
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8") if (ROOT / "CMakeLists.txt").exists() else ""
    mcp = (ROOT / "tools" / "vespera_mcp_server.py").read_text(encoding="utf-8") if (ROOT / "tools" / "vespera_mcp_server.py").exists() else ""
    limits = (ROOT / "docs" / "LIMITATIONS.md").read_text(encoding="utf-8") if (ROOT / "docs" / "LIMITATIONS.md").exists() else ""
    hub = (ROOT / "examples" / "project_hub" / "ui" / "project_hub.rml").read_text(encoding="utf-8")
    starter = (ROOT / "templates" / "2d" / "managed" / "StarterGame.cs").read_text(encoding="utf-8")
    package_source = (ROOT / "engine" / "src" / "assets" / "project_package.cpp").read_text(encoding="utf-8")
    player_cmake = (ROOT / "runtime" / "player" / "CMakeLists.txt").read_text(encoding="utf-8")

    if "MIT License" not in license_text or "Timingplanet" not in license_text:
        failures.append("LICENSE is not the expected MIT grant for Timingplanet")
    if f'set(VESPERA_VERSION_LABEL "{expected_version}")' not in cmake or f'SERVER_VERSION = "{expected_version}"' not in mcp:
        failures.append(f"public release version surfaces are not stamped {expected_version}")
    if "-" not in expected_version and re.search(r"1\.1\.0-(?:alpha|beta|rc)\.", readme, re.I):
        failures.append("README still advertises a prerelease version")
    if "Windows x64" not in readme:
        failures.append("README is missing the supported Windows x64 platform")
    if "2D / UI Foundation (Experimental)" not in limits:
        failures.append("limitations doc does not state the experimental 2D product scope")
    if "2D / UI FOUNDATION" not in hub.upper():
        failures.append("Project Hub does not visibly label the 2D / UI foundation")
    if 'Input.Value("move_horizontal")' not in starter or 'SetProperty("left"' not in starter:
        failures.append("2D starter is missing its playable screen-space C#/RmlUi foundation")
    if "Vespera.LICENSE.txt" not in package_source or "Vespera.ThirdPartyNotices.md" not in package_source:
        failures.append("shared-player packages do not carry Vespera legal notices")
    if "Copying Vespera branding and legal notices" not in player_cmake:
        failures.append("shared player build does not stage public legal notices")



class _LinkCollector(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, _tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for key, value in attrs:
            if key in {"href", "src"} and value:
                self.links.append(value)


def check_website_links(failures: list[str]) -> None:
    website = ROOT / "website"
    for page in website.glob("*.html"):
        parser = _LinkCollector()
        parser.feed(page.read_text(encoding="utf-8"))
        for link in parser.links:
            if link.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target_text = link.split("#", 1)[0].split("?", 1)[0]
            if not target_text:
                continue
            target = (page.parent / target_text).resolve()
            if not target.exists():
                failures.append(f"broken documentation-site link in {page.relative_to(ROOT)}: {link}")



def check_markdown_links(failures: list[str]) -> None:
    public_docs = [
        ROOT / "README.md",
    ROOT / "test-rc.ps1",
        ROOT / "CONTRIBUTING.md",
        ROOT / "docs" / "GETTING_STARTED.md",
        ROOT / "docs" / "FIRST_GAME.md",
        ROOT / "docs" / "BUILD_AND_SHIP.md",
        ROOT / "docs" / "LIMITATIONS.md",
    ]
    link_re = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
    for path in public_docs:
        if not path.is_file():
            continue
        for link in link_re.findall(path.read_text(encoding="utf-8")):
            target_text = link.split("#", 1)[0].strip()
            if not target_text or "://" in target_text or target_text.startswith("mailto:"):
                continue
            target = (path.parent / target_text).resolve()
            if not target.exists():
                failures.append(f"broken Markdown link in {path.relative_to(ROOT)}: {link}")


def source_version_label() -> str:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(VESPERA_VERSION_LABEL\s+"([^"]+)"\)', cmake)
    if not match:
        raise SystemExit("Could not read VESPERA_VERSION_LABEL from CMakeLists.txt")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a cleaned Vespera public release tree")
    parser.add_argument("--expect-version", help="Exact public version; defaults to VESPERA_VERSION_LABEL")
    args = parser.parse_args()
    expected_version = args.expect_version or source_version_label()

    failures: list[str] = []
    check_required(failures)
    check_forbidden(failures)
    check_personal_data(failures)
    check_powershell_dependencies(failures)
    check_release_content(failures, expected_version)
    check_website_links(failures)
    check_markdown_links(failures)

    if failures:
        print("Public release validation failed:")
        for failure in sorted(dict.fromkeys(failures)):
            print(f" - {failure}")
        return 1

    print("Vespera public release validation passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
