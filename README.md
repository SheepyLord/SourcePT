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
* Entropy: Zero 2
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

### Workshop RTX content (map remasters, character PBR)
Some Workshop items carry RTX Remix mods as a [GMod native loader content package](https://github.com/SheepyLord/Gmod-Workshop-RTX-Remix/blob/main/docs/PACKAGE_FORMAT.md):
a `.gma` with `native/manifest.json` and the files under `native/root/`, such as the
[Backrooms Complex](https://steamcommunity.com/sharedfiles/filedetails/?id=3836505482) and
[Pitfalls](https://steamcommunity.com/sharedfiles/filedetails/?id=3836505733) remaster parts. GMod cannot mount
these, so the proxy unpacks the ones you subscribed to when the game starts: their Remix mods into `rtx-remix/mods`
and their Lua add-on folders, if any, into `garrysmod/addons`.
- Only subscribed items count, less the ones turned off in the game's add-on list; packages with native code
  (the RTX package's runtime) are left to the native loader.
- Every file is checked against the package's manifest (size and SHA-256) before it is put in place.
- After an item updates, its files are brought up to date at the next start; after you unsubscribe, they are
  removed. A file you changed, and a folder the proxy did not create, are left as they are.
- The first start after a new or updated item waits while it unpacks (a few seconds per GB on an SSD). What was
  placed is recorded in `bin/win64/shaderapi_proxy_workshop.txt`, and each step is logged in
  `shaderapi_proxy_diag.log`. `OPENREMIX_PROXY_WORKSHOP=0` turns it off.
- A Workshop map itself still has to be mounted by the game: turn on RTXLauncher's *Load Workshop Addons*.

## Credits
* Jane for helping me make the logo.
* Xenthio for ToPBR, Foward+, and general guidance
