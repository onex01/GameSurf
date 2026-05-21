project('gamesurf', 'c',
  version: '0.1.0',
  license: 'GPL-3.0',
  default_options: [
    'warning_level=2',
    'c_std=gnu11',
  ],
)

cc = meson.get_compiler('c')

# Dependencies
gtk_dep = dependency('gtk4', version: '>= 4.6')
webkit_dep = dependency('webkitgtk-6.0', version: '>= 2.40')
sdl2_dep = dependency('sdl2')
xtst_dep = dependency('xtst', required: false)
x11_dep = dependency('x11', required: false)
libinput_dep = dependency('libinput', required: false)
gsound_dep = dependency('gsound', required: false)
gstreamer_dep = dependency('gstreamer-1.0', required: false)
m_dep = cc.find_library('m', required: true)

# Configuration
config_data = configuration_data()
# Используем set_quoted для текстовых строк, чтобы они обернулись в ""
config_data.set_quoted('VERSION', meson.project_version())
config_data.set_quoted('PACKAGE_NAME', 'gamesurf')
config_data.set_quoted('GETTEXT_PACKAGE', 'gamesurf')
config_data.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))

# Check for X11 or Wayland
if xtst_dep.found() and x11_dep.found()
  config_data.set('HAVE_X11', 1)
elif libinput_dep.found()
  config_data.set('HAVE_WAYLAND', 1)
endif

configure_file(
  output: 'config.h',
  configuration: config_data,
)

# Include directories
inc = include_directories('src', '.')

# Sources
sources = files(
  'src/main.c',
  'src/gs-application.c',
  'src/gs-window.c',
  'src/gs-web-view.c',
  'src/gs-gamepad-manager.c',
  'src/gs-cursor-controller.c',
  'src/gs-virtual-keyboard.c',
  'src/gs-video-controller.c',
  'src/gs-settings.c',
  'src/gs-utils.c',
)

# Executable
executable('gamesurf',
  sources,
  include_directories: inc,
  dependencies: [
    gtk_dep,
    webkit_dep,
    sdl2_dep,
    xtst_dep,
    x11_dep,
    libinput_dep,
    gsound_dep,
    gstreamer_dep,
    m_dep,
  ],
  install: true,
)

# Data files
install_data('data/gamesurf.desktop',
  install_dir: get_option('datadir') / 'applications'
)

install_data('data/gamesurf.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

install_data('data/style.css',
  install_dir: get_option('datadir') / 'gamesurf'
)

# Post-install script
meson.add_install_script('sh', '-c', 'glib-compile-schemas $MESON_INSTALL_DESTDIR_PREFIX/share/glib-2.0/schemas')