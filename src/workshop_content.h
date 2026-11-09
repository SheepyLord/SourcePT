#pragma once
#include <string>

// Workshop content packages: Remix mods (and the Lua add-ons that go with
// them) that a subscribed Workshop item carries as a GMod native loader
// package, schema 1, content only (Gmod-Workshop-RTX-Remix's
// docs/PACKAGE_FORMAT.md; map PBR remasters ship this way). The item's .gma
// holds native/manifest.json and, under native/root/, the files to place:
// rtx-remix/mods/<mod>/... and garrysmod/addons/<add-on>/.... GMod refuses
// to mount such an archive and OpenRemix reads mods from a directory, so the
// proxy unpacks each file where it is read: the mods into the mods directory
// startRemix configures, the add-ons into garrysmod/addons.
//
// Only items an account on this computer subscribed to count (Steam's
// appworkshop_4000.acf names a subscriber), less the ones switched off in
// GMod's add-on list (garrysmod/cfg/addonnomount.txt). Every file is checked
// against the manifest (size and SHA-256) and the manifest against the
// archive. What the proxy placed is recorded beside it
// (shaderapi_proxy_workshop.txt): a later start brings those files up to date
// with the item and takes them away once the item is unsubscribed. A folder
// the proxy did not create, and a file changed since it was placed, are left
// as they are. OPENREMIX_PROXY_WORKSHOP=0 turns all of it off.
namespace proxy::workshop {

// The first CreateInterface: finds the packages and places their add-on
// folders before it returns, ahead of GMod mounting garrysmod/addons. The
// mods are unpacked on a thread.
void begin();
// startRemix, before the runtime reads the mods directory: waits for that
// thread. A start with nothing new only compares sizes and times.
void finish();

// The same, synchronously, for given folders (offline tests).
struct Options {
  std::wstring gameRoot;  // the folder of gmod.exe
  std::wstring modsDir;   // where the runtime reads mods
  std::wstring statePath; // the record of what was placed
  std::wstring steamapps; // empty: the game's library, else Steam's libraryfolders.vdf
  std::wstring steamRoot; // empty: HKCU\Software\Valve\Steam SteamPath
};
void deploy(const Options &options);

} // namespace proxy::workshop
