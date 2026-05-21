#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

if [ -x "$APP_DIR/build/gamesurf" ]; then
    BINARY="$APP_DIR/build/gamesurf"
elif [ -x "$APP_DIR/gamesurf" ]; then
    BINARY="$APP_DIR/gamesurf"
else
    BINARY="gamesurf"
fi

if [ -f "$APP_DIR/data/gamesurf.gschema.xml" ] && command -v glib-compile-schemas >/dev/null 2>&1; then
    glib-compile-schemas "$APP_DIR/data"
fi

if [ -f "$APP_DIR/data/gschemas.compiled" ]; then
    export GSETTINGS_SCHEMA_DIR="$APP_DIR/data"
fi

if [ -f "$APP_DIR/data/style.css" ]; then
    export GAMESURF_DATA_DIR="$APP_DIR/data"
fi

export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1

exec "$BINARY" "$@"
