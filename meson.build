project(
  'neo-qiv', 'c',
  version : '0.0.0',
  license : 'GPL2',
  default_options : [
      'warning_level=1',
      'prefix=/usr',
      'sysconfdir=/etc',
  ],
  meson_version : '>= 0.46'
)

incdir = include_directories('src')

builddir = meson.current_build_dir()
sourcedir = meson.current_source_dir()
package = meson.project_name()
version = meson.project_version()
prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
sysconfdir = get_option('sysconfdir')
localedir = join_paths(prefix, get_option('localedir'))
mandir = join_paths(prefix, get_option('mandir'))

c_compiler = meson.get_compiler('c')

# preprocessor related - start

pre_args = [
    '-DPACKAGE_NAME="qiv"',
    '-DPACKAGE_NAME_FANCY="Qiv"',
    '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),

    '-DPACKAGE_BUGREPORT="TODO"',
    '-DPACKAGE_ONLINE_DOCS="TODO"',

    '-DCENTER=1',
    '-DFILTER=1',
    '-DSTATUSBAR_FONT="Monospace 9"',
    '-DCOMMENT_FONT="Monospace 20"',

    '-DG_DISABLE_ASSERT',
    '-DG_DISABLE_CAST_CHECKS',
    '-DG_DISABLE_CHECKS',
    '-DG_LOG_DOMAIN="Qiv"',
]

if get_option('exif')
  pre_args += '-DEXIF'
endif

if get_option('magic')
  pre_args += '-DMAGIC'
endif

if get_option('lcms')
  pre_args += '-DLCMS'
endif


foreach a : pre_args
  add_project_arguments(a, language : ['c', 'cpp'])
endforeach

# preprocessor related - end


dep_gtk = dependency('gtk+-2.0')
dep_glib = dependency('glib-2.0')
dep_imlib2 = dependency('imlib2')
dep_exif = dependency('libexif')
dep_x11 = dependency('x11')

sources = [
  'src/event.c',
  'src/image.c',
  'src/main.c',
  'src/options.c',
  'src/utils.c',
  'src/xmalloc.c',
]

target_name = 'qiv'
target_type = 'executable'

qiv = build_target(
  target_name,
  sources,
  target_type: target_type,
  install : true,
  install_dir : bindir,
  dependencies: [
  dep_x11,
  dep_gtk,
  dep_glib,
  dep_imlib2,
  dep_exif,
  ],
)

