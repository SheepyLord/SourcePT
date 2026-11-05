#pragma once
#include <windows.h>

#include <cstring>
#include <string>

// The game the proxy is built for, chosen when the build is configured
// (meson option `game`): PROXY_GAME_GMOD, Garry's Mod x64 (bin/win64),
// PROXY_GAME_L4D2, Left 4 Dead 2 (x86), or PROXY_GAME_HL2RTX, Half-Life 2 RTX
// (x86, a Mapbase mod on NVIDIA's Portal RTX engine build). What differs
// between them is named here; the rest of the proxy reads these instead of
// literals.
#if defined(PROXY_GAME_GMOD) + defined(PROXY_GAME_L4D2) + defined(PROXY_GAME_HL2RTX) != 1
#error "define one of PROXY_GAME_GMOD, PROXY_GAME_L4D2, PROXY_GAME_HL2RTX (meson option game)"
#endif
#if defined(PROXY_GAME_GMOD) && !defined(_WIN64)
#error "the Garry's Mod proxy is a 64-bit DLL"
#endif
#if defined(PROXY_GAME_L4D2) && defined(_WIN64)
#error "the Left 4 Dead 2 proxy is a 32-bit DLL"
#endif
#if defined(PROXY_GAME_HL2RTX) && defined(_WIN64)
#error "the Half-Life 2 RTX proxy is a 32-bit DLL"
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
// The game draws its 3D scene through vertex shaders only: draws without one
// (none in the scene) are not converted from the fixed-function pipeline.
constexpr bool kFixedFunction = false;

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

// Vertex shader c3.x: cFlexScale, 1 while a flex mesh is bound to stream 2
// (the fallback when a shader's constant table does not name it).
constexpr unsigned kRegFlexScale = 3;
// Shader names as the materials report them (the stock ones).
constexpr char kShaderPrefix[] = "";

// The runtime: OpenRemix in process, bin/win64/openremix/remix.dll (its
// dependencies, SDL3, OpenUSD, DLSS, resolved from that directory).
constexpr wchar_t kRuntimePath[] = L"\\openremix\\remix.dll"; // after the proxy's directory
constexpr char kRuntimeLabel[] = "openremix\\remix.dll";
constexpr wchar_t kRuntimeModule[] = L"remix.dll";
constexpr char kRuntimeName[] = "remix.dll";

#elif defined(PROXY_GAME_L4D2)

constexpr bool kGMod = false;

constexpr char kShaderApiInterface[] = "ShaderApi029";
constexpr char kHardwareConfigInterface[] = "MaterialSystemHardwareConfig013";
// L4D2: the tracker is wrapped but not hooked (vb_origins' slots are not
// verified there, and nothing builds meshes the way GMod's Lua does).
constexpr bool kHookVBAllocTracker = false;
constexpr bool kFixedFunction = false;

// IShaderAPIDX8 (ShaderApi029, shaderapidx9.dll build 10097) slots: see
// slots_shaderapi_l4d2.h.
// void SetLights(int nCount, const LightDesc_t *pDesc): an array (stack 8).
constexpr unsigned kSlotSetLight = 88;
constexpr bool kSetLightsArray = true;
// void Bind(IMaterial *) (engine_materials, which checks its code).
constexpr unsigned kSlotBind = 76;
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

// L4D2's shaders keep cFlexScale at c13 (c3 is a light count there).
constexpr unsigned kRegFlexScale = 13;
constexpr char kShaderPrefix[] = "";

// ISurface (vguimatsurface.dll) SetCursorAlwaysVisible(bool): VGUI_Surface031
// slot 49 (stores the flag at +0x29C).
constexpr char kSurfaceInterface[] = "VGUI_Surface031";
constexpr unsigned kSlotSetCursorAlwaysVisible = 49;

// The runtime: the x86 -> x64 bridge client beside the proxy (bin), which runs
// OpenRemix in its 64-bit host process.
constexpr wchar_t kRuntimePath[] = L"\\remix_bridge.dll";
constexpr char kRuntimeLabel[] = "remix_bridge.dll";
constexpr wchar_t kRuntimeModule[] = L"remix_bridge.dll";
constexpr char kRuntimeName[] = "remix_bridge.dll";

#else // PROXY_GAME_HL2RTX

constexpr bool kGMod = false;

// Half-Life 2 RTX (Steam app 2477290): Valve's Portal engine as NVIDIA built
// it for Portal RTX (materialsystem, shaderapidx9 from "portal-trex"), the
// SDK 2013 interfaces, with Mapbase's client, server and game shaders.
constexpr char kShaderApiInterface[] = "ShaderApi030";
constexpr char kHardwareConfigInterface[] = "MaterialSystemHardwareConfig012";
// The tracker is wrapped but not hooked (as in L4D2).
constexpr bool kHookVBAllocTracker = false;
// NVIDIA's build renders everything through D3D9's fixed-function pipeline
// (made for RTX Remix, which takes its transforms and texture stages), at any
// -dxlevel: the D3D hooks track SetTransform, SetFVF, the texture stage states
// and the lights, and draws with no vertex shader are converted from them
// (fixed_function.h).
constexpr bool kFixedFunction = true;

// IShaderAPIDX8 (ShaderApi030, shaderapidx9.dll of Nov 2026): see
// slots_shaderapi_hl2rtx.h. SDK 2013's IShaderAPI order, one slot less from
// 99 on (no GetPixelFogCombo1): each slot below checked in its code.
// void SetLight(int lightNum, const LightDesc_t &desc), one at a time
// (copies 0x58 bytes into its light array).
constexpr unsigned kSlotSetLight = 126;
constexpr bool kSetLightsArray = false;
// void Bind(IMaterial *) (engine_materials, which checks its code).
constexpr unsigned kSlotBind = 113;
// void SetPIXMarker(unsigned long color, const char *name) (a stub, ret 8).
constexpr unsigned kSlotSetPIXMarker = 215;
// void GetMatrix(MaterialMatrixMode_t, float *dst) (copies its stack's top
// matrix); int GetCurrentNumBones() const.
constexpr unsigned kSlotGetMatrix = 12;
constexpr unsigned kSlotGetCurrentNumBones = 34;
// GetSceneFogMode 4 (returns the mode), GetSceneFogColor 5 (copies 3 bytes),
// GetFogDistances 212 (start, end, fog z); GMod: 4, 5, 213.
constexpr unsigned kSlotGetSceneFogMode = 4, kSlotGetSceneFogColor = 5, kSlotGetFogDistances = 212;
constexpr bool kSceneFogSlots = true;

// stdshader_dx9's and Mapbase's vertex shaders: cFlexScale at c3, as in GMod
// (their constant tables: c4 cModelViewProj, c8 cViewProj, c21..c26 the
// ambient cube, c50/51 the iris projection, c58 cModel).
constexpr unsigned kRegFlexScale = 3;
// Mapbase's game_shader_dx9 registers the stock shaders again as
// "SDK_<name>" (SDK_VertexLitGeneric, ...): a material that binds one is
// read as the stock shader (engine_materials, bsp_world compare lowercase).
constexpr char kShaderPrefix[] = "sdk_";

// ISurface (vguimatsurface.dll) SetCursorAlwaysVisible(bool): VGUI_Surface030
// slot 52 (stores the flag at +0x298, which IsCursorVisible, 53, reads).
constexpr char kSurfaceInterface[] = "VGUI_Surface030";
constexpr unsigned kSlotSetCursorAlwaysVisible = 52;

// The runtime: the bridge, as in L4D2.
constexpr wchar_t kRuntimePath[] = L"\\remix_bridge.dll";
constexpr char kRuntimeLabel[] = "remix_bridge.dll";
constexpr wchar_t kRuntimeModule[] = L"remix_bridge.dll";
constexpr char kRuntimeName[] = "remix_bridge.dll";

#endif

// A shader name without kShaderPrefix (matched in any case), for comparisons
// with the stock names ("SDK_VertexLitGeneric": "VertexLitGeneric").
inline std::string stockShaderName(const char *name) {
  constexpr size_t n = sizeof(kShaderPrefix) - 1;
  if (!name)
    return {};
  if (n && std::strlen(name) > n && _strnicmp(name, kShaderPrefix, n) == 0)
    name += n;
  return name;
}

// The loaded runtime module, where the API's extensions (remixapi_*EXT) are
// looked up (nullptr before it is loaded).
inline HMODULE runtimeModule() { return GetModuleHandleW(kRuntimeModule); }

} // namespace proxy::game
