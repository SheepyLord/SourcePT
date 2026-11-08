// The x86 games (Left 4 Dead 2, Half-Life 2 RTX): the runtime's own UI
// (remix_ui.h). OpenRemix runs in the bridge's 64-bit host process, its
// window and SDL input there: F12 on the game window toggles the UI
// (SetUIState ADVANCED / NONE), and while it is open the game window's input
// is forwarded to the host (remix_bridge.dll!remixapi_bridge_InjectInputEXT)
// instead of the game, as l4d2-rtx's imgui.cpp did. Key-ups still reach the
// game (no stuck keys); raw mouse input (WM_INPUT) does not (it would turn the
// camera). The engine shows the OS cursor meanwhile
// (ISurface::SetCursorAlwaysVisible, game.h).
// The library loader (remixapi_lib_*) is remix_output's.
#define REMIX_WINAPI_NO_LIBRARY_LOADER
#include "remix_ui.h"

#include "game.h"
#ifdef PROXY_GAME_L4D2
#include "l4d2.h"
#endif
#include "pe_module.h"

#include <atomic>
#include <cstdio>
#include <initializer_list>

namespace proxy::remix_ui {
namespace {

HWND g_window = nullptr;
WNDPROC g_previous = nullptr;
remixapi_Interface *g_api = nullptr;
std::atomic<bool> g_active{false};
char g_status[160] = "not installed";
std::atomic<unsigned> g_toggles{0};
std::atomic<uint64_t> g_forwarded{0};
const char *g_injectState = "not looked up";

// remixapi_bridge_InjectInputEXT(type, code, down, x, y, wheel): type 0 mouse
// move, 1 button (code 1 left, 2 middle, 3 right), 2 wheel (notches), 3 key
// (virtual key), 4 text (UTF-32), 5 focus (down: gained); client pixels.
enum Input : uint32_t { Motion = 0, Button = 1, Wheel = 2, Key = 3, Text = 4, Focus = 5 };
using InjectFn = remixapi_ErrorCode(REMIXAPI_CALL *)(uint32_t, uint32_t, uint32_t, int32_t, int32_t, float);

void inject(Input type, uint32_t code, bool down, int32_t x = 0, int32_t y = 0, float wheel = 0) {
  static const InjectFn fn = [] {
    HMODULE bridge = proxy::game::runtimeModule();
    InjectFn found = nullptr;
    for (const char *name : {"remixapi_bridge_InjectInputEXT", "remixapi_bridge_InjectInputEXT@24",
                             "_remixapi_bridge_InjectInputEXT@24"})
      if (!found && bridge)
        found = reinterpret_cast<InjectFn>(reinterpret_cast<void *>(GetProcAddress(bridge, name)));
    g_injectState = found ? "ok" : "remixapi_bridge_InjectInputEXT not exported";
    return found;
  }();
  if (fn) {
    fn(type, code, down ? 1 : 0, x, y, wheel);
    ++g_forwarded;
  }
}

// ISurface::SetCursorAlwaysVisible(bool) (game.h: L4D2's VGUI_Surface031
// slot 49, Half-Life 2 RTX's VGUI_Surface030 slot 52).
void setCursorVisible(bool visible) {
  static void *surface = proxy::pe::createInterface("vguimatsurface.dll", proxy::game::kSurfaceInterface);
  if (surface)
    reinterpret_cast<void(PROXY_THISCALL *)(void *, bool)>(
        (*static_cast<void ***>(surface))[proxy::game::kSlotSetCursorAlwaysVisible])(surface, visible);
}

void setActive(bool active) {
  g_active = active;
  ++g_toggles;
  g_api->SetUIState(active ? REMIXAPI_UI_STATE_ADVANCED : REMIXAPI_UI_STATE_NONE);
  inject(Focus, 0, active);
  setCursorVisible(active);
}

// True: the message was taken for the UI.
bool forward(UINT message, WPARAM w, LPARAM l) {
  const int32_t x = short(LOWORD(l)), y = short(HIWORD(l));
  switch (message) {
  case WM_MOUSEMOVE: inject(Motion, 0, false, x, y); return true;
  case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: inject(Button, 1, true, x, y); return true;
  case WM_LBUTTONUP: inject(Button, 1, false, x, y); return true;
  case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: inject(Button, 2, true, x, y); return true;
  case WM_MBUTTONUP: inject(Button, 2, false, x, y); return true;
  case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: inject(Button, 3, true, x, y); return true;
  case WM_RBUTTONUP: inject(Button, 3, false, x, y); return true;
  case WM_MOUSEWHEEL: inject(Wheel, 0, false, 0, 0, float(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA); return true;
  case WM_KEYDOWN: case WM_SYSKEYDOWN: inject(Key, uint32_t(w), true); return true;
  case WM_KEYUP: case WM_SYSKEYUP: inject(Key, uint32_t(w), false); return false; // the game still sees key-ups
  case WM_CHAR: inject(Text, uint32_t(w), true); return true;
  case WM_SETFOCUS: case WM_KILLFOCUS: inject(Focus, 0, message == WM_SETFOCUS); return false;
  case WM_INPUT: return true; // raw mouse would turn the camera
  default: return false;
  }
}

[[gnu::force_align_arg_pointer]] LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == kToggleMessage) {
    setActive(!g_active);
    return 0;
  }
  if (w == VK_F12 && (message == WM_KEYDOWN || message == WM_KEYUP)) {
    if (message == WM_KEYUP) {
      setActive(!g_active);
      return CallWindowProcW(g_previous, window, message, w, l);
    }
    return 0;
  }
  if (g_active && forward(message, w, l))
    return 0;
  return CallWindowProcW(g_previous, window, message, w, l);
}

} // namespace

void install(HWND window, remixapi_Interface *api, WNDPROC) {
#ifdef PROXY_GAME_L4D2
  proxy::l4d2::setGameWindow(window);
#endif
  if (g_previous || !window || !api || !api->SetUIState)
    return;
  g_window = window;
  g_api = api;
  g_previous =
      reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
  std::snprintf(g_status, sizeof(g_status), "window %p subclassed %d (window thread %lu, startup thread %lu)",
                static_cast<void *>(window), g_previous != nullptr, GetWindowThreadProcessId(window, nullptr),
                GetCurrentThreadId());
}

void uninstall() {
  if (!g_previous)
    return;
  if (GetWindowLongPtrW(g_window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&windowProc))
    SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_previous));
  if (g_active)
    setCursorVisible(false);
  g_active = false;
  std::snprintf(g_status, sizeof(g_status), "uninstalled");
}

bool active() { return g_active; }

void open() {
  if (g_api && !g_active)
    setActive(true);
}

void dumpStatistics(FILE *out) {
  std::fprintf(out, "   Remix UI (F12, bridge host): %s; open %d, toggles %u, events forwarded %llu, input %s\n",
               g_status, int(g_active.load()), g_toggles.load(),
               static_cast<unsigned long long>(g_forwarded.load()), g_injectState);
}

} // namespace proxy::remix_ui
