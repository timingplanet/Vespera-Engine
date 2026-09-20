# Exporting Reference

The editor's **Build Game** window is the normal path. The source tree also includes `export.ps1` for automation, CI-like checks, and command-line packaging.

## Basic export

```powershell
.\export.ps1 -Project C:\Games\MyGame\MyGame.vesperaproject
```

If the project does not declare a custom native game target, export uses the shared `vespera_player` runtime.

## Configurations

The export script maps game configurations to native build configurations:

| Game configuration | Native configuration |
|---|---|
| Debug | Debug |
| Development | RelWithDebInfo |
| Release | Release |

Development can include debug symbols when project diagnostics are enabled.

## Managed deployment

By default, export follows the project's `managed_deployment` setting. The project can choose framework-dependent or portable deployment.

The export path builds the configured managed project into a project-specific staging directory before packaging, protecting against accidentally reusing a stale assembly from a different project.

## Output folder

If no output is supplied, Vespera uses the project's configured build output directory and names the package from the project and configuration.

The package includes:

```text
Vespera.PackageManifest.txt
Vespera.PackageReport.txt
```

Use these when investigating what was included or why a dependency was omitted.

## Launch after export

The script can launch a successful package when requested. The runtime is started with the package folder as its working directory.

## Avoid `-NoBuild` until you understand the state

Skipping build work is useful for controlled automation when you know the expected native and managed outputs already exist. For ordinary shipping, let Vespera build the required targets so you do not package stale binaries.
