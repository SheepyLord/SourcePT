// Left 4 Dead 2: install, markers, map, cvars, static prop skip (see l4d2.h).
// The library loader (remixapi_lib_*) is remix_output's.
#define REMIX_WINAPI_NO_LIBRARY_LOADER
#include "l4d2.h"

#include "bsp_world.h"
#include "diag.h"
#include "engine_hooks.h"
#include "game.h"
#include "remix_output.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace proxy::l4d2 {

// ---- helpers ----

bool Module::open(const char *module) {
  const auto m = reinterpret_cast<uintptr_t>(GetModuleHandleA(module));
  if (!m)
    return false;
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(m);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return false;
  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(m + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    return false;
  const auto *section = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
      codeBegin = section->VirtualAddress;
      codeEnd = section->VirtualAddress + section->Misc.VirtualSize;
      break;
    }
  if (codeEnd <= codeBegin)
    return false;
  name = module;
  base = m;
  size = nt->OptionalHeader.SizeOfImage;
  return true;
}

namespace {
// "8B 50 ? 8B CB" -> bytes and a mask (false: any byte).
size_t parse(const char *text, uint8_t *bytes, bool *mask, size_t capacity) {
  size_t n = 0;
  for (const char *p = text; *p && n < capacity;) {
    while (*p == ' ')
      ++p;
    if (!*p)
      break;
    if (*p == '?') {
      while (*p == '?')
        ++p;
      bytes[n] = 0;
      mask[n++] = false;
      continue;
    }
    char *end;
    const unsigned long value = std::strtoul(p, &end, 16);
    if (end == p)
      return 0;
    bytes[n] = uint8_t(value);
    mask[n++] = true;
    p = end;
  }
  return n;
}
} // namespace

bool Pattern::find(const Module &module) {
  uint8_t bytes[128];
  bool mask[128];
  const size_t n = parse(this->bytes, bytes, mask, sizeof(bytes));
  rva = 0;
  count = 0;
  if (!n || !module.base || module.codeEnd - module.codeBegin < n)
    return false;
  const uint8_t *begin = module.at<uint8_t>(module.codeBegin);
  const uint8_t *last = module.at<uint8_t>(module.codeEnd - n);
  // Anchored on the first exact byte.
  size_t anchor = 0;
  while (anchor < n && !mask[anchor])
    ++anchor;
  for (const uint8_t *p = begin; p <= last;) {
    if (anchor < n) {
      const void *hit = std::memchr(p + anchor, bytes[anchor], size_t(last - p) + 1);
      if (!hit)
        break;
      p = static_cast<const uint8_t *>(hit) - anchor;
    }
    size_t i = 0;
    while (i < n && (!mask[i] || p[i] == bytes[i]))
      ++i;
    if (i == n) {
      if (!count++)
        rva = uintptr_t(p) - module.base;
    }
    ++p;
  }
  return count != 0;
}

bool bytesAt(const Module &module, uintptr_t rva, const char *text) {
  uint8_t bytes[128];
  bool mask[128];
  const size_t n = parse(text, bytes, mask, sizeof(bytes));
  if (!n || !module.has(rva, n))
    return false;
  const uint8_t *p = module.at<uint8_t>(rva);
  for (size_t i = 0; i < n; ++i)
    if (mask[i] && p[i] != bytes[i])
      return false;
  return true;
}

void **rttiVtable(const Module &module, const char *typeName) {
  // TypeDescriptor: {vtable, spare, name[]}; CompleteObjectLocator: {signature
  // 0, offset, cdOffset, TypeDescriptor *, ClassHierarchyDescriptor *}; the
  // vtable's entry -1 points at the locator.
  const size_t length = std::strlen(typeName) + 1;
  const uint8_t *image = module.at<uint8_t>(0);
  for (uintptr_t at = module.codeEnd; at + length <= module.size; ++at) {
    if (image[at] != uint8_t(typeName[0]) || std::memcmp(image + at, typeName, length) != 0)
      continue;
    const uint32_t descriptor = uint32_t(module.base + at - 8);
    for (uintptr_t col = 0; col + 20 <= module.size; col += 4) {
      const auto *locator = reinterpret_cast<const uint32_t *>(image + col);
      if (locator[3] != descriptor || locator[0] != 0 || locator[1] != 0)
        continue;
      const uint32_t colAddress = uint32_t(module.base + col);
      for (uintptr_t slot = 0; slot + 8 <= module.size; slot += 4) {
        const auto *entry = reinterpret_cast<const uint32_t *>(image + slot);
        if (entry[0] == colAddress && module.inCode(reinterpret_cast<void *>(uintptr_t(entry[1]))))
          return reinterpret_cast<void **>(module.base + slot + 4);
      }
    }
    return nullptr;
  }
  return nullptr;
}

void *createInterface(const char *module, const char *name) {
  using CreateInterfaceFn = void *(*)(const char *, int *);
  HMODULE m = GetModuleHandleA(module);
  auto factory = m ? reinterpret_cast<CreateInterfaceFn>(reinterpret_cast<void *>(GetProcAddress(m, "CreateInterface"))) : nullptr;
  return factory ? factory(name, nullptr) : nullptr;
}

void *swapSlot(void **slot, void *hook) {
  DWORD old;
  if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
    return nullptr;
  void *previous = InterlockedExchangePointer(slot, hook);
  VirtualProtect(slot, sizeof(void *), old, &old);
  return previous;
}

bool writeCode(void *at, const void *bytes, size_t n) {
  DWORD old;
  if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old))
    return false;
  std::memcpy(at, bytes, n);
  VirtualProtect(at, n, old, &old);
  FlushInstructionCache(GetCurrentProcess(), at, n);
  return true;
}

bool writeJump(void *at, const void *target, size_t n) {
  uint8_t code[16];
  if (n < 5 || n > sizeof(code))
    return false;
  code[0] = 0xE9;
  const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(at) + 5));
  std::memcpy(code + 1, &rel, 4);
  std::memset(code + 5, 0x90, n - 5);
  return writeCode(at, code, n);
}

bool envFlag(const char *name, bool fallback) {
  char full[96], v[8] = {};
  std::snprintf(full, sizeof(full), "OPENREMIX_PROXY_%s", name);
  if (!GetEnvironmentVariableA(full, v, sizeof(v)))
    return fallback;
  return v[0] != '0';
}

float envFloat(const char *name, float fallback) {
  char full[96], v[32] = {};
  std::snprintf(full, sizeof(full), "OPENREMIX_PROXY_%s", name);
  return GetEnvironmentVariableA(full, v, sizeof(v)) ? float(std::atof(v)) : fallback;
}

namespace {

std::atomic<const char *> g_status{"not installed"};
Module g_engine, g_client, g_materialSystem;

// ---- markers ----

// IMaterialSystem (VMaterialSystem080) slot 96: GetRenderContext (the calling
// thread's context, AddRef'd); IMatRenderContext slot 137: SetPIXMarker
// (color, name), slot 1: Release. The hardware context's SetPIXMarker jumps to
// IShaderAPIDX8 slot 181 (game.h). Verified in L4D2's materialsystem.dll.
void *g_materials = nullptr;
std::atomic<uint64_t> g_markers{0};

// ---- view id ----

// client.dll's current view id (CViewRender's g_CurrentViewID): "mov
// [g_CurrentViewID],edx; add esp,..".
Pattern g_viewIdPattern{"viewid", "89 15 ? ? ? ? 83 C4", 0x1CCFCC};
const int *g_viewId = nullptr;

// ---- the game window, the main thread ----

std::atomic<HWND> g_window{nullptr};
std::atomic<DWORD> g_mainThread{0};

// ---- IVRenderView::DrawWorldLists, COverlayMgr::RenderOverlays ----

struct Hook {
  const char *name;
  std::atomic<const char *> state{"not installed"};
  uintptr_t rva = 0; // the hooked function in its module
};
Hook g_worldListsHook{"DrawWorldLists"}, g_overlaysHook{"RenderOverlays"}, g_modelHook{"DrawModelExecute"};

using DrawWorldListsFn = void(PROXY_THISCALL *)(void *, void *, unsigned long, float);
DrawWorldListsFn g_drawWorldLists = nullptr;
using RenderOverlaysFn = void(PROXY_THISCALL *)(void *, int);
RenderOverlaysFn g_renderOverlays = nullptr;
using DrawModelExecuteFn = void(PROXY_THISCALL *)(void *, const void *, const uint8_t *, void *);
DrawModelExecuteFn g_drawModelExecute = nullptr;

struct Statistics {
  std::atomic<uint64_t> worldLists{0}, worldListsSky{0}, overlays{0}, overlaysSky{0}, propsSkipped{0},
      propsDrawn{0}, mainFrames{0}, cvarPasses{0};
} g_stats;

// ---- per frame ----

std::atomic<uint32_t> g_presents{0};
uint32_t g_lastMainFrame = ~0u; // main thread

// ---- the map ----

// IVEngineClient (VEngineClient013) slot 53: GetLevelName: "maps/x.bsp" while
// connected (signon state >= 2), "" otherwise, "dedicated" on a dedicated
// server (verified in L4D2's engine.dll).
void *g_engineClient = nullptr;
std::mutex g_mapLock;
std::string g_announced, g_candidate; // under g_mapLock
unsigned g_stable = 0;
std::atomic<uint32_t> g_loads{0}, g_unloads{0};

// ---- cvars ----

// VEngineCvar007 (vstdlib.dll) slot 13: ConVar *FindVar(const char *).
// ConVar (L4D2): ConCommandBase {vtable, next, registered, name +0x0C, help,
// flags +0x14}, IConVar at +0x18 (slot 1: SetValue(int), forwards to the
// parent, +0x1C). Verified in engine.dll's ConVar vtable.
void *g_cvars = nullptr;
struct Forced {
  const char *name;
  int value;
  bool set; // false: only made usable (cheat, hidden and development flags cleared)
  void *var = nullptr;
  bool skipped = false; // OPENREMIX_PROXY_CVAR_SKIP
};
std::vector<Forced> g_forced; // main thread
std::atomic<const char *> g_cvarState{"not started"};
std::atomic<unsigned> g_cvarsFound{0}, g_cvarsMissing{0};
std::string g_cvarMissingNames; // main thread; read by the statistics (a snapshot below)
std::mutex g_cvarNamesLock;

} // namespace

// ---- markers, view id, threads ----

void marker(const char *text) {
  void *materials = g_materials;
  if (!materials)
    return;
  using GetRenderContextFn = void *(PROXY_THISCALL *)(void *);
  using SetPIXMarkerFn = void(PROXY_THISCALL *)(void *, unsigned long, const char *);
  using ReleaseFn = int(PROXY_THISCALL *)(void *);
  void *context = reinterpret_cast<GetRenderContextFn>((*static_cast<void ***>(materials))[96])(materials);
  if (!context)
    return;
  void **vtable = *static_cast<void ***>(context);
  reinterpret_cast<SetPIXMarkerFn>(vtable[137])(context, 0x0E5C0DE0, text);
  reinterpret_cast<ReleaseFn>(vtable[1])(context);
  ++g_markers;
}

int viewId() { return g_viewId ? *g_viewId : -1; }

void setGameWindow(HWND window) {
  if (!window)
    return;
  g_window = window;
  g_mainThread = GetWindowThreadProcessId(window, nullptr);
}

bool onMainThread() {
  if (!g_mainThread.load()) {
    // Not handed over (OpenRemix off): the engine's window class.
    HWND window = FindWindowA("Valve001", nullptr);
    DWORD process = 0;
    if (window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId())
      setGameWindow(window);
  }
  return g_mainThread.load() == GetCurrentThreadId();
}

namespace {

// ---- cvars ----

// What l4d2-rtx forced in OpenRemix mode (main_module.cpp force_cvars and the
// map-load list; game_settings defaults lod_forcing off, force_graphic_settings
// on, enable_3d_sky off).
void buildCvarTable() {
  const bool sky = envFlag("3DSKY", false); // 1: r_3dsky stays the game's (default on)
  g_forced = {
      // The proxy takes its signals as markers queued with the draws: the
      // material system renders on its own thread.
      {"mat_queue_mode", 2, true},
      // The proxy skins on the GPU from the game's bone matrices.
      {"mat_softwareskin", 0, true},
      // The proxy keeps D3D9 depth: the sun glow's occlusion query works.
      {"r_dopixelvisibility", 1, true},
      // The path tracer shadows everything; Source's blob and render-to-texture
      // shadows would draw dark quads.
      {"r_shadows", 0, true},
      {"r_shadowrendertotexture", 0, true},
      {"r_shadowfromworldlights", 0, true},
      // The flashlight is a Remix light (l4d2_lights); Source's projected one
      // re-draws lit geometry.
      {"r_flashlightrender", 0, true},
      {"r_flashlightrendermodels", 0, true},
      {"r_flashlightrenderworld", 0, true},
      {"r_FlashlightDetailProps", 0, true},
      // Water views: culling near water surfaces, and their cost.
      {"r_WaterDrawRefraction", 0, true},
      {"r_WaterDrawReflection", 0, true},
      // Anti-culling (l4d2_cull): r_PortalTestEnts 0 makes the client's
      // renderable culling ask CullBox, which the patch skips.
      {"r_threaded_particles", 0, true},
      {"r_entityclips", 0, true},
      {"r_PortalTestEnts", 0, true},
      // Fast paths that bypass the draws the proxy captures (and
      // DrawModelExecute: the static prop skip).
      {"cl_fastdetailsprites", 0, true},
      {"cl_brushfastpath", 0, true},
      {"cl_tlucfastpath", 0, true},
      // Not cl_modelfastpath: common infected's wounds reach the shader only
      // through its instance data (no wound there: the shader API binds
      // ellipsoids at 1e6), and static props still pass DrawModelExecute.
      {"r_queued_ropes", 0, true},
      {"mat_softwarelighting", 0, true},
      {"mat_parallaxmap", 0, true},
      {"mat_frame_sync_enable", 0, true},
      {"mat_dof_enabled", 0, true},
      {"mat_displacementmap", 0, true},
      {"mat_drawflat", 0, true},
      {"mat_normalmaps", 0, true},
      {"mat_fullbright", 1, true},
      {"mat_phong", 1, true},
      {"mat_fastnobump", 1, true},
      {"mat_disable_bloom", 1, true},
      {"r_threadeddetailprops", 0, true},
      {"r_DrawDetailProps", 0, true},
      {"r_DispWalkable", 1, true},
      // force_graphic_settings.
      {"cpu_level", 2, true},
      {"gpu_level", 0, true},
      {"gpu_mem_level", 2, true},
      // Effects.
      {"cl_particle_fallback_base", 0, true},
      {"cl_particle_fallback_multiplier", 1, true},
      {"cl_impacteffects_limit_general", 10, true},
      {"cl_impacteffects_limit_exit", 3, true},
      {"cl_impacteffects_limit_water", 2, true},
      {"mat_depthfeather_enable", 0, true},
      {"mat_force_vertexfog", 1, true},
      {"cl_footstep_fx", 1, true},
      {"cl_ragdoll_self_collision", 1, true},
      {"cl_ragdoll_maxcount", 24, true},
      {"cl_detaildist", 1024, true},
      {"cl_detailfade", 400, true},
      {"r_drawmodeldecals", 1, true},
      {"r_decalstaticprops", 1, true},
      {"cl_player_max_decal_count", 32, true},
      {"r_decals", 2048, true},
      {"r_decal_overlap_count", 3, true},
      // Fog is the runtime's.
      {"fog_override", 1, true},
      {"fog_enable", 0, true},
      // Usable from the console (l4d2-rtx: on map load; lod_forcing off).
      {"r_staticprop_lod", 0, false},
      {"r_lod", 0, false},
      {"r_lod_switch_scale", 0, false},
  };
  g_forced.push_back({"r_3dsky", 0, !sky});
  for (const char *name :
       {"r_propsmaxdist", "cl_detaildist", "cl_detailfade", "cl_footstep_fx", "cl_fov", "cl_impacteffects",
        "cl_interpolate", "cl_particle_batch_mode", "cl_smoke_alpha", "cl_smoke_far", "cl_viewbob",
        "fx_drawimpactdebris", "fx_drawimpactdust", "fx_drawmetalspark", "r_draw_flashlight_3rd_person",
        "r_draw_lasersight_1st_person", "r_draw_lasersight_3rd_person", "r_drawbatchdecals", "r_drawflecks",
        "r_drawunderwaterfogblocker", "r_fade360style", "r_flashlight_3rd_person_range", "r_frustumcullworld",
        "r_impactparticles", "r_maxmodeldecal", "r_occlusion", "r_particle_timescale", "r_queued_decals",
        "r_RainParticleDensity", "r_ropetranslucent", "r_ShowViewerArea", "r_snapportal", "r_staticlight_streams",
        "r_staticpropinfo", "r_teeth", "scene_print"}) {
    bool listed = false;
    for (const Forced &f : g_forced)
      listed |= _stricmp(f.name, name) == 0;
    if (!listed)
      g_forced.push_back({name, 0, false});
  }
  // OPENREMIX_PROXY_CVAR_SKIP="name,name": made usable, not forced.
  char skip[1024] = {};
  if (GetEnvironmentVariableA("OPENREMIX_PROXY_CVAR_SKIP", skip, sizeof(skip))) {
    const std::string list = std::string(",") + skip + ",";
    for (Forced &f : g_forced)
      f.skipped = list.find(std::string(",") + f.name + ",") != std::string::npos;
  }
}

void forceCvars() {
  static const bool enabled = envFlag("CVARS", true);
  if (!enabled) {
    g_cvarState = "off (OPENREMIX_PROXY_CVARS=0)";
    return;
  }
  if (!g_cvars)
    return;
  static bool broken = false;
  if (broken)
    return;
  if (g_forced.empty())
    buildCvarTable();
  using FindVarFn = void *(PROXY_THISCALL *)(void *, const char *);
  using SetIntFn = void(PROXY_THISCALL *)(void *, int);
  const auto findVar = reinterpret_cast<FindVarFn>((*static_cast<void ***>(g_cvars))[13]);
  // Names not found yet (client cvars before client.dll registers them) are
  // looked up again now and then.
  static unsigned pass = 0;
  const bool lookup = pass++ % 300 == 0;
  unsigned found = 0, missing = 0;
  std::string missingNames;
  for (Forced &f : g_forced) {
    if (!f.var && lookup) {
      void *var = findVar(g_cvars, f.name);
      if (var) {
        // The layout this relies on: the name at +0x0C.
        const char *name = *reinterpret_cast<const char *const *>(static_cast<uint8_t *>(var) + 0x0C);
        if (IsBadReadPtr(name, 1) || _stricmp(name, f.name) != 0) {
          broken = true;
          g_cvarState = "ConVar layout not as expected: nothing forced";
          diag::stage("L4D2: ConVar %s does not hold its name at +0x0C: cvar forcing off", f.name);
          return;
        }
        f.var = var;
      }
    }
    if (!f.var) {
      ++missing;
      missingNames += missingNames.empty() ? f.name : std::string(" ") + f.name;
      continue;
    }
    ++found;
    auto *bytes = static_cast<uint8_t *>(f.var);
    // FCVAR_DEVELOPMENTONLY, FCVAR_HIDDEN, FCVAR_CHEAT.
    *reinterpret_cast<int *>(bytes + 0x14) &= ~((1 << 1) | (1 << 4) | (1 << 14));
    if (f.set && !f.skipped) {
      void *conVar = bytes + 0x18; // IConVar (SetValue returns at once when unchanged)
      reinterpret_cast<SetIntFn>((*static_cast<void ***>(conVar))[1])(conVar, f.value);
    }
  }
  if (lookup) {
    std::lock_guard<std::mutex> lock(g_cvarNamesLock);
    g_cvarMissingNames = missingNames;
  }
  g_cvarsFound = found;
  g_cvarsMissing = missing;
  g_cvarState = "forced every frame";
  ++g_stats.cvarPasses;
}

// ---- once per frame on the main thread ----

void mainFrame() {
  const uint32_t present = g_presents.load(std::memory_order_relaxed);
  if (present == g_lastMainFrame)
    return;
  g_lastMainFrame = present;
  ++g_stats.mainFrames;
  forceCvars();
  sampleLights();
}

// ---- the map ----

// Polled at every Present (the thread that runs the shader API's draws, where
// markers arrive): "openremix:map <name>" when the engine's level changes and
// stayed the same for two frames (the name is the main thread's, read while it
// may be written), "openremix:map" when it disconnects.
void pollMap() {
  if (!g_engineClient)
    return;
  using GetLevelNameFn = const char *(PROXY_THISCALL *)(void *);
  const char *level = reinterpret_cast<GetLevelNameFn>((*static_cast<void ***>(g_engineClient))[53])(g_engineClient);
  char name[260] = {};
  if (level)
    std::snprintf(name, sizeof(name), "%s", level);
  if (std::strcmp(name, "dedicated") == 0)
    name[0] = 0;
  std::string text;
  {
    std::lock_guard<std::mutex> lock(g_mapLock);
    if (g_candidate != name) {
      g_candidate = name;
      g_stable = 1;
      return;
    }
    if (++g_stable < 2 || g_candidate == g_announced)
      return;
    g_announced = g_candidate;
    text = g_announced.empty() ? "openremix:map" : "openremix:map " + g_announced;
  }
  ++(text.size() > 13 ? g_loads : g_unloads);
  diag::stage("L4D2: %s", text.c_str());
  remix::onMarker(text.c_str());
}

// ---- hooks ----

[[gnu::force_align_arg_pointer]] void PROXY_THISCALL drawWorldLists(void *self, void *list, unsigned long flags,
                                                                      float waterZ) {
  const int view = viewId();
  if (view == kViewMain || view < 0)
    mainFrame();
  // The 3D skybox's world is the BSP's only when it is path traced (in world
  // space); otherwise the engine's sky view keeps its draws (l4d2-rtx).
  const bool mark = view != kView3dSky || bsp::skyTracedReach() > 0;
  ++(mark ? g_stats.worldLists : g_stats.worldListsSky);
  if (mark)
    marker("openremix:world_lists 1");
  g_drawWorldLists(self, list, flags, waterZ);
  if (mark)
    marker("openremix:world_lists 0");
}

// Overlays come from the BSP too, except in the 3D skybox (bsp_world leaves
// those to the engine).
[[gnu::force_align_arg_pointer]] void PROXY_THISCALL renderOverlays(void *self, int sortGroup) {
  if (viewId() == kView3dSky) {
    ++g_stats.overlaysSky;
    g_renderOverlays(self, sortGroup);
    return;
  }
  ++g_stats.overlays;
  marker("openremix:overlays 1");
  g_renderOverlays(self, sortGroup);
  marker("openremix:overlays 0");
}

// IVModelRender::DrawModelExecute(const DrawModelState_t &, const
// ModelRenderInfo_t &, matrix3x4_t *bones): a static prop the BSP world draws
// itself (bsp::providesProp, by placement: ModelRenderInfo_t::pModelToWorld at
// +0x20) is not drawn (the proxy would drop its draws). The 3D skybox stays
// with the engine. OPENREMIX_PROXY_ENGINE_PROPS=1 draws them all.
[[gnu::force_align_arg_pointer]] void PROXY_THISCALL drawModelExecute(void *self, const void *state,
                                                                        const uint8_t *info, void *bones) {
  static const bool engineProps = envFlag("ENGINE_PROPS", false);
  if (!engineProps && info && viewId() != kView3dSky) {
    const float *modelToWorld = *reinterpret_cast<const float *const *>(info + 0x20);
    if (modelToWorld && bsp::providesProp(modelToWorld)) {
      ++g_stats.propsSkipped;
      return;
    }
  }
  ++g_stats.propsDrawn;
  g_drawModelExecute(self, state, info, bones);
}

// ---- install ----

bool slotInCode(void *object, unsigned slot, const Module &module) {
  return object && module.inCode((*static_cast<void ***>(object))[slot]);
}

void installWorldLists(void *renderView) {
  // Slot 13 forwards to CRender::DrawWorldLists (vtable +0x20) and pops 12
  // bytes: push ebp; mov ebp,esp; mov ecx,[g_EngineRenderer] ... mov eax,[eax+0x20].
  if (!slotInCode(renderView, 13, g_engine)) {
    g_worldListsHook.state = "slot 13 not in engine.dll";
    return;
  }
  void **slot = &(*static_cast<void ***>(renderView))[13];
  const auto *fn = static_cast<const uint8_t *>(*slot);
  g_worldListsHook.rva = uintptr_t(fn) - g_engine.base;
  if (!bytesAt(g_engine, g_worldListsHook.rva, "55 8B EC 8B 0D") ||
      !bytesAt(g_engine, g_worldListsHook.rva + 0x11, "8B 40 20")) {
    g_worldListsHook.state = "not DrawWorldLists (bytes differ)";
    return;
  }
  g_drawWorldLists = reinterpret_cast<DrawWorldListsFn>(*slot);
  g_worldListsHook.state =
      swapSlot(slot, reinterpret_cast<void *>(&drawWorldLists)) ? "ok" : "cannot write the vtable";
}

void installOverlays() {
  // COverlayMgr's vtable by its RTTI (engine.dll+0x37661C in the build this
  // was written for), slot 7: RenderOverlays (pops 4 bytes).
  void **vtable = rttiVtable(g_engine, ".?AVCOverlayMgr@@");
  if (!vtable) {
    g_overlaysHook.state = "COverlayMgr vtable not found";
    return;
  }
  if (!g_engine.inCode(vtable[7])) {
    g_overlaysHook.state = "slot 7 not in engine.dll";
    return;
  }
  g_overlaysHook.rva = uintptr_t(vtable[7]) - g_engine.base;
  if (!bytesAt(g_engine, g_overlaysHook.rva, "55 8B EC 81 EC 68 06 00 00")) {
    g_overlaysHook.state = "not RenderOverlays (bytes differ)";
    return;
  }
  diag::stage("L4D2: COverlayMgr vtable at engine+0x%lX", (unsigned long)(uintptr_t(vtable) - g_engine.base));
  g_renderOverlays = reinterpret_cast<RenderOverlaysFn>(vtable[7]);
  g_overlaysHook.state =
      swapSlot(&vtable[7], reinterpret_cast<void *>(&renderOverlays)) ? "ok" : "cannot write the vtable";
}

void installModelRender() {
  void *modelRender = createInterface("engine.dll", "VEngineModel016");
  if (!slotInCode(modelRender, 19, g_engine)) {
    g_modelHook.state = "VEngineModel016 slot 19 not found";
    return;
  }
  void **slot = &(*static_cast<void ***>(modelRender))[19];
  g_modelHook.rva = uintptr_t(*slot) - g_engine.base;
  // push ebp; mov ebp,esp; sub esp,0x28C (CModelRender::DrawModelExecute).
  if (!bytesAt(g_engine, g_modelHook.rva, "55 8B EC 81 EC 8C 02 00 00")) {
    g_modelHook.state = "not DrawModelExecute (bytes differ)";
    return;
  }
  g_drawModelExecute = reinterpret_cast<DrawModelExecuteFn>(*slot);
  g_modelHook.state =
      swapSlot(slot, reinterpret_cast<void *>(&drawModelExecute)) ? "ok" : "cannot write the vtable";
}

bool installEngine() {
  if (!g_engine.open("engine.dll") || !g_materialSystem.open("materialsystem.dll"))
    return false;
  void *renderView = createInterface("engine.dll", "VEngineRenderView013");
  void *materials = createInterface("materialsystem.dll", "VMaterialSystem080");
  g_engineClient = createInterface("engine.dll", "VEngineClient013");
  g_cvars = createInterface("vstdlib.dll", "VEngineCvar007");
  if (!renderView || !materials)
    return false;
  if (!slotInCode(materials, 96, g_materialSystem)) {
    g_status = "IMaterialSystem slot 96 not in materialsystem.dll: no markers, nothing installed";
    diag::stage("L4D2: %s", g_status.load());
    return true;
  }
  if (g_engineClient && !slotInCode(g_engineClient, 53, g_engine))
    g_engineClient = nullptr;
  {
    Module vstdlib;
    if (g_cvars && !(vstdlib.open("vstdlib.dll") && slotInCode(g_cvars, 13, vstdlib)))
      g_cvars = nullptr;
  }
  g_materials = materials;
  installWorldLists(renderView);
  installOverlays();
  if (envFlag("ENGINE_PROPS", false))
    g_modelHook.state = "off (OPENREMIX_PROXY_ENGINE_PROPS=1)";
  else
    installModelRender();
  g_status = "engine.dll hooked; waiting for client.dll";
  diag::stage("L4D2: engine hooks: DrawWorldLists %s, RenderOverlays %s, DrawModelExecute %s; GetLevelName %s, "
              "cvars %s",
              g_worldListsHook.state.load(), g_overlaysHook.state.load(), g_modelHook.state.load(),
              g_engineClient ? "ok" : "MISSING", g_cvars ? "ok" : "MISSING");
  return true;
}

bool installClient() {
  if (!g_client.open("client.dll"))
    return false;
  if (g_viewIdPattern.find(g_client) && bytesAt(g_client, g_viewIdPattern.rva, "89 15")) {
    const auto address = *g_client.at<const uintptr_t>(g_viewIdPattern.rva + 2);
    if (g_client.holds(reinterpret_cast<const void *>(address)))
      g_viewId = reinterpret_cast<const int *>(address);
  }
  diag::stage("L4D2: view id %s (client+0x%lX, %u matches)", g_viewId ? "found" : "NOT found: every view marked",
              (unsigned long)g_viewIdPattern.rva, g_viewIdPattern.count);
  if (!g_materials)
    return true; // nothing on the engine side either (no markers)
  installCull(g_engine, g_client);
  installLights();
  g_status = "installed";
  return true;
}

} // namespace

void dumpStatistics(FILE *out) {
  static ULONGLONG last = GetTickCount64();
  const ULONGLONG now = GetTickCount64();
  const double seconds = now > last ? double(now - last) / 1000 : 1;
  last = now;
  auto rate = [&](std::atomic<uint64_t> &counter) { return double(counter.exchange(0)) / seconds; };
  fprintf(out, "== L4D2 engine: %s | view id %s (client+0x%lX, %u matches)\n", g_status.load(),
          g_viewId ? "ok" : "missing", (unsigned long)g_viewIdPattern.rva, g_viewIdPattern.count);
  for (const Hook *hook : {&g_worldListsHook, &g_overlaysHook, &g_modelHook})
    fprintf(out, "   %s: %s (engine+0x%lX)\n", hook->name, hook->state.load(), (unsigned long)hook->rva);
  fprintf(out,
          "   per second: world lists marked %.1f, 3D sky left to the engine %.1f | overlays marked %.1f, 3D sky "
          "%.1f | static props skipped %.1f, models drawn %.1f | main frames %.1f, markers %.1f\n",
          rate(g_stats.worldLists), rate(g_stats.worldListsSky), rate(g_stats.overlays), rate(g_stats.overlaysSky),
          rate(g_stats.propsSkipped), rate(g_stats.propsDrawn), rate(g_stats.mainFrames), rate(g_markers));
  {
    std::lock_guard<std::mutex> lock(g_mapLock);
    fprintf(out, "   map: \"%s\" (GetLevelName %s; %u loads, %u unloads)\n", g_announced.c_str(),
            g_engineClient ? "ok" : "missing", g_loads.load(), g_unloads.load());
  }
  {
    std::lock_guard<std::mutex> lock(g_cvarNamesLock);
    fprintf(out, "   cvars: %s, %u found, %u missing%s%s (%.1f passes/s)\n", g_cvarState.load(), g_cvarsFound.load(),
            g_cvarsMissing.load(), g_cvarMissingNames.empty() ? "" : ": ", g_cvarMissingNames.c_str(),
            rate(g_stats.cvarPasses));
  }
  dumpCull(out);
  dumpLights(out);
}

} // namespace proxy::l4d2

// d3d_hooks' Present: installs what is not yet in place (retried until
// engine.dll, materialsystem.dll and client.dll are loaded), then the
// per-frame work of this thread. OPENREMIX_PROXY_ENGINE_HOOKS=0 leaves the
// engine untouched.
namespace proxy::engine {
bool install() {
  using namespace proxy::l4d2;
  g_presents.fetch_add(1, std::memory_order_relaxed);
  // The engine side serves OpenRemix only (OPENREMIX_PROXY=0: the game as it is).
  static const bool enabled = envFlag("ENGINE_HOOKS", true) && remix::enabled();
  if (!enabled) {
    g_status = remix::enabled() ? "off (OPENREMIX_PROXY_ENGINE_HOOKS=0)" : "off (OPENREMIX_PROXY=0)";
    return false;
  }
  static bool engineDone = false, clientDone = false;
  if (!engineDone)
    engineDone = installEngine();
  if (engineDone && !clientDone)
    clientDone = installClient();
  if (engineDone)
    pollMap();
  // Menus and loading screens draw no world: the main thread's per-frame work
  // runs here when the main thread presents (mat_queue_mode 0).
  if (clientDone && onMainThread())
    mainFrame();
  return engineDone && clientDone;
}
} // namespace proxy::engine
