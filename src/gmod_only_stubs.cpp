// The GMod-only modules' entry points for the builds that do not have them
// (Left 4 Dead 2): engine_patches, engine_nocull, engine_shadows and
// engine_lights patch one build of GMod's x64 engine.dll and client.dll. Each
// is off here, as it is on GMod when it does not validate.
//
// L4D2 has its own engine side (l4d2.h): engine::install (l4d2_engine.cpp:
// world lists, overlays, static props, the map, cvars, anti-culling,
// flashlights) and remix_ui (l4d2_ui.cpp: the runtime's UI in the bridge
// host, F12).
#include "engine_lights.h"
#include "engine_nocull.h"
#include "engine_patches.h"
#include "engine_shadows.h"

namespace proxy::engine_patches {
bool install() { return false; }
void beginWorldLists() {}
void endWorldLists() {}
void dumpStatistics(FILE *) {}
} // namespace proxy::engine_patches

namespace proxy::engine_nocull {
bool install(void *, void *) { return false; }
void dumpStatistics(FILE *) {}
} // namespace proxy::engine_nocull

namespace proxy::engine_shadows {
bool install() { return false; }
void dumpStatistics(FILE *) {}
} // namespace proxy::engine_shadows

namespace proxy::engine_lights {
bool install() { return false; }
void sample() {}
// The BSP world leaves the map's styled lights to the draws' SetLights.
bool stylesActive() { return false; }
void activate(uint32_t) {}
std::shared_ptr<const Snapshot> current() { return nullptr; }
void report(uint32_t, uint32_t, uint32_t) {}
void dumpStatistics(FILE *) {}
} // namespace proxy::engine_lights
