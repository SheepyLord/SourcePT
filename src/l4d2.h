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
//  * l4d2_infected.cpp: the common infected's look and wounds (see infected).
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

// l4d2_infected.cpp: the common infected (Source's Infected shader), drawn
// with their gradient-mapped look (remixapi_MaterialGradientMapEXT) and their
// wounds cut out (remixapi_InstanceInfoWoundCutEXT). Their draws are told
// apart by their shaders' constant tables (CTAB, read once per shader
// object): the pixel shader declares g_vGradSelect, g_vUVTransform,
// BaseTextureSampler and ColorLookupSampler; the wounded combination also
// g_tCutOutDecalMap and g_vWoundUVScaleBias1/2, its vertex shader the
// ellipsoids (g_vEllipsoidCenter...). Registers are taken from the tables.
// OPENREMIX_PROXY_INFECTED=0: drawn as any other model. Render thread.
namespace infected {
bool enabled();

// What one draw's shaders and constants say.
struct Params {
  // Samplers (D3D stages) of the base texture, detail (-1: none), gradient,
  // cut (-1: not the wounded combination) and burn (-1: not the burning one).
  int baseStage = 0, detailStage = -1, gradientStage = 5, cutStage = -1, burnStage = -1;
  float skinRow = 0, clothRow = 0, spriteOffset[2] = {}, detailU[2] = {1, 0}, detailV[2] = {0, 1};
  float bloodColor[3] = {1, 1, 1}, bloodRange[2] = {0, 1};
  // Burning: g_vShaderParams.x, 0 to 1 (0: not burning, or no burn sampler).
  // Opacity: g_DiffuseModulation's alpha, which the shader writes to oC0.w
  // (Source fades a model by it), 0 to 1 for an alpha-blended draw (else 1).
  // Both in steps of 1/kQuantum: they name the material, and animate.
  static constexpr float kQuantum = 64;
  float burnStrength = 0, opacity = 1;
  // Pixel shader registers, for the probe (-1: not declared): g_cEyeGlowColor,
  // g_DiffuseModulation, g_vShaderParams.
  int eyeGlowRegister = -1, modulationRegister = -1, shaderParamsRegister = -1;
  bool fadeable = false; // the pixel shader declares g_DiffuseModulation
  // A wound in place: the wounded combination of both shaders, and an
  // ellipsoid near the model (the game parks unused ones at 1e6).
  bool wounded = false;
  bool woundCombo = false; // the pixel shader is the wounded combination
  bool vertexShaderKnown = false; // the vertex shader's constant table names the registers
  float center[2][3] = {}, axisX[2][3] = {}, axisY[2][3] = {}, axisZ[2][3] = {};
  float mappingScale = 1, secondBlend = 0, projectionScale[4] = {}, projectionBias[4] = {};
  float cutScaleBias[2][4] = {}, threshold = 1;
};
// Whether the draw is an infected one (`out` filled). `ps`, `vs`: the
// shadowed float constants (224 / 256 registers); `alphaBlended`: the draw
// blends SRCALPHA, INVSRCALPHA (its opacity is then the modulation's alpha).
bool classify(const void *pixelShader, const void *vertexShader, const float *ps, const float *vs, bool alphaBlended,
              Params &out);

// Statistics.
enum Missing { kMissingBase, kMissingGradient, kMissingDetail, kMissingCut, kMissingBurn, kMissingCount };
void noteDraw(const Params &p, bool material, bool woundSent);
// An infected shader's draw not taken as infected: blended (other than a
// fade), alpha tested, lifted or a particle.
void noteRefused(bool blend, bool alphaTest, bool lifted, bool particle, const Params &p);
void noteMissing(Missing which);
void noteMaterialCreated(bool failed);
void dumpStatistics(FILE *out, size_t materials);

// Dev aid: bin/shaderapi_proxy_infected.request (its contents: a draw count,
// default 32; "wounded [count]"; "refused [count]": the infected shaders'
// draws not taken) writes the next infected draws to bin/infected_probe.txt.
// servicePresent: once per frame (polls the request); probing: draws left.
void servicePresent(const wchar_t *directory, uint64_t frame);
bool probing();
struct ProbeDraw {
  // Base, detail, gradient, cut, burn: their stages (-1: none), textures,
  // runtime texture hashes (0: none), whether the runtime has them, and their
  // samplers' D3DSAMP_SRGBTEXTURE.
  static constexpr int kTextures = 5;
  int stages[kTextures] = {-1, -1, -1, -1, -1};
  const void *textures[kTextures] = {};
  uint64_t hashes[kTextures] = {};
  bool uploaded[kTextures] = {};
  uint32_t srgb[kTextures] = {};
  const void *pixelShader = nullptr;
  uint32_t vertices = 0, primitives = 0;
  uint32_t bonesPerVertex = 0, boneCount = 0; // as the mesh was built (boneCount 1: rigid)
  bool skinnedLayout = false, dynamicVertices = false, flexIgnored = false;
  bool material = false, woundSent = false;
  // The draw's blend and alpha test state.
  bool blend = false, separateAlpha = false, alphaTest = false;
  uint32_t srcBlend = 0, dstBlend = 0, blendOp = 0, srcBlendAlpha = 0, dstBlendAlpha = 0, blendOpAlpha = 0;
  uint32_t alphaFunc = 0, alphaRef = 0;
  // Not taken (probed under "refused" only), and why besides its blend state.
  bool refused = false, lifted = false, particle = false;
  uint64_t frame = 0;
};
void probe(const Params &p, const float *ps, const float *vs, const ProbeDraw &draw);
} // namespace infected

} // namespace proxy::l4d2
