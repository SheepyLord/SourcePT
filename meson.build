project('gmodrtx', ['cpp', 'c'], version: '0.1.0',
        default_options: ['cpp_std=c++17', 'warning_level=2', 'buildtype=release', 'b_ndebug=if-release'])

# One tree, two games, chosen at configure time (-Dgame=, default by the
# cross file): Garry's Mod x64 (cross/x86_64-w64-mingw32.txt) or Left 4 Dead 2
# x86 (cross/i686-w64-mingw32.txt). src/game.h holds what differs.
game = get_option('game')
cpu = host_machine.cpu_family()
if game == 'auto'
  game = cpu == 'x86_64' ? 'gmod' : 'l4d2'
endif
if game == 'gmod' and cpu != 'x86_64'
  error('the Garry\'s Mod proxy is a 64-bit DLL: configure with cross/x86_64-w64-mingw32.txt')
endif
if game == 'l4d2' and cpu != 'x86'
  error('the Left 4 Dead 2 proxy is a 32-bit DLL: configure with cross/i686-w64-mingw32.txt')
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
  'src/vertex_program.cpp', 'src/particles.cpp', 'src/diag.cpp', 'src/lzma_source.cpp', 'src/host_passes.cpp', 'src/vb_origins.cpp',
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
else
  # x86: the Remix API is used through the bridge (remix_c.h is 64-bit only
  # unless allowed; empty, as the headers that define it themselves).
  cpp_args += ['-DPROXY_GAME_L4D2', '-DREMIX_ALLOW_X86=']
  # The GMod-only modules' entry points as no-ops; L4D2's own engine side
  # (src/l4d2.h) and the runtime UI in the bridge host.
  sources += ['src/gmod_only_stubs.cpp', 'src/l4d2_engine.cpp', 'src/l4d2_cull.cpp', 'src/l4d2_lights.cpp',
              'src/l4d2_ui.cpp']
  def_file = 'shaderapidx9_l4d2.def'
  link_args += '-Wl,--enable-stdcall-fixup'
endif

shared_library('shaderapidx9', sources,
  include_directories: include_directories(remix_inc, sdl_inc, 'deps/xxhash'),
  dependencies: [dxso_dep],
  cpp_args: cpp_args,
  name_prefix: '',
  vs_module_defs: def_file,
  link_args: link_args,
)
