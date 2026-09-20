#!/usr/bin/env python3
"""Fast cross-platform regression tests for Vespera release assembly tooling."""
from __future__ import annotations

import importlib.util
import pathlib
import re
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[1]

def source_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(VESPERA_VERSION_LABEL\s+"([^"]+)"\)', text)
    if not match:
        raise RuntimeError("Could not read VESPERA_VERSION_LABEL")
    return match.group(1)

VERSION = source_version()
SDK = "10.0.401"
EPOCH = 946684800


def run(*args: str, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    if expect_success and result.returncode != 0:
        raise AssertionError(f"command failed ({result.returncode}): {' '.join(args)}\n{result.stdout}")
    if not expect_success and result.returncode == 0:
        raise AssertionError(f"command unexpectedly succeeded: {' '.join(args)}")
    return result


def load_tool_module(name: str, filename: str):
    tools_dir = ROOT / "tools"
    if str(tools_dir) not in sys.path:
        sys.path.insert(0, str(tools_dir))
    spec = importlib.util.spec_from_file_location(name, tools_dir / filename)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load tool module: {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fake_executable(path: pathlib.Path, identity: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"#!/bin/sh\n[ \"$1\" = \"--version\" ] && echo '{identity} {VERSION}'\nexit 0\n", encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def fake_build(root: pathlib.Path, platform: str) -> None:
    ext = ".exe" if platform == "windows" else ""
    config = pathlib.Path("Release") if platform == "windows" else pathlib.Path()
    entries = {
        pathlib.Path("examples/project_hub") / config / f"vespera_project_hub{ext}": "Vespera Hub",
        pathlib.Path("editor") / config / f"vespera_editor{ext}": "Vespera Editor",
        pathlib.Path("runtime/player") / config / f"vespera_player{ext}": "Vespera Player",
        pathlib.Path("tools/builder") / config / f"vespera_builder{ext}": "Vespera Builder",
        pathlib.Path("tools/packager") / config / f"vespera_packager{ext}": "Vespera Packager",
    }
    for relative, identity in entries.items():
        fake_executable(root / relative, identity)


def fake_dotnet(root: pathlib.Path, platform: str, *, extra_sdk: bool = False) -> None:
    launcher = root / ("dotnet.exe" if platform == "windows" else "dotnet")
    fake_executable(launcher, ".NET SDK")
    for relative in [f"sdk/{SDK}/marker", "host/fxr/10.0.0/marker", "shared/Microsoft.NETCore.App/10.0.0/marker"]:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("test\n", encoding="utf-8")
    if extra_sdk:
        path = root / "sdk/10.0.402/marker"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("unexpected\n", encoding="utf-8")


def verify_windows_archive(path: pathlib.Path) -> None:
    with zipfile.ZipFile(path) as archive:
        files = [entry for entry in archive.infolist() if not entry.is_dir()]
        assert files, "Windows archive is empty"
        assert {entry.date_time for entry in files} == {(2000, 1, 1, 0, 0, 0)}
        assert any(entry.filename.endswith("/VesperaHub.exe") for entry in files)


def verify_linux_archive(path: pathlib.Path) -> None:
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        assert members, "Linux archive is empty"
        assert {member.mtime for member in members} == {EPOCH}
        assert {(m.uid, m.gid, m.uname, m.gname) for m in members} == {(0, 0, "root", "root")}
        assert any(member.name.endswith("/VesperaHub") for member in members)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="vespera-release-tooling-") as temporary:
        temp = pathlib.Path(temporary)
        for platform in ("windows", "linux"):
            build = temp / f"build-{platform}"
            dotnet = temp / f"dotnet-{platform}"
            output = temp / f"release-{platform}"
            fake_build(build, platform)
            fake_dotnet(dotnet, platform)
            run(
                sys.executable, "tools/build-release-assets.py",
                "--platform", platform,
                "--build-dir", str(build),
                "--configuration", "Release",
                "--output-dir", str(output),
                "--dotnet-root", str(dotnet),
                "--skip-build",
            )
            if platform == "windows":
                archive = output / f"VesperaEngine-{VERSION}-Windows-x64-Portable.zip"
                verify_windows_archive(archive)
            else:
                archive = output / f"VesperaEngine-{VERSION}-Linux-x64.tar.gz"
                verify_linux_archive(archive)
            run(
                sys.executable, "tools/validate-release-archive.py",
                "--archive", str(archive), "--platform", platform, "--require-dotnet",
                "--expect-version", VERSION,
            )
            repeat_output = temp / f"release-repeat-{platform}"
            run(sys.executable, "tools/build-release-assets.py", "--platform", platform, "--build-dir", str(build), "--configuration", "Release", "--output-dir", str(repeat_output), "--dotnet-root", str(dotnet), "--skip-build")
            repeat_archive = repeat_output / archive.name
            assert repeat_archive.read_bytes() == archive.read_bytes(), f"{platform} portable archive is not deterministic"
            if platform == "windows":
                root_name = f"VesperaEngine-{VERSION}-Windows-x64"
                tampered = temp / "bad-tampered.zip"
                with zipfile.ZipFile(archive) as source, zipfile.ZipFile(tampered, "w") as destination:
                    for info in source.infolist():
                        if info.is_dir(): continue
                        data = source.read(info.filename)
                        if info.filename == f"{root_name}/README.md": data += b"\nTAMPERED\n"
                        destination.writestr(info, data)
                rejected = run(sys.executable, "tools/validate-release-archive.py", "--archive", str(tampered), "--platform", "windows", "--require-dotnet", "--expect-version", VERSION, expect_success=False)
                assert "Payload SHA-256 mismatch" in rejected.stdout
                unlisted = temp / "bad-unlisted.zip"
                with zipfile.ZipFile(archive) as source, zipfile.ZipFile(unlisted, "w") as destination:
                    for info in source.infolist():
                        if not info.is_dir(): destination.writestr(info, source.read(info.filename))
                    destination.writestr(f"{root_name}/unexpected.txt", "junk\n")
                rejected = run(sys.executable, "tools/validate-release-archive.py", "--archive", str(unlisted), "--platform", "windows", "--require-dotnet", "--expect-version", VERSION, expect_success=False)
                assert "files not listed" in rejected.stdout

        for script in ("tools/build-release-assets.py","tools/check-release-version.py","tools/fetch-dotnet-sdk.py","tools/package-engine.py","tools/prepare-public-release.py","tools/validate-distribution.py","tools/validate-installed-workflow.py","tools/validate-release-archive.py","tools/validate_public_release.py"):
            run(sys.executable, script, "--help")
        probe = run(sys.executable, "-c", "import sys; sys.dont_write_bytecode=True; import tools.validate_public_release as v; print(v.source_version_label())")
        assert probe.stdout.strip() == VERSION
        good_windows = temp / "release-windows" / f"VesperaEngine-{VERSION}-Windows-x64-Portable.zip"
        duplicate_manifest = temp / "bad-duplicate-manifest.zip"
        with zipfile.ZipFile(good_windows) as source, zipfile.ZipFile(duplicate_manifest, "w") as destination:
            manifest_mutated = False
            for info in source.infolist():
                if info.is_dir():
                    continue
                data = source.read(info.filename)
                normalized_name = info.filename.replace("\\", "/")
                if normalized_name.split("/")[-1] == "Vespera.DistributionManifest.txt":
                    manifest_lines = data.decode("utf-8").splitlines()
                    footer_index = manifest_lines.index("end_distribution")
                    manifest_lines.insert(footer_index, f'version "{VERSION}"')
                    data = ("\n".join(manifest_lines) + "\n").encode("utf-8")
                    manifest_mutated = True
                destination.writestr(info, data)
            assert manifest_mutated, "Could not find Vespera.DistributionManifest.txt in Windows test archive"
        rejected=run(sys.executable,"tools/validate-release-archive.py","--archive",str(duplicate_manifest),"--platform","windows",expect_success=False)
        assert "Duplicate distribution manifest key" in rejected.stdout
        root_name=f"VesperaEngine-{VERSION}-Windows-x64"
        case_collision=temp/"bad-case-collision.zip"
        with zipfile.ZipFile(good_windows) as source, zipfile.ZipFile(case_collision,"w") as destination:
            for info in source.infolist():
                if not info.is_dir(): destination.writestr(info,source.read(info.filename))
            destination.writestr(f"{root_name}/readme.md","collision\n")
        rejected=run(sys.executable,"tools/validate-release-archive.py","--archive",str(case_collision),"--platform","windows",expect_success=False)
        assert "case-colliding entries" in rejected.stdout
        validate_release_archive=load_tool_module("vespera_validate_release_archive_test","validate-release-archive.py")
        noncanonical_member=f"{root_name}\\Vespera.DistributionManifest.txt"
        try:
            validate_release_archive.canonical_member_name(noncanonical_member)
        except SystemExit as exc:
            assert "non-canonical path" in str(exc)
        else:
            raise AssertionError("backslash archive member path was accepted")

        build_assets=load_tool_module("vespera_build_release_assets_test","build-release-assets.py")
        fetch_dotnet=load_tool_module("vespera_fetch_dotnet_test","fetch-dotnet-sdk.py")
        prepare_public=load_tool_module("vespera_prepare_public_test","prepare-public-release.py")
        managed_runtime=load_tool_module("vespera_managed_runtime_test","build-managed-runtime.py")
        installed_workflow=load_tool_module("vespera_installed_workflow_test","validate-installed-workflow.py")
        safe_dotnet=temp/"safe-dotnet-root"; safe_dotnet.mkdir()
        build_assets.ensure_safe_release_paths(ROOT/"build-release-test-safe", ROOT/"release-test-safe", safe_dotnet)
        for callable_,arguments in ((build_assets.ensure_safe_release_paths,(ROOT,temp/"release-safe",safe_dotnet)),(build_assets.ensure_safe_release_paths,(temp/"build-safe",ROOT/"tools",safe_dotnet)),(fetch_dotnet.ensure_safe_output,(ROOT,)),(managed_runtime.ensure_safe_stage,(ROOT,ROOT/"examples/reference_game/managed/ReferenceGame.Scripts.csproj",ROOT)),(installed_workflow.ensure_safe_work_dir,(ROOT,temp/"distribution-safe"))):
            try: callable_(*arguments)
            except SystemExit: pass
            else: raise AssertionError(f"unsafe path was accepted by {callable_.__name__}")
        assert prepare_public.paths_overlap(ROOT, ROOT.parent)

        dirty_dotnet = temp / "dotnet-dirty"
        fake_dotnet(dirty_dotnet, "linux", extra_sdk=True)
        rejected = run(
            sys.executable, "tools/package-engine.py",
            "--platform", "linux",
            "--build-dir", str(temp / "build-linux"),
            "--configuration", "Release",
            "--output-dir", str(temp / "release-dirty"),
            "--dotnet-root", str(dirty_dotnet),
            "--expect-dotnet-sdk", SDK,
            expect_success=False,
        )
        assert "unexpected SDKs" in rejected.stdout

        # Archive validators must reject junk outside the single distribution root.
        bad_zip = temp / "bad-extra-root.zip"
        with zipfile.ZipFile(bad_zip, "w") as archive:
            archive.writestr("VesperaEngine-test/Vespera.DistributionManifest.txt", "vespera_distribution 1\nend_distribution\n")
            archive.writestr("unexpected.txt", "junk\n")
        rejected = run(
            sys.executable, "tools/validate-release-archive.py",
            "--archive", str(bad_zip), "--platform", "windows",
            expect_success=False,
        )
        assert "exactly one top-level folder" in rejected.stdout

        # A portable archive with a valid-looking distribution moved under the
        # wrong root name must still be rejected. Users and installer tooling
        # rely on the versioned root directory being deterministic.
        good_windows = temp / "release-windows" / f"VesperaEngine-{VERSION}-Windows-x64-Portable.zip"
        wrong_root = temp / "bad-root-name.zip"
        with zipfile.ZipFile(good_windows) as source, zipfile.ZipFile(wrong_root, "w") as destination:
            expected_prefix = f"VesperaEngine-{VERSION}-Windows-x64/"
            for info in source.infolist():
                if info.is_dir():
                    continue
                assert info.filename.startswith(expected_prefix)
                renamed = "VesperaEngine-wrong-Windows-x64/" + info.filename[len(expected_prefix):]
                destination.writestr(renamed, source.read(info.filename))
        rejected = run(
            sys.executable, "tools/validate-release-archive.py",
            "--archive", str(wrong_root), "--platform", "windows",
            expect_success=False,
        )
        assert "Distribution root name mismatch" in rejected.stdout

        # Installed products may live in an arbitrary directory selected by the
        # installer/user. Keep canonical root-name enforcement for staged/archive
        # distributions, but allow the installed-product validator to opt out.
        installed_parent = temp / "installed-root-name-test"
        with zipfile.ZipFile(good_windows) as archive:
            archive.extractall(installed_parent)
        canonical_installed_root = installed_parent / f"VesperaEngine-{VERSION}-Windows-x64"
        installed_root = installed_parent / "VesperaEngineInstalled"
        canonical_installed_root.rename(installed_root)
        rejected = run(
            sys.executable, "tools/validate-distribution.py",
            "--root", str(installed_root), "--platform", "windows", "--require-dotnet",
            expect_success=False,
        )
        assert "Distribution root name mismatch" in rejected.stdout
        run(
            sys.executable, "tools/validate-distribution.py",
            "--root", str(installed_root), "--platform", "windows", "--require-dotnet",
            "--allow-noncanonical-root-name",
        )

        # Source archives must exclude arbitrary build* directories, not just a
        # hard-coded list of known build folder names.
        sentinel_dir = ROOT / "build-release-sentinel-test"
        sentinel = sentinel_dir / "DO-NOT-PACKAGE.txt"
        handoff_sentinel = ROOT / f"Vespera-{VERSION}-HANDOFF.md"
        managed_project_dir = ROOT / "examples" / "reference_game" / "managed"
        managed_obj = managed_project_dir / "obj"
        managed_bin = managed_project_dir / "bin"
        project_cache = ROOT / "examples" / "reference_game" / ".vespera"
        sentinel_dir.mkdir(exist_ok=True)
        sentinel.write_text("generated\n", encoding="utf-8")
        handoff_sentinel.write_text("local handoff should never enter public staging\n", encoding="utf-8")
        managed_obj.mkdir(parents=True, exist_ok=True)
        managed_bin.mkdir(parents=True, exist_ok=True)
        project_cache.mkdir(parents=True, exist_ok=True)
        machine_path = "C:" + "\\" + "Users" + "\\" + "Example" + "\\" + "Downloads" + "\\" + "vespera"
        (managed_obj / "project.assets.json").write_text(f'{{"source":"{machine_path}"}}', encoding="utf-8")
        (managed_bin / "Generated.dll").write_bytes(b"managed-build-output")
        (project_cache / "managed-cache.txt").write_text("generated\n", encoding="utf-8")
        try:
            public_output = temp / "public-source-test"
            run(sys.executable, "tools/prepare-public-release.py", str(public_output))
            assert not (public_output / "build-release-sentinel-test").exists(), "public source prep leaked generated build directory"
            assert not (public_output / handoff_sentinel.name).exists(), "public source prep leaked a versioned handoff file"
            assert not (public_output / "examples" / "reference_game" / "managed" / "obj").exists(), "public source prep leaked managed obj intermediate"
            assert not (public_output / "examples" / "reference_game" / "managed" / "bin").exists(), "public source prep leaked managed bin output"
            assert not (public_output / "examples" / "reference_game" / ".vespera").exists(), "public source prep leaked project-local generated cache"
            assert (public_output / "build.ps1").is_file(), "public source prep excluded legitimate build.ps1"

            source_output = temp / "source-archive-test"
            run(
                sys.executable, "tools/package-engine.py",
                "--platform", "linux",
                "--build-dir", str(temp / "build-linux"),
                "--configuration", "Release",
                "--output-dir", str(source_output),
                "--skip-dotnet", "--source-archive",
            )
            source_zip = source_output / f"VesperaEngine-{VERSION}-Source.zip"
            with zipfile.ZipFile(source_zip) as archive:
                names = archive.namelist()
                assert not any("build-release-sentinel-test" in name for name in names)
                assert not any("/.vespera/" in name for name in names), "source archive leaked project-local cache"
                assert not any(name.endswith("/" + handoff_sentinel.name) for name in names), "source archive leaked versioned handoff file"
                assert not any("/managed/obj/" in name or "/managed/bin/" in name for name in names), "source archive leaked managed build intermediates"
                assert any(name.endswith("/build.ps1") for name in names), "legitimate build.ps1 was excluded"
                assert any(name.endswith("/test-rc.ps1") for name in names), "RC test runner was excluded"
        finally:
            import shutil
            shutil.rmtree(sentinel_dir, ignore_errors=True)
            shutil.rmtree(managed_obj, ignore_errors=True)
            shutil.rmtree(managed_bin, ignore_errors=True)
            shutil.rmtree(project_cache, ignore_errors=True)
            handoff_sentinel.unlink(missing_ok=True)

    rc_runner = (ROOT / "test-rc.ps1").read_text(encoding="utf-8")
    behavioral_runner = (ROOT / "test.ps1").read_text(encoding="utf-8")
    run_game = (ROOT / "run-game.ps1").read_text(encoding="utf-8")
    managed_helper = (ROOT / "tools/build-managed-editor.ps1").read_text(encoding="utf-8")
    run_project = (ROOT / "run-project.ps1").read_text(encoding="utf-8")
    export_script = (ROOT / "export.ps1").read_text(encoding="utf-8")
    assert "RESULT: PASS" in rc_runner and "RESULT: FAIL" in rc_runner
    assert "Vespera-RC-Logs" in rc_runner and "Full failure transcript" in rc_runner
    assert "tools\\run-rc-gate.ps1" in rc_runner
    assert rc_runner.count("-Configuration Release") >= 4
    assert "& $Body | Out-Host" in rc_runner, "automated RC result can be polluted by success-stream output"
    assert "--target vespera_engine_logic_tests" not in behavioral_runner, "behavioral runner builds only one registered CTest target"
    assert "& $CMake --build $BuildDir --config $Configuration" in behavioral_runner
    assert "ctest-output.log" in behavioral_runner and "Full CTest output:" in behavioral_runner
    assert '[ValidateSet("Debug", "Release", "RelWithDebInfo")]' in run_game
    assert 'build\\examples\\reference_game\\$Configuration' in run_game
    assert '[ValidateSet("Debug", "Release")]' in managed_helper
    assert managed_helper.count('-c $Configuration') >= 2
    assert '-Configuration $ManagedConfiguration' in run_project
    assert '-Configuration $ManagedConfiguration' in export_script
    rc_gate = (ROOT / "tools/run-rc-gate.ps1").read_text(encoding="utf-8")
    assert "RC public source hygiene" in rc_gate and "validate_public_release.py" in rc_gate
    assert "Staged public release hygiene" in rc_gate
    assert "The Vespera Editor will open for automated QA" in rc_gate
    assert "Remove disposable RC QA project" in rc_gate
    assert "Stop Release editor automation" in rc_gate
    assert "Stop-EditorAutomationProcess" in rc_gate
    assert "Remove-DirectoryWithRetry" in rc_gate
    assert rc_gate.index('Invoke-Step "Stop Release editor automation"') < rc_gate.index('Invoke-Step "Remove disposable RC QA project"'), "RC cleanup must stop the editor before deleting managed QA files"
    assert "RC post-QA staged public release hygiene" in rc_gate
    assert "prepare-public-release.py" in rc_gate
    prepare_public_text = (ROOT / "tools" / "prepare-public-release.py").read_text(encoding="utf-8")
    public_validator_text = (ROOT / "tools" / "validate_public_release.py").read_text(encoding="utf-8")
    assert 'name in {"bin", "obj"}' in prepare_public_text and '".vespera"' in prepare_public_text
    assert 'managed build intermediate present' in public_validator_text and 'project-local generated cache present' in public_validator_text
    editor_installation_text = (ROOT / "editor" / "editor_installation.cpp").read_text(encoding="utf-8")
    editor_managed_text = (ROOT / "editor" / "editor_managed.cpp").read_text(encoding="utf-8")
    assert "editor_builder_configuration_fallbacks(configuration)" in editor_installation_text
    assert 'editor_find_builder_executable("Debug")' in editor_managed_text

    print("Vespera release tooling tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
