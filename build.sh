#!/bin/bash
# GameSurf - Build and deploy script for ArkOS/Rocknix
# This script sets up the complete build environment and creates the ArkOS distribution

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
DIST_DIR="${PROJECT_DIR}/dist"
ARKOS_DIR="${DIST_DIR}/GameSurf"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}========================================${NC}"
}

print_info() {
    echo -e "${YELLOW}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Step 1: Check dependencies
print_header "Checking Dependencies"
DEPS_OK=true

check_cmd() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 not found"
        DEPS_OK=false
    else
        print_success "$1 found"
    fi
}

check_pkg() {
    if ! pkg-config --exists $1 2>/dev/null; then
        print_error "$1 not found"
        DEPS_OK=false
    else
        print_success "$1 found"
    fi
}

check_cmd meson
check_cmd ninja
check_cmd gcc
check_pkg gtk4
check_pkg webkitgtk-6.0
check_pkg sdl2

if [ "$DEPS_OK" = false ]; then
    print_error "Some dependencies are missing. Please install them and try again."
    exit 1
fi

# Step 2: Clean build directory
print_header "Cleaning Previous Build"
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
    print_success "Cleaned $BUILD_DIR"
fi

# Step 3: Configure Meson
print_header "Configuring Meson Build"
cd "$PROJECT_DIR"
meson setup "$BUILD_DIR" \
    --prefix=/usr/local \
    --wipe

print_success "Meson configured"

# Step 4: Build
print_header "Building GameSurf"
ninja -C "$BUILD_DIR" -j$(nproc)
print_success "Build completed"

# Step 5: Create ArkOS distribution structure
print_header "Creating ArkOS Distribution Structure"
rm -rf "$DIST_DIR"
mkdir -p "$ARKOS_DIR/bin"
mkdir -p "$ARKOS_DIR/cache"
mkdir -p "$ARKOS_DIR/data"

# Copy binary
cp "$BUILD_DIR/gamesurf" "$ARKOS_DIR/bin/"
chmod +x "$ARKOS_DIR/bin/gamesurf"
print_success "Binary copied"

# Copy data files
cp "data/gamesurf.gschema.xml" "$ARKOS_DIR/data/"
cp "data/style.css" "$ARKOS_DIR/data/"
cp "data/gamesurf.desktop" "$ARKOS_DIR/data/"
print_success "Data files copied"

# Create launcher script
cat > "$ARKOS_DIR/gamesurf.sh" << 'EOF'
#!/bin/bash
# GameSurf launcher for ArkOS/Rocknix
# This script sets up the environment and launches GameSurf

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAMESURF_HOME="${SCRIPT_DIR}"
GAMESURF_DATA="${GAMESURF_HOME}/data"
GAMESURF_CACHE="${GAMESURF_HOME}/cache"
GAMESURF_BIN="${GAMESURF_HOME}/bin/gamesurf"

# Create necessary directories
mkdir -p "$GAMESURF_CACHE"

# Setup environment
export HOME="$GAMESURF_HOME"
export XDG_DATA_HOME="$GAMESURF_HOME/data"
export XDG_CACHE_HOME="$GAMESURF_CACHE"
export XDG_CONFIG_HOME="$GAMESURF_HOME/.config"
export GSETTINGS_SCHEMA_DIR="$GAMESURF_DATA"
export SDL_JOYSTICK_HIDAPI=1
export SDL_GAMECONTROLLER_ALLOW_STEAM_NEUTRAL_OUTPUT=1

# Re-compile schemas if needed
if [ ! -f "$GAMESURF_DATA/gschemas.compiled" ] || \
   [ "$GAMESURF_DATA/gamesurf.gschema.xml" -nt "$GAMESURF_DATA/gschemas.compiled" ]; then
    glib-compile-schemas "$GAMESURF_DATA" 2>/dev/null || true
fi

# Launch GameSurf
exec "$GAMESURF_BIN" "$@"
EOF
chmod +x "$ARKOS_DIR/gamesurf.sh"
print_success "Launcher script created"

# Create main launcher for /roms/tools/
cat > "$DIST_DIR/GameSurf.sh" << 'EOF'
#!/bin/bash
# GameSurf main launcher for ArkOS/Rocknix
# Place this in /roms/tools/GameSurf.sh

SCRIPT_DIR="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
GAMESURF_DIR="${SCRIPT_DIR}/GameSurf"

if [ ! -d "$GAMESURF_DIR" ]; then
    echo "Error: GameSurf directory not found at $GAMESURF_DIR"
    exit 1
fi

# Kill any existing instances
pkill -f "gamesurf" 2>/dev/null || true
sleep 1

# Launch GameSurf
"$GAMESURF_DIR/gamesurf.sh" "$@"
EOF
chmod +x "$DIST_DIR/GameSurf.sh"
print_success "Main launcher created"

# Create installation guide
cat > "$DIST_DIR/README_INSTALLATION.md" << 'EOF'
# GameSurf Installation Guide for ArkOS/Rocknix

## Installation

1. **Extract the distribution:**
   ```bash
   tar xzf gamesurf-dist.tar.gz
   ```

2. **Copy to ArkOS:**
   ```bash
   # Copy the GameSurf directory to /roms/tools/
   cp -r GameSurf /roms/tools/
   
   # Copy the launcher script
   cp GameSurf.sh /roms/tools/
   chmod +x /roms/tools/GameSurf.sh
   ```

3. **Verify installation:**
   ```bash
   ls -la /roms/tools/GameSurf/
   ls -la /roms/tools/GameSurf.sh
   ```

4. **PortMaster integration (optional):**
   If using PortMaster, create a metadata file:
   ```bash
   mkdir -p /roms/ports/GameSurf
   cp GameSurf/data/gamesurf.desktop /roms/ports/GameSurf/metadata.json
   ```

## Launch

- **From ArkOS menu:** Navigate to Tools and select "GameSurf.sh"
- **From command line:** `/roms/tools/GameSurf.sh`
- **With custom URL:** `/roms/tools/GameSurf.sh https://example.com`

## Configuration

Settings are stored in `~/.local/share/gamesurf/` (inside the GameSurf directory)

### Keyboard Layouts
Supported: English (en), Russian (ru), German (de), French (fr)

### Gamepad Controls
- **A button:** Click / Activate
- **B button:** Back / Cancel
- **X button:** Toggle cursor/focus mode
- **Y button:** Reload page
- **L shoulder:** Previous page
- **R shoulder:** Next page
- **Start:** Open menu
- **Select:** Toggle keyboard
- **D-Pad:** Navigate
- **Left stick:** Cursor movement or scroll
- **Right stick:** (reserved for future)

## Troubleshooting

### Gamepad not detected
1. Verify gamepad connection: `ls -l /dev/input/js*`
2. Test with SDL: `sdl2-jstest`
3. Check Bluetooth: `bluetoothctl devices`

### No sound
Check ALSA: `aplay -l` or `pactl list short sinks`

### Settings not saving
Ensure cache directory is writable: `chmod -R 777 GameSurf/cache/`

## Building from source

See BUILD.md in the GameSurf repository

## License

GameSurf is licensed under GPL-3.0
EOF

print_success "Installation guide created"

# Create build info file
cat > "$DIST_DIR/BUILD_INFO.txt" << EOF
GameSurf Distribution Build Information
=======================================

Build Date: $(date)
Build System: $(uname -s) $(uname -m)
Target: ArkOS / Rocknix / Similar systems

Compiler: $(gcc --version | head -1)
Meson Version: $(meson --version)
Ninja Version: $(ninja --version)

Dependencies:
- GTK 4.6+
- WebKit GTK 6.0+
- SDL2
- GLib 2.56+

Build Directory: $BUILD_DIR
Distribution Directory: $DIST_DIR

Files included:
- GameSurf/bin/gamesurf (binary)
- GameSurf/data/gamesurf.gschema.xml (GSettings schema)
- GameSurf/data/style.css (Theme)
- GameSurf/gamesurf.sh (Launcher script)
- GameSurf.sh (Main entry point)
- README_INSTALLATION.md (This file)
EOF

print_success "Build information recorded"

# Create package
print_header "Creating Distribution Package"
cd "$DIST_DIR/.."
tar czf "gamesurf-dist-$(date +%Y%m%d).tar.gz" \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='.git' \
    "GameSurf" \
    "GameSurf.sh" \
    "BUILD_INFO.txt" \
    "README_INSTALLATION.md"

DIST_FILE="gamesurf-dist-$(date +%Y%m%d).tar.gz"
print_success "Distribution package created: $DIST_FILE"

# Create checksum
cd "$DIST_DIR/.."
sha256sum "$DIST_FILE" > "${DIST_FILE}.sha256"
print_success "Checksum created: ${DIST_FILE}.sha256"

# Final summary
print_header "Build Complete!"
echo -e "${GREEN}Distribution ready at:${NC}"
echo "  $(pwd)/$DIST_FILE"
echo ""
echo -e "${GREEN}Installation instructions:${NC}"
echo "  1. Extract: tar xzf $DIST_FILE"
echo "  2. Copy GameSurf to /roms/tools/"
echo "  3. Copy GameSurf.sh to /roms/tools/"
echo "  4. Run: /roms/tools/GameSurf.sh"
echo ""
echo -e "${GREEN}For detailed instructions, see: README_INSTALLATION.md${NC}"

print_success "All done!"
