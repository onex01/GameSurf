#!/bin/bash
# =============================================================================
#  GameSurf.sh — ArkOS / Rocknix / JelOS / AmberELEC launcher
#
#  Directory layout (portable, lives entirely under /roms/tools/):
#
#  /roms/tools/
#  |--- GameSurf.sh          ← this script
#  |--- GameSurf/
#           |--- gamesurf    ← compiled binary
#           |--- bin/        ← runtime libs / helpers (optional)
#           |--- cache/      ← disk cache, cookies, history
#
#  Usage: just run GameSurf.sh from the PortMaster / tools menu.
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# 1. Locate ourselves (works whether called from a symlink or directly)
# ---------------------------------------------------------------------------
SCRIPT_PATH="$(readlink -f "$0")"
TOOLS_DIR="$(dirname "$SCRIPT_PATH")"        # /roms/tools
GAMESURF_DIR="${TOOLS_DIR}/GameSurf"
BINARY="${GAMESURF_DIR}/gamesurf"
BIN_DIR="${GAMESURF_DIR}/bin"
CACHE_DIR="${GAMESURF_DIR}/cache"
DATA_DIR="${CACHE_DIR}/data"
COOKIES_DIR="${CACHE_DIR}/cookies"
LOG_FILE="${CACHE_DIR}/gamesurf.log"

# ---------------------------------------------------------------------------
# 2. Sanity checks
# ---------------------------------------------------------------------------
if [[ ! -f "$BINARY" ]]; then
    echo "[GameSurf] ERROR: binary not found at ${BINARY}" >&2
    # Try to show an on-screen error via 'dialog' if available
    command -v dialog &>/dev/null && \
        dialog --msgbox "GameSurf binary not found!\n${BINARY}" 8 60
    exit 1
fi

if [[ ! -x "$BINARY" ]]; then
    chmod +x "$BINARY"
fi

# ---------------------------------------------------------------------------
# 3. Create required directories
# ---------------------------------------------------------------------------
mkdir -p "${BIN_DIR}"
mkdir -p "${DATA_DIR}"
mkdir -p "${COOKIES_DIR}"
# Keep the log to a sane size
if [[ -f "$LOG_FILE" ]] && [[ $(wc -c < "$LOG_FILE") -gt 524288 ]]; then
    tail -c 262144 "$LOG_FILE" > "${LOG_FILE}.tmp" && mv "${LOG_FILE}.tmp" "$LOG_FILE"
fi

# ---------------------------------------------------------------------------
# 4. Detect firmware / architecture
# ---------------------------------------------------------------------------
ARCH="$(uname -m)"
HOSTNAME_LOWER="$(hostname | tr '[:upper:]' '[:lower:]')"
FIRMWARE="unknown"

# ArkOS: hostname starts with 'ark' or there is /opt/system/Tools
if [[ -d /opt/system/Tools ]] || echo "$HOSTNAME_LOWER" | grep -q '^ark'; then
    FIRMWARE="arkos"
fi
# Rocknix
if [[ -d /storage/.config/rocknix ]]; then
    FIRMWARE="rocknix"
fi
# JelOS / AmberELEC share similar paths
if [[ -d /storage/.config/amberelec ]]; then
    FIRMWARE="amberelec"
fi
if [[ -d /storage/.config/jelos ]]; then
    FIRMWARE="jelos"
fi

echo "[GameSurf] Firmware: ${FIRMWARE}  Arch: ${ARCH}" | tee -a "$LOG_FILE"

# ---------------------------------------------------------------------------
# 5. Set up the runtime library path
# ---------------------------------------------------------------------------
# If the bin/ directory contains companion .so files, prepend it.
if [[ -d "$BIN_DIR" ]] && ls "${BIN_DIR}"/*.so* &>/dev/null 2>&1; then
    export LD_LIBRARY_PATH="${BIN_DIR}:${LD_LIBRARY_PATH:-}"
fi

# ---------------------------------------------------------------------------
# 6. Display server — pick Wayland or X11
# ---------------------------------------------------------------------------
# ArkOS / older firmwares use X11
# Rocknix / JelOS typically have Wayland via weston / gamescope

DISPLAY_BACKEND=""

if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    DISPLAY_BACKEND="wayland"
    echo "[GameSurf] Using Wayland: ${WAYLAND_DISPLAY}" | tee -a "$LOG_FILE"
elif [[ -n "${DISPLAY:-}" ]]; then
    DISPLAY_BACKEND="x11"
    echo "[GameSurf] Using X11: ${DISPLAY}" | tee -a "$LOG_FILE"
else
    # Try to start a minimal X server on :1 if nothing is running
    if command -v Xorg &>/dev/null; then
        echo "[GameSurf] No display found — starting Xorg :1" | tee -a "$LOG_FILE"
        Xorg :1 -nolisten tcp &
        XORG_PID=$!
        export DISPLAY=":1"
        sleep 1.5
        DISPLAY_BACKEND="x11"
        # Clean up Xorg when we exit
        trap "kill ${XORG_PID} 2>/dev/null || true" EXIT
    elif command -v weston &>/dev/null; then
        echo "[GameSurf] No display found — starting weston" | tee -a "$LOG_FILE"
        weston --backend=fbdev-backend.so &
        WESTON_PID=$!
        export WAYLAND_DISPLAY="wayland-1"
        sleep 2
        DISPLAY_BACKEND="wayland"
        trap "kill ${WESTON_PID} 2>/dev/null || true" EXIT
    else
        echo "[GameSurf] ERROR: no display server available!" | tee -a "$LOG_FILE"
        exit 1
    fi
fi

# Force GTK to use the right backend
if [[ "$DISPLAY_BACKEND" == "wayland" ]]; then
    export GDK_BACKEND="wayland,x11"
else
    export GDK_BACKEND="x11"
    export GDK_SCALE="${GDK_SCALE:-1}"
fi

# ---------------------------------------------------------------------------
# 7. SDL hints for Bluetooth gamepad reliability
# ---------------------------------------------------------------------------
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
export SDL_GAMECONTROLLERCONFIG_FILE="${CACHE_DIR}/gamecontrollerdb.txt"

# Download / update SDL controller database if curl is available
GCDB="${CACHE_DIR}/gamecontrollerdb.txt"
if [[ ! -f "$GCDB" ]] && command -v curl &>/dev/null; then
    echo "[GameSurf] Downloading SDL gamecontrollerdb..." | tee -a "$LOG_FILE"
    curl -sL --max-time 10 \
        "https://raw.githubusercontent.com/gabomdq/SDL_GameControllerDB/master/gamecontrollerdb.txt" \
        -o "${GCDB}.tmp" && mv "${GCDB}.tmp" "$GCDB" || true
fi

# ---------------------------------------------------------------------------
# 8. GSettings schema — portable, no system install required
# ---------------------------------------------------------------------------
SCHEMA_DIR="${GAMESURF_DIR}/schemas"
if [[ -d "$SCHEMA_DIR" ]]; then
    export GSETTINGS_SCHEMA_DIR="$SCHEMA_DIR"
    # Compile the schema if the .compiled file is missing or stale
    if [[ ! -f "${SCHEMA_DIR}/gschemas.compiled" ]] || \
       [[ "${SCHEMA_DIR}/org.gamesurf.browser.gschema.xml" -nt \
          "${SCHEMA_DIR}/gschemas.compiled" ]]; then
        echo "[GameSurf] Compiling GSettings schema..." | tee -a "$LOG_FILE"
        glib-compile-schemas "$SCHEMA_DIR" 2>>"$LOG_FILE" || true
    fi
fi

# ---------------------------------------------------------------------------
# 9. WebKit / cache environment
# ---------------------------------------------------------------------------
export WEBKIT_DISABLE_COMPOSITING_MODE=1    # safer on ARM without GPU driver
export WEBKIT_FORCE_SANDBOX=0               # retro devices lack namespaces
export XDG_CACHE_HOME="${CACHE_DIR}"
export XDG_DATA_HOME="${DATA_DIR}"
export XDG_CONFIG_HOME="${DATA_DIR}/config"
mkdir -p "${XDG_CONFIG_HOME}"

# ---------------------------------------------------------------------------
# 10. Pass the portable cache dir to the binary via env var
#     (gs-application.c reads GS_CACHE_DIR and passes it to the data manager)
# ---------------------------------------------------------------------------
export GS_CACHE_DIR="$CACHE_DIR"
export GS_DATA_DIR="$DATA_DIR"

# ---------------------------------------------------------------------------
# 11. Launch!
# ---------------------------------------------------------------------------
echo "[GameSurf] Launching ${BINARY} ..." | tee -a "$LOG_FILE"
exec "$BINARY" "$@" 2>>"$LOG_FILE"
