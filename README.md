> This project uses AI-generated code

<p align="center">
  <img src="https://github.com/user-attachments/assets/ecee9efd-d865-494c-96e4-f0b4ed3c01d9" width="500" />
</p>

> proper logo someday

This is currently a proxy `shaderapidx9.dll` for Garry's Mod that wraps the
Source shader API (and later D3D9) to submit the game's rendering to
[OpenRemix](https://github.com/OpenRenderCollective/OpenRemix) through the Remix C API. 

This removes the need to force the engine to run in the fixed function pipeline, so all shader-based systems are supported and are translated when applicable. OpenRemix has its own rasterization pass for sprites and other effects that would not work well in path-tracing and can be integrated seamlessly.

RTX Remix mods from the old version of the project will be supported, and also any lua stools like the Remix API lights, etc.

Multiplayer works out of the box since everything lives in the wrapper.

Other source game support is planned

## Installation
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

## Build
This is primarily built on Linux, Windows support is largely untested.

    meson setup --cross-file cross/x86_64-w64-mingw32.txt _build
    meson compile -C _build

Deploy: rename `bin/win64/shaderapidx9.dll` to `shaderapidx9_orig.dll` and copy
`_build/shaderapidx9.dll` in its place.

The same tree builds the proxy for Left 4 Dead 2 (x86, work in progress: the
runtime is reached through the bridge, `remix_bridge.dll` beside the proxy in
`bin`), with the 32-bit cross file:

    meson setup --cross-file cross/i686-w64-mingw32.txt _build-l4d2
    meson compile -C _build-l4d2

The game follows the cross file (`-Dgame=gmod|l4d2` names it; a mismatch with
the cross file is an error). What differs between the games is in
`src/game.h`; the GMod-only engine modules are left out of the L4D2 build
(`src/gmod_only_stubs.cpp`).
