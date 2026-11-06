project('gmodrtx', ['cpp', 'c'], version: '0.1.0',
        default_options: ['cpp_std=c++17', 'warning_level=2', 'buildtype=release', 'b_ndebug=if-release'])

# One tree, four games, chosen at configure time (-Dgame=, default by the
# cross file): Garry's Mod x64 or Team Fortress 2 x64
# (cross/x86_64-w64-mingw32.txt, -Dgame=tf2), Left 4 Dead 2 x86 or Half-Life 2
# RTX x86 (cross/i686-w64-mingw32.txt, -Dgame=hl2rtx). src/game.h holds what
# differs.
game = get_option('game')
cpu = host_machine.cpu_family()
if game == 'auto'
  game = cpu == 'x86_64' ? 'gmod' : 'l4d2'
endif
if game == 'gmod' and cpu != 'x86_64'
  error('the Garry\'s Mod proxy is a 64-bit DLL: configure with cross/x86_64-w64-mingw32.txt')
endif
if game == 'tf2' and cpu != 'x86_64'
  error('the Team Fortress 2 proxy is a 64-bit DLL (tf_win64.exe): configure with cross/x86_64-w64-mingw32.txt')
endif
if game == 'l4d2' and cpu != 'x86'
  error('the Left 4 Dead 2 proxy is a 32-bit DLL: configure with cross/i686-w64-mingw32.txt')
endif
if game == 'hl2rtx' and cpu != 'x86'
  error('the Half-Life 2 RTX proxy is a 32-bit DLL: configure with cross/i686-w64-mingw32.txt')
endif

remix_inc = get_option('openremix_include')
if remix_inc == ''
  error('set -Dopenremix_include=<openremix>/public/include')
endif
# SDL3 headers for the panel's input events (the runtime ships SDL3.dll).
sdl_inc = get_option('sdl_include')
if sdl_inc == ''
  sdl_inc = remix_inc / '..' / '..' / 'subprojects' / 'SDL3-3.4.14' / 'include'
endif

subdir('deps/dxso')

# The proxy's own host-pass shaders (src/shaders), SPIR-V as C arrays.
glslang = find_program('glslangValidator')
host_shaders = []
foreach shader : ['host_depth.vert', 'host_depth.frag', 'host_frame_depth.frag']
  host_shaders += custom_target(shader + '.inc',
    input: 'src/shaders' / shader, output: shader + '.inc',
    command: [glslang, '-V', '--vn', shader.replace('.', '_'), '-o', '@OUTPUT@', '@INPUT@'])
endforeach

sources = [
  'src/main.cpp', 'src/vtable_proxy.cpp', 'src/d3d_hooks.cpp', 'src/remix_output.cpp', 'src/texture_formats.cpp',
  'src/dxvk_hash.cpp', 'src/texture_categories.cpp', 'src/source_fs.cpp', 'src/bsp_world.cpp',
  'src/engine_materials.cpp', 'src/pbr_rules.cpp',
  'src/vertex_program.cpp', 'src/shader_tables.cpp', 'src/particles.cpp', 'src/fixed_function.cpp', 'src/diag.cpp', 'src/lzma_source.cpp', 'src/host_passes.cpp', 'src/vb_origins.cpp',
  'deps/lzma/LzmaDec.c', host_shaders,
]
cpp_args = []
link_args = ['-static', '-static-libgcc', '-static-libstdc++']
if game == 'gmod'
  cpp_args += '-DPROXY_GAME_GMOD'
  # Engine-side hooks (GMod has no client mod) and the in-process panel.
  sources += ['src/engine_hooks.cpp', 'src/remix_ui.cpp', 'src/engine_patches.cpp', 'src/engine_lights.cpp',
              'src/engine_shadows.cpp', 'src/engine_nocull.cpp']
  def_file = 'shaderapidx9.def'
elif game == 'tf2'
  # Team Fortress 2 x64: OpenRemix in process and GMod's interface-based engine
  # hooks and panel; GMod's build-specific engine patches off (their entry
  # points as no-ops); the module helpers for the classes it finds by RTTI.
  cpp_args += '-DPROXY_GAME_TF2'
  sources += ['src/engine_hooks.cpp', 'src/remix_ui.cpp', 'src/gmod_only_stubs.cpp', 'src/pe_module.cpp']
  def_file = 'shaderapidx9.def'
else
  # x86: the Remix API is used through the bridge (remix_c.h is 64-bit only
  # unless allowed; empty, as the headers that define it themselves). The
  # GMod-only modules' entry points as no-ops, the 32-bit module helpers and
  # the runtime UI in the bridge host.
  cpp_args += ['-DREMIX_ALLOW_X86=']
  sources += ['src/gmod_only_stubs.cpp', 'src/pe_module.cpp', 'src/bridge_ui.cpp']
  link_args += '-Wl,--enable-stdcall-fixup'
  if game == 'l4d2'
    # L4D2's own engine side (src/l4d2.h).
    cpp_args += '-DPROXY_GAME_L4D2'
    sources += ['src/l4d2_engine.cpp', 'src/l4d2_cull.cpp', 'src/l4d2_lights.cpp', 'src/l4d2_infected.cpp']
    def_file = 'shaderapidx9_l4d2.def'
  else
    # Half-Life 2 RTX: no engine side yet (src/hl2rtx_engine.cpp).
    cpp_args += '-DPROXY_GAME_HL2RTX'
    sources += ['src/hl2rtx_engine.cpp']
    def_file = 'shaderapidx9_hl2rtx.def'
  endif
endif

shared_library('shaderapidx9', sources,
  include_directories: include_directories(remix_inc, sdl_inc, 'deps/xxhash'),
  dependencies: [dxso_dep],
  cpp_args: cpp_args,
  name_prefix: '',
  vs_module_defs: def_file,
  link_args: link_args,
)
