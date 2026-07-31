project('NorthstarStubs', 'cpp',
  license         : 'Zlib',
  default_options : [
    'cpp_std=c++20',

    # the build works fine without these, but it significantly reduces the dll
    # size, and it's not like we need debugging symbols in the stubs since
    # they're pretty simple (note that strip applies at "meson install")
    'buildtype=release',
    'strip=true',
  ],
  meson_version   : '>= 0.59.0',
)

if (target_machine.cpu_family() != 'x86_64')
  error('only x86_64 is supported')
endif

windows = import('windows')
cpp     = meson.get_compiler('cpp')

add_project_arguments('-DNOMINMAX', language : 'cpp')
add_project_arguments('-D_WIN32_WINNT=0x0601', language: 'cpp')

if cpp.get_id() == 'clang'
  if cpp.has_argument('-Wno-unused-private-field')
    add_project_arguments('-Wno-unused-private-field', language: 'cpp')
  endif
  if cpp.has_argument('-Wno-microsoft-exception-spec')
    add_project_arguments('-Wno-microsoft-exception-spec', language: 'cpp')
  endif
  if cpp.has_argument('-Wno-dll-attribute-on-redeclaration')
    add_project_arguments('-Wno-dll-attribute-on-redeclaration', language: 'cpp')
  endif
endif

if cpp.get_id() != 'msvc'
  add_global_link_arguments('-static', '-static-libgcc', language: 'c')
  add_global_link_arguments('-static', '-static-libgcc', '-static-libstdc++', language: 'cpp')
endif

subdir('src/d3d11')
subdir('src/gfsdk')
