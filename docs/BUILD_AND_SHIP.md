# Build & Ship

For normal game development, build from the editor with **Build → Build Game…** (`Ctrl+Shift+B`).

The build flow saves project state, validates required content, builds project C# when configured, builds/uses the required native runtime target, computes the package closure, and writes a standalone folder.

## Build configurations

Use the configuration that matches what you are doing:

- **Debug** — development/debugging build with debug-oriented native configuration.
- **Development** — optimized development build (`RelWithDebInfo` native configuration) with project diagnostics when enabled.
- **Release** — shipping configuration.

The Build menu also exposes direct Development, Build & Run Development, and Release actions.

## Project packaging settings

Open **Project Settings → Build & Package** to configure:

- Company
- Product Version
- Package Name
- Executable Name
- Build Output Directory
- Managed Deployment

### Framework-dependent .NET

The package expects a compatible .NET runtime to exist on the target machine.

### Portable (.NET bundled)

Vespera copies a private compatible .NET runtime beside the game so the package does not depend on a system-wide .NET installation.

Use Portable when you want the most self-contained C# deployment.

## What gets packaged

Vespera starts from project startup assets and known dependencies. It also includes explicit build roots you configure for content loaded dynamically from code.

The package contains the runtime executable renamed to the project's executable name, required project content, managed output when used, deployment/runtime support files, branding/legal support files, and generated package reports.

## Test the package

Do not stop at “the build completed.” Launch the executable from the packaged folder and test:

- startup scene;
- startup UI;
- C# and/or Lua startup;
- input;
- scene changes;
- sounds and dynamically loaded assets;
- saves;
- clean exit.

A package problem is easier to diagnose while the project context is still fresh.

## Source-tree CLI export

For automation or source-checkout workflows, `export.ps1` exposes the same packaging path. See **Exporting Reference** for parameters and examples.
