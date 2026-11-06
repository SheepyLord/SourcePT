> This project uses AI-generated code

<p align="left">
  <img src="https://github.com/user-attachments/assets/4dc256a6-8795-4c1b-885c-addf341d66eb" width="150" />
</p>


This is currently a proxy `shaderapidx9.dll` for a few Source Engine games that wraps the
Source shader API (and later D3D9) to submit the game's rendering to
[OpenRemix](https://github.com/OpenRenderCollective/OpenRemix) through the Remix C API. 

This removes the need to force the engine to run in the fixed function pipeline, so all shader-based systems are supported and are translated when applicable. OpenRemix has its own rasterization pass for sprites and other effects that would not work well in path-tracing and can be integrated seamlessly.

RTX Remix mods from the old version of the project will be supported, and also any lua stools like the Remix API lights, etc for Garry's Mod.

Multiplayer works out of the box since everything lives in the wrapper.

### Games the wrapper works with
* Garry's Mod **(CI only builds for this one)**
* Team Fortress 2 (64-bit, `tf_win64.exe`)
* Left 4 Dead 2
* Half-Life 2 RTX
* Portal with RTX
* Portal 2

## Garry's Mod Installation Instructions
- Ensure you're on the `main` branch of Garry's Mod on Steam (not `x86-64`)
- Run the vanilla game at least once, especially if you switched branches.
- Download [RTXLauncher](https://github.com/Xenthio/RTXLauncher/releases) (Use the newer nightly if available).
- **Windows**
   - Run the launcher (**as an Administrator**)
- **Linux**
   - Ensure Steam Play is forced for Garry's Mod with either Proton Experimental or another recent Proton build.
   - Run [GModPatchTool](https://github.com/solsticegamestudios/GModPatchTool)
   - Make sure it's marked as executable `chmod +x ./RTXLauncher.Avalonia.Linux`, and run it.
- Select `Start Quick Install` on the main screen and follow the prompts when asked.
- Once it's finished, press `Launch Game` at the bottom of the launcher.

## Team Fortress 2 Installation Instructions
The proxy runs in the 64-bit game (`tf_win64.exe`, `bin/x64`) with OpenRemix in process, as in Garry's Mod.
- Build it with `-Dgame=tf2` (see Build) and get an OpenRemix runtime
  ([nightly](https://github.com/OpenRenderCollective/OpenRemix/releases/tag/nightly), `openremix-windows-x64.zip`).
- In `Team Fortress 2/bin/x64`: rename `shaderapidx9.dll` to `shaderapidx9_orig.dll`, copy the built
  `shaderapidx9.dll` there, and extract the runtime zip into `bin/x64/openremix/` (so `bin/x64/openremix/remix.dll`).
- The proxy adds `-insecure` (it hooks the engine: stay off VAC-secured servers) and `-nod3d9ex` itself. Play on a
  local server or an insecure one.
- The runtime starts in its raster backend: choose path tracing in OpenRemix's panel (Alt+X), or start the game
  with the environment variable `OPENREMIX_PROXY_BACKEND=pathtrace`.
- Recommended video settings, as for Garry's Mod: `mat_hdr_level 0`, `mat_antialias 1`, `mat_motion_blur_enabled 0`,
  `r_shadowrendertotexture 0`, `mat_colorcorrection 0`.
- A game update or Steam's file verification puts the original `shaderapidx9.dll` back (the proxy is then simply
  gone); install the proxy again, renaming the new original to `shaderapidx9_orig.dll`.

## Build
This is primarily built on Linux, Windows support is largely untested (the Team Fortress 2 build was made on Windows
with MSYS2's MinGW-w64 GCC as a native toolchain).

    meson setup --cross-file cross/x86_64-w64-mingw32.txt _build
    meson compile -C _build

Deploy: rename `bin/win64/shaderapidx9.dll` to `shaderapidx9_orig.dll` and copy
`_build/shaderapidx9.dll` in its place.

Team Fortress 2: the same cross file with `-Dgame=tf2` (the TF2 section above for the install).

    meson setup --cross-file cross/x86_64-w64-mingw32.txt -Dgame=tf2 -Dopenremix_include=<openremix>/public/include _build-tf2
    meson compile -C _build-tf2

## Credits
* Jane for helping me make the logo.
* Xenthio for ToPBR, Foward+, and general guidance
