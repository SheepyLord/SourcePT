#pragma once
#include <windows.h>

// The game the proxy is built for, chosen when the build is configured
// (meson option `game`): PROXY_GAME_GMOD, Garry's Mod x64 (bin/win64), or
// PROXY_GAME_L4D2, Left 4 Dead 2 (x86). What differs between the two is named
// here; the rest of the proxy reads these instead of literals.
#if defined(PROXY_GAME_GMOD) == defined(PROXY_GAME_L4D2)
#error "define one of PROXY_GAME_GMOD, PROXY_GAME_L4D2 (meson option game)"
#endif
#if defined(PROXY_GAME_GMOD) && !defined(_WIN64)
#error "the Garry's Mod proxy is a 64-bit DLL"
#endif
#if defined(PROXY_GAME_L4D2) && defined(_WIN64)
#error "the Left 4 Dead 2 proxy is a 32-bit DLL"
#endif

// The engine's C++ methods (MSVC ABI), as the proxy hooks and calls them: a
// plain function whose first parameter is `this`. x86 passes `this` in ECX
// and the callee pops its arguments (__thiscall); x64 has one convention.
// Every typed hook on, and every call through, a C++ vtable uses it (the D3D9
// COM interfaces are STDMETHODCALLTYPE).
#ifdef _WIN64
#define PROXY_THISCALL
#else
#define PROXY_THISCALL __thiscall
#endif

namespace proxy::game {

#ifdef PROXY_GAME_GMOD

constexpr bool kGMod = true;

// Interface versions the proxy wraps (CreateInterface names).
constexpr char kShaderApiInterface[] = "ShaderApi030";
constexpr char kHardwareConfigInterface[] = "MaterialSystemHardwareConfig012";
// IShaderDeviceMgr::Connect(factory) hands out VBAllocTracker001 to the
// shader API; vb_origins hooks it (GMod: Lua meshes are told apart by it).
constexpr bool kHookVBAllocTracker = true;

// IShaderAPI (ShaderApi030) slots (tools/sdk_vtables64.py).
// void SetLight(int lightNum, const LightDesc_t &desc): SDK 2013 sets the
// model lights one at a time (copies 88 bytes into its light array).
constexpr unsigned kSlotSetLight = 127;
constexpr bool kSetLightsArray = false;
// void Bind(IMaterial *) (engine_materials).
constexpr unsigned kSlotBind = 114;
// void SetPIXMarker(unsigned long color, const char *name).
constexpr unsigned kSlotSetPIXMarker = 216;
// void GetMatrix(MaterialMatrixMode_t, float *dst).
constexpr unsigned kSlotGetMatrix = 12;
// int GetCurrentNumBones() const.
constexpr unsigned kSlotGetCurrentNumBones = 34;
// IShaderDynamicAPI: MaterialFogMode_t GetSceneFogMode(), void
// GetSceneFogColor(unsigned char *rgb); IShaderAPI: void GetFogDistances(float
// *start, float *end, float *fogZ).
constexpr unsigned kSlotGetSceneFogMode = 4, kSlotGetSceneFogColor = 5, kSlotGetFogDistances = 213;
constexpr bool kSceneFogSlots = true;

// The runtime: OpenRemix in process, bin/win64/openremix/remix.dll (its
// dependencies, SDL3, OpenUSD, DLSS, resolved from that directory).
constexpr wchar_t kRuntimePath[] = L"\\openremix\\remix.dll"; // after the proxy's directory
constexpr char kRuntimeLabel[] = "openremix\\remix.dll";
constexpr wchar_t kRuntimeModule[] = L"remix.dll";
constexpr char kRuntimeName[] = "remix.dll";

#else // PROXY_GAME_L4D2

constexpr bool kGMod = false;

constexpr char kShaderApiInterface[] = "ShaderApi029";
constexpr char kHardwareConfigInterface[] = "MaterialSystemHardwareConfig013";
// L4D2: the tracker is wrapped but not hooked (vb_origins' slots are not
// verified there, and nothing builds meshes the way GMod's Lua does).
constexpr bool kHookVBAllocTracker = false;

// IShaderAPIDX8 (ShaderApi029, shaderapidx9.dll build 10097) slots: see
// slots_shaderapi_l4d2.h.
// void SetLights(int nCount, const LightDesc_t *pDesc): an array (stack 8).
constexpr unsigned kSlotSetLight = 88;
constexpr bool kSetLightsArray = true;
// L4D2: Bind (76) is not hooked: engine_materials reads GMod's material system only.
// void SetPIXMarker(unsigned long color, const char *name) (reached from
// IMatRenderContext slot 137).
constexpr unsigned kSlotSetPIXMarker = 181;
constexpr unsigned kSlotGetMatrix = 40;
constexpr unsigned kSlotGetCurrentNumBones = 9;
// L4D2: GetSceneFogMode 2, GetSceneFogColor 3, GetFogDistances 178 by the
// slot alignment alone (stack sizes, not verified in the binary): not read
// until they are, so no fog is sent.
constexpr unsigned kSlotGetSceneFogMode = 2, kSlotGetSceneFogColor = 3, kSlotGetFogDistances = 178;
constexpr bool kSceneFogSlots = false;

// The runtime: the x86 -> x64 bridge client beside the proxy (bin), which runs
// OpenRemix in its 64-bit host process.
constexpr wchar_t kRuntimePath[] = L"\\remix_bridge.dll";
constexpr char kRuntimeLabel[] = "remix_bridge.dll";
constexpr wchar_t kRuntimeModule[] = L"remix_bridge.dll";
constexpr char kRuntimeName[] = "remix_bridge.dll";

#endif

// The loaded runtime module, where the API's extensions (remixapi_*EXT) are
// looked up (nullptr before it is loaded).
inline HMODULE runtimeModule() { return GetModuleHandleW(kRuntimeModule); }

} // namespace proxy::game
