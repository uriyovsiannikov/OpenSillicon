project(
    'twm',
    'c',
    default_options: [
        'warning_level=3',
        'werror=true',
    ],
    version: '1.0.13.1',
)

os_root = meson.project_source_root() / '../../..'
x11_prefix = os_root / 'build/x11/prefix'
gcc_include = run_command('gcc', '-print-file-name=include', check: true).stdout().strip()
add_project_arguments(
    '-DOPEN_SILLICON', '-nostdinc', '-I' + gcc_include,
    '-I' + os_root / 'user/libos/include', '-I' + x11_prefix / 'include',
    '-I' + os_root / 'user/xorg/deps/libX11/include',
    '-I' + os_root / 'build/x11/libX11/include',
    '-I' + os_root / 'user/xorg/deps/libxtrans', '-idirafter', '/usr/include',
    language: 'c',
)
add_project_link_arguments(
    '-nostdlib', '-static', '-no-pie', '-Wl,--build-id=none',
    '-Wl,-z,noexecstack', '-Wl,-T,' + os_root / 'user/c_linker.ld',
    os_root / 'build/libos/crt0.o', os_root / 'build/libos/liblibos.a',
    x11_prefix / 'lib/libi18n.a',
    language: 'c',
)

add_project_arguments(
    '-DAPP_VERSION="@0@"'.format(meson.project_version()),
    language: 'c',
)

add_project_arguments(
    '-DDATA_DIR="@0@"'.format(get_option('datadir')),
    language: 'c',
)

if meson.get_compiler('c').has_function('mkstemp')
    add_project_arguments(
        '-DHAVE_MKSTEMP=1',
        language: 'c',
    )
endif

twm_dependencies = [
    dependency('x11'),
    dependency('xext'),
    dependency('xt'),
    dependency('xmu'),
    dependency('ice'),
    dependency('sm'),
    dependency('xproto', version:'>=7.0.17'),
    dependency('xcb'),
    dependency('xau'),
]

subdir('src')
subdir('man')
