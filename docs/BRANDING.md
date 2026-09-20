# Vespera Engine branding

The shared Vespera application/startup assets live in root `branding/` and are copied to the runtime packages that need them.

- `vespera_icon.png` — high-resolution filled rounded-square V/orbit source artwork.
- `vespera_icon_window.png` — small-window optimized RGBA icon for SDL/Application-hosted windows.
- `vespera_icon_window.bmp` — small-window optimized BMP used by the native editor SDL window-icon path.
- `vespera_icon.ico` — Windows multi-resolution application icon with 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px representations.
- `vespera_splash.png` — Vespera Engine startup/project-loading artwork.
- `vespera_logo_sting.wav` — short startup ident sound.

## Startup behavior

The Project Hub shows the Vespera splash and plays the sting while its real startup work runs. Application-hosted runtimes and the native editor keep the startup artwork visible for a **minimum 4 seconds**; real loading time counts toward that window, so a slow startup is not delayed beyond the remaining branding interval. The editor does not replay the sting after a Hub launch. The shared player uses the splash/sting as Vespera defaults and uses `vespera_icon_window.png` when a project does not provide its own icon.

Project-authored game icons override the shared-player fallback. The engine should not permanently force Vespera branding into a commercial game's identity; broader project-level splash/ident replacement/disable controls remain release-polish work unless already exposed by the project/runtime configuration.
