#!/bin/bash
set -euo pipefail

# ============================================================
# GameSurf Master Build Script
# Builds x86_64, arm64, and armhf into dist/{arch}/
# Cross-configs are read from config/ directory
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_DIR/build"
DIST_DIR="$PROJECT_DIR/dist"
CONFIG_DIR="$PROJECT_DIR/config"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step()  { echo -e "${BLUE}[STEP]${NC} $1"; }

# Check dependencies
check_deps() {
    local missing=()
    for cmd in meson ninja pkg-config glib-compile-schemas; do
        if ! command -v "$cmd" &>/dev/null; then
            missing+=("$cmd")
        fi
    done
    if [ ${#missing[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing[*]}"
        echo "Install: sudo apt install meson ninja-build pkg-config libglib2.0-dev-bin"
        exit 1
    fi
}

# Build for a specific architecture
build_arch() {
    local arch=$1
    local cross_file=$2
    local build_subdir="$BUILD_DIR/$arch"
    local dist_arch="$DIST_DIR/$arch"

    log_step "Building for $arch..."

    rm -rf "$build_subdir"
    mkdir -p "$build_subdir"

    local setup_args=(
        --buildtype=release
        --prefix=/usr/local
        -Dstrip=true
    )
    if [ -n "$cross_file" ] && [ -f "$cross_file" ]; then
        setup_args+=("--cross-file=$cross_file")
        log_info "Using cross-file: $cross_file"
    fi

    meson setup "$build_subdir" "$PROJECT_DIR" "${setup_args[@]}"
    ninja -C "$build_subdir"

    # Create dist structure
    mkdir -p "$dist_arch/bin"
    mkdir -p "$dist_arch/data"
    mkdir -p "$dist_arch/lib"

    cp "$build_subdir/gamesurf" "$dist_arch/bin/"
    cp "$PROJECT_DIR/data/style.css" "$dist_arch/data/"
    cp "$PROJECT_DIR/data/gamesurf.desktop" "$dist_arch/data/"
    cp "$PROJECT_DIR/data/gamesurf.gschema.xml" "$dist_arch/data/"

    if command -v glib-compile-schemas &>/dev/null; then
        glib-compile-schemas "$dist_arch/data/"
    fi

    # Universal launcher
    cat > "$dist_arch/GameSurf.sh" << 'LAUNCHER_EOF'
#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export GSETTINGS_SCHEMA_DIR="$SCRIPT_DIR/data"
export GAMESURF_DATA_DIR="$SCRIPT_DIR/data"
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
exec "$SCRIPT_DIR/bin/gamesurf" "$@"
LAUNCHER_EOF
    chmod +x "$dist_arch/GameSurf.sh"

    # PortMaster / ArkOS launcher
    cat > "$dist_arch/launch.sh" << 'PORTMASTER_EOF'
#!/bin/bash
# PortMaster / ArkOS / Rocknix launcher
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
export GAMESURF_DATA_DIR="$(dirname "$0")/data"
export GSETTINGS_SCHEMA_DIR="$(dirname "$0")/data"
export SDL_JOYSTICK_HIDAPI=1
export SDL_HINT_JOYSTICK_HIDAPI_BLUETOOTH=1
exec "$(dirname "$0")/bin/gamesurf" "$@"
PORTMASTER_EOF
    chmod +x "$dist_arch/launch.sh"

    log_info "$arch build complete → $dist_arch/"
}

# Create tar.xz packages
create_packages() {
    log_step "Creating distribution packages..."
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

# Print usage
usage() {
    cat << EOF
Usage: $0 [OPTIONS] [ARCH...]

Build GameSurf for specified architectures.

Architectures:
  x86_64    Desktop Linux (default)
  arm64     ARM64 / AArch64 (RG552, RG505, etc.)
  armhf     ARM 32-bit hard-float (RG351MP, RG351V, RG353V, etc.)

Options:
  -a, --all       Build all architectures
  -p, --package   Create tar.xz packages after build
  -c, --clean     Clean build directories first
  -h, --help      Show this help

Examples:
  $0              Build x86_64 only
  $0 arm64        Build for ARM64
  $0 -a           Build all architectures
  $0 -a -p        Build all and create packages

Cross-compiler install:
  ARM64:  sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
  ARMHF:  sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
EOF
}

main() {
    local arches=()
    local do_package=false
    local do_clean=false

    while [ $# -gt 0 ]; do
        case $1 in
            -a|--all)      arches=(x86_64 arm64 armhf); shift ;;
            -p|--package)   do_package=true; shift ;;
            -c|--clean)     do_clean=true; shift ;;
            -h|--help)      usage; exit 0 ;;
            x86_64|amd64)   arches+=("x86_64"); shift ;;
            arm64|aarch64)  arches+=("arm64"); shift ;;
            armhf|armv7)    arches+=("armhf"); shift ;;
            *)              log_error "Unknown option: $1"; usage; exit 1 ;;
        esac
    done

    [ ${#arches[@]} -eq 0 ] && arches=("x86_64")

    check_deps

    if [ "$do_clean" = true ]; then
        log_step "Cleaning..."
        rm -rf "$BUILD_DIR" "$DIST_DIR"
    fi

    mkdir -p "$DIST_DIR"

    for arch in "${arches[@]}"; do
        case $arch in
            x86_64) build_arch "x86_64" "" ;;
            arm64)  build_arch "arm64" "$CONFIG_DIR/cross-arm64.txt" ;;
            armhf)  build_arch "armhf" "$CONFIG_DIR/cross-armhf.txt" ;;
        esac
    done

    if [ "$do_package" = true ]; then
        create_packages
    fi

    log_info "Done!"
    echo ""
    find "$DIST_DIR" -maxdepth 2 -type d | sort | sed 's/^/  /'
}

main "$@"
