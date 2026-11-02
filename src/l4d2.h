#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

// Left 4 Dead 2 (x86): the engine side the proxy needs, done in process the
// way the GMod build does it with its engine_* modules (that was the job of
// l4d2-rtx's client mod in "OpenRemix mode").
//
//  * l4d2_engine.cpp: install (lazily, from Present, until engine.dll,
//    client.dll and materialsystem.dll are loaded), the world-lists and
//    overlay markers, the map that is loaded (polled), cvar forcing, the
//    engine-side static prop skip.
//  * l4d2_cull.cpp: anti-culling code patches in engine.dll and client.dll.
//  * l4d2_lights.cpp: the survivors' flashlights as Remix lights.
//  * l4d2_ui.cpp: the runtime's own UI on F12 (remix_ui).
//
// Every code address is found by a byte pattern (first match in the module's
// code, as l4d2-rtx did) and checked against the bytes it expects before it is
// patched; what does not match is logged (shaderapi_proxy_diag.log) and left
// out, the statistics file says what is in place.
struct remixapi_Interface;

namespace proxy::l4d2 {

// A module loaded in this process (PE32).
struct Module {
  const char *name = "";
  uintptr_t base = 0, size = 0;
  uintptr_t codeBegin = 0, codeEnd = 0; // RVAs of the first executable section
  bool open(const char *module);
  bool has(uintptr_t rva, size_t n) const { return base && rva <= size && n <= size - rva; }
  template <typename T> T *at(uintptr_t rva) const { return reinterpret_cast<T *>(base + rva); }
  bool holds(const void *p) const {
    const auto a = reinterpret_cast<uintptr_t>(p);
    return base && a >= base && a - base < size;
  }
  bool inCode(const void *p) const {
    const auto a = reinterpret_cast<uintptr_t>(p);
    return base && a >= base + codeBegin && a < base + codeEnd;
  }
};

// A byte pattern ("8B 50 ? 8B CB", ? any byte) found in a module's code.
struct Pattern {
  const char *name;
  const char *bytes;
  uintptr_t staticRva; // where it is in the build this was written for
  uintptr_t rva = 0;   // first match (0: none)
  unsigned count = 0;  // matches in the code section
  // Scans the module's code; true when found.
  bool find(const Module &module);
};

// Whether `bytes` (as Pattern::bytes) are at module+rva.
bool bytesAt(const Module &module, uintptr_t rva, const char *bytes);
// The vtable of the class with the MSVC RTTI type name `typeName`
// (".?AVCOverlayMgr@@"), complete object at offset 0; null if none.
void **rttiVtable(const Module &module, const char *typeName);
void *createInterface(const char *module, const char *name);
// One pointer-sized vtable write; returns the previous entry (null: failed).
void *swapSlot(void **slot, void *hook);
// Rewrites code (made writable and flushed).
bool writeCode(void *at, const void *bytes, size_t n);
// A relative jump (E9) at `at` to `target`, `n` >= 5 bytes (the rest nops).
bool writeJump(void *at, const void *target, size_t n);

// Environment: OPENREMIX_PROXY_<name>; `fallback` when unset.
bool envFlag(const char *name, bool fallback);
float envFloat(const char *name, float fallback);

// "openremix:..." markers through the calling thread's material system render
// context (queued with the draws under mat_queue_mode 2).
void marker(const char *text);
// client.dll's current view id (VIEW_MAIN 0, VIEW_3DSKY 1, VIEW_MONITOR 2,
// ...), -1 when unknown.
constexpr int kViewMain = 0, kView3dSky = 1, kViewMonitor = 2;
int viewId();
// The game window (remix_ui::install, else found by class) and whether the
// calling thread is the one that owns it: the engine's main thread.
void setGameWindow(HWND window);
bool onMainThread();

// l4d2_cull.cpp: installs the anti-culling patches that match (engine.dll
// and client.dll loaded); once.
void installCull(const Module &engine, const Module &client);
void dumpCull(FILE *out);

// l4d2_lights.cpp. Main thread, once per frame (the main view's
// DrawWorldLists): reads the players' flashlights. Render thread, while the
// frame's lights are drawn: creates, updates and draws their Remix lights.
bool installLights();
void sampleLights();
void submitLights(const remixapi_Interface &api);
void dumpLights(FILE *out);

// Everything above, for the statistics file.
void dumpStatistics(FILE *out);

} // namespace proxy::l4d2
