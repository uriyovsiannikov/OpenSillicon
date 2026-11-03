# SPDX-License-Identifier: MIT
#
# Copyright (c) 2025, Oracle and/or its affiliates.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

project(
  'xeyes',
  'c',
  version: '1.3.1',
  license: 'X11',
  license_files: 'COPYING',
  meson_version: '>= 1.3.0',
)

conf = configuration_data()
package_string = ' '.join(meson.project_name(), meson.project_version())
conf.set_quoted('PACKAGE_STRING', package_string)

# Replacement for XORG_DEFAULT_OPTIONS
cc = meson.get_compiler('c')
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

# The target has static X libraries and no complete pkg-config metadata for
# its freestanding ABI. Resolve the libraries directly; their headers come
# from the Xorg prefix supplied by the build environment.
dep_libxi   = cc.find_library('Xi', required: true)
dep_libxfixes = cc.find_library('Xfixes', required: true)
dep_libxext = cc.find_library('Xext', required: true)
dep_libxmu  = cc.find_library('Xmu', required: true)
dep_libxt   = cc.find_library('Xt', required: true)
dep_libxaw  = cc.find_library('Xaw7', required: true)
dep_libxpm  = cc.find_library('Xpm', required: true)
dep_libice  = cc.find_library('ICE', required: true)
dep_libsm   = cc.find_library('SM', required: true)
dep_libxcb  = cc.find_library('xcb', required: true)
dep_libxau  = cc.find_library('Xau', required: true)
dep_libx11  = cc.find_library('X11', required: true)

deps = [dep_libxi, dep_libxfixes, dep_libxext, dep_libxmu, dep_libxt,
        dep_libxaw, dep_libxpm, dep_libice, dep_libsm, dep_libxcb,
        dep_libxau, dep_libx11]

# Optional dependencies
if get_option('xrender').allowed()
  dep_xrender = cc.find_library('Xrender', required: get_option('xrender'))
  deps += dep_xrender
  conf.set('XRENDER', dep_xrender.found() ? 1 : false,
           description:  'Define to use X Render Extension')
else
  dep_xrender = dependency('', required: false)
endif
summary('X Render', dep_xrender.found())

if get_option('present').allowed()
  dep_x11_xcb     = dependency('x11-xcb',
                               required: get_option('present'))
  dep_xcb_present = dependency('xcb-present', version: '>= 1.9',
                               required: get_option('present'))
  dep_xcb_xfixes  = dependency('xcb-xfixes',
                               required: get_option('present'))
  dep_xcb_damage  = dependency('xcb-damage',
                               required: get_option('present'))
  found_all = dep_x11_xcb.found() and dep_xcb_present.found() and dep_xcb_xfixes.found() and dep_xcb_damage.found()

  if found_all
    deps += [dep_x11_xcb, dep_xcb_present, dep_xcb_xfixes, dep_xcb_damage]
  endif
  conf.set('PRESENT', found_all ? 1 : false,
           description:  'Define to use X Present Extension')
else
  found_all = false
endif
summary('X Present', found_all)

dep_libm = cc.find_library('m', required: false)
deps += dep_libm

config_h = configure_file(output: 'config.h', configuration: conf)
add_project_arguments('-DHAVE_CONFIG_H', language: ['c'])

# Make builds reproducible instead of embedding build paths due to use
# of __FILE__ in the assert() macro
fs = import('fs')
source_prefix_path = fs.relative_to(
    meson.project_source_root(),
    meson.project_build_root()
)
macro_prefix_map = '-fmacro-prefix-map=' + source_prefix_path + '/='
if cc.has_argument(macro_prefix_map)
  add_project_arguments(macro_prefix_map, language: 'c')
endif

xeyes_sources = [
  'Eyes.c',
  'Eyes.h',
  'EyesP.h',
  'transform.c',
  'transform.h',
  'xeyes.c',
  config_h
]

executable(
  'xeyes',
  xeyes_sources,
  dependencies: deps,
  install: true
)

# Man page
prog_sed = find_program('sed')

custom_target(
  'xeyes.man',
  input: 'man/xeyes.man',
  output: 'xeyes.1',
  command: [
    prog_sed,
    '-e', 's/__xorgversion__/"@0@" "X Version 11"/'.format(package_string),
    '-e', 's/__appmansuffix__/1/g',
    '-e', 's/__miscmansuffix__/7/g',
    '@INPUT@',
  ],
  capture: true,
  install: true,
  install_dir: get_option('mandir') / 'man1',
)
