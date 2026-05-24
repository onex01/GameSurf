#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_DIR/build"
DIST_DIR="$PROJECT_DIR/dist"
CONFIG_DIR="$PROJECT_DIR/config"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

check_deps() {
  local missing=()
  for cmd in meson ninja pkg-config glib-compile-schemas; do
    if ! command -v "$cmd" &>/dev/null; then missing+=("$cmd"); fi
  done
  if [ ${#missing[@]} -ne 0 ]; then
    log_error "Missing: ${missing[*]}"
    exit 1
  fi
}

check_arm_deps() {
  local arch=$1
  local pc_dir=""
  case "$arch" in
    arm64) pc_dir="/usr/lib/aarch64-linux-gnu/pkgconfig" ;;
    armhf) pc_dir="/usr/lib/arm-linux-gnueabihf/pkgconfig" ;;
  esac
  if [ ! -f "$pc_dir/gtk4.pc" ]; then
    log_error "ARM GTK4 pkgconfig not found: $pc_dir/gtk4.pc"
    return 1
  fi
  export PKG_CONFIG_LIBDIR="$pc_dir:/usr/share/pkgconfig:/usr/lib/pkgconfig"
  export PKG_CONFIG_PATH=""
  if ! pkg-config --exists gtk4; then
    log_error "pkg-config cannot find ARM gtk4"
    return 1
  fi
  log_info "ARM gtk4 found: $(pkg-config --modversion gtk4)"
  return 0
}

build_arch() {
  local arch=$1
  local cross_file=$2
  local build_subdir="$BUILD_DIR/$arch"
  local dist_arch="$DIST_DIR/$arch"
  local app_dir="$dist_arch/GameSurf"

  log_step "Building for $arch..."

  if [ -n "$cross_file" ]; then
    if ! check_arm_deps "$arch"; then
      return 1
    fi
  fi

  rm -rf "$build_subdir" "$dist_arch"
  mkdir -p "$build_subdir"

  local setup_args=( --buildtype=release --prefix=/usr/local -Dstrip=true )

  if [ -n "$cross_file" ] && [ -f "$cross_file" ]; then
    local pc_dir=""
    case "$arch" in
      arm64) pc_dir="/usr/lib/aarch64-linux-gnu/pkgconfig" ;;
      armhf) pc_dir="/usr/lib/arm-linux-gnueabihf/pkgconfig" ;;
    esac
    export PKG_CONFIG_LIBDIR="$pc_dir:/usr/share/pkgconfig:/usr/lib/pkgconfig"
    export PKG_CONFIG_PATH=""
    setup_args+=("--cross-file=$cross_file")
  fi

  meson setup "$build_subdir" "$PROJECT_DIR" "${setup_args[@]}"
  ninja -C "$build_subdir"

  # Очистка переменных окружения
  unset PKG_CONFIG_LIBDIR PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR 2>/dev/null || true

  mkdir -p "$app_dir/bin" "$app_dir/data" "$app_dir/lib"
  cp "$build_subdir/gamesurf" "$app_dir/bin/"
  cp "$PROJECT_DIR/data/style.css" "$app_dir/data/"
  cp "$PROJECT_DIR/data/gamesurf.desktop" "$app_dir/data/"
  cp "$PROJECT_DIR/data/gamesurf.gschema.xml" "$app_dir/data/"

  if command -v glib-compile-schemas &>/dev/null; then
    glib-compile-schemas "$app_dir/data/"
  fi

  cat > "$dist_arch/GameSurf.sh" << 'LAUNCHER_EOF'
#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export GSETTINGS_SCHEMA_DIR="$SCRIPT_DIR/GameSurf/data"
export GAMESURF_DATA_DIR="$SCRIPT_DIR/GameSurf/data"
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
exec "$SCRIPT_DIR/GameSurf/bin/gamesurf" "$@"
LAUNCHER_EOF
  chmod +x "$dist_arch/GameSurf.sh"

  cat > "$dist_arch/launch.sh" << 'PORTMASTER_EOF'
#!/bin/bash
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
export GAMESURF_DATA_DIR="$(dirname "$0")/GameSurf/data"
export GSETTINGS_SCHEMA_DIR="$(dirname "$0")/GameSurf/data"
export SDL_JOYSTICK_HIDAPI=1
exec "$(dirname "$0")/GameSurf/bin/gamesurf" "$@"
PORTMASTER_EOF
  chmod +x "$dist_arch/launch.sh"

  log_info "$arch done → $dist_arch/"
}

create_packages() {
  log_step "Packaging..."
  mkdir -p "$DIST_DIR/packages"
  local version
  version="$(grep "version:" "$PROJECT_DIR/meson.build" | head -1 | sed "s/.*version: *'//;s/'.*//")"
  [ -z "$version" ] && version="0.3.0"

  for arch in x86_64 arm64 armhf; do
    local src="$DIST_DIR/$arch"
    if [ -d "$src" ]; then
      local pkg="$DIST_DIR/packages/gamesurf-${version}-${arch}.tar.xz"
      tar -cJf "$pkg" -C "$DIST_DIR" "$arch"
      log_info "Package: $pkg"
    fi
  done
}

usage() {
  cat << EOF
Usage: $0 [OPTIONS] [ARCH...]

Architectures:
  x86_64  Desktop Linux (default)
  arm64   ARM64 / AArch64
  armhf   ARM 32-bit hard-float

Options:
  -a, --all      Build all architectures
  -p, --package  Create tar.xz packages
  -c, --clean    Clean build dirs first
  -h, --help     Show help

Examples:
  $0             Build x86_64
  $0 arm64       Build ARM64
  $0 -a -p       Build all + packages
EOF
}

main() {
  local arches=()
  local do_package=false
  local do_clean=false

  while [ $# -gt 0 ]; do
    case $1 in
      -a|--all) arches=(x86_64 arm64 armhf); shift ;;
      -p|--package) do_package=true; shift ;;
      -c|--clean) do_clean=true; shift ;;
      -h|--help) usage; exit 0 ;;
      x86_64|amd64) arches+=("x86_64"); shift ;;
      arm64|aarch64) arches+=("arm64"); shift ;;
      armhf|armv7) arches+=("armhf"); shift ;;
      *) log_error "Unknown: $1"; usage; exit 1 ;;
    esac
  done

  [ ${#arches[@]} -eq 0 ] && arches=("x86_64")

  check_deps

  if [ "$do_clean" = true ]; then
    rm -rf "$BUILD_DIR" "$DIST_DIR"
  fi
  mkdir -p "$DIST_DIR"

  local failed=()
  for arch in "${arches[@]}"; do
    case $arch in
      x86_64) build_arch "x86_64" "" || failed+=("x86_64") ;;
      arm64) build_arch "arm64" "$CONFIG_DIR/cross-arm64.txt" || failed+=("arm64") ;;
      armhf) build_arch "armhf" "$CONFIG_DIR/cross-armhf.txt" || failed+=("armhf") ;;
    esac
  done

  if [ ${#failed[@]} -gt 0 ]; then
    log_warn "Failed: ${failed[*]}"
  fi

  [ "$do_package" = true ] && create_packages

  log_info "Done!"
  find "$DIST_DIR" -maxdepth 3 -type d | sort | sed 's/^/  /'
}

main "$@"