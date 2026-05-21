# GameSurf
Linux browser with gamepad-compatible controls.

## 🎮 Features
- **Gamepad Support**: Full gamepad navigation with analog sticks, triggers, and buttons
- **Virtual Keyboard**: On-screen keyboard for text input (hidden by default)
- **Cursor Emulation**: Analog stick to cursor movement with adjustable sensitivity
- **Video Controls**: Special mode for video playback with gamepad controls
- **Settings Storage**: Persistent settings using GSettings (GNOME)
- **Cross-platform**: Built for ARM and x86 architectures

## 📋 Requirements
- GTK 4.6+
- WebKit GTK 2.40+
- SDL2
- GStreamer 1.0 (optional)
- libinput (optional)
- X11 or Wayland support

## 🔨 Build Instructions

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt install libgtk-4-dev libwebkitgtk-6.0-dev libsdl2-dev meson ninja-build

# Additional optional packages
sudo apt install libxinput-dev libx11-dev libxtst-dev libgstreamer1.0-dev
```

### Compilation
```bash
cd GameSurf
rm -rf build
meson setup build
ninja -C build
```

### Installation
```bash
sudo ninja -C build install
```

This installs:
- Binary to `/usr/local/bin/gamesurf`
- Desktop file to `/usr/local/share/applications/`
- GSettings schema to `/usr/local/share/glib-2.0/schemas/`
- CSS theme to `/usr/local/share/gamesurf/`

## 🚀 Running

After installation:
```bash
gamesurf
```

Or to run from build directory:
```bash
GSETTINGS_SCHEMA_DIR=/usr/local/share/glib-2.0/schemas ./build/gamesurf
```

For local testing or PortMaster-style wrappers, use the bundled launcher:
```bash
./scripts/gamesurf.sh
```

## 🎮 Gamepad Controls

| Button | Action |
|--------|--------|
| **A** | Click / Activate |
| **B** | Back |
| **X** | Toggle mode (cursor/navigation) |
| **Y** | Reload page |
| **L/R Shoulder** | Back / Forward |
| **D-Pad** | Navigate focusable page elements |
| **Analog Sticks** | Cursor movement or page navigation |
| **Start** | Open virtual keyboard for focused web input |
| **Left Stick Press** | Open address bar input |
| **Right Stick Press** | Hide/show browser chrome |
| **Select/Back** | Open settings |

When the virtual keyboard is open:

| Button | Action |
|--------|--------|
| **A** | Press focused key |
| **B / Start** | Close keyboard |
| **X** | Backspace |
| **Y** | Switch keyboard layout |
| **L/R Shoulder** | Move text cursor |
| **D-Pad / Left Stick** | Move between keys |

## ⚙️ Configuration

Settings are stored in GSettings under `org.gamesurf.browser`:

```bash
# View current settings
gsettings list-keys org.gamesurf.browser

# Modify settings
gsettings set org.gamesurf.browser cursor-sensitivity 2.0
gsettings set org.gamesurf.browser stick-deadzone 0.2
gsettings set org.gamesurf.browser homepage 'https://www.example.com'
gsettings set org.gamesurf.browser cursor-speed 2  # 0=slow, 1=normal, 2=fast
```

## 📁 Project Structure
```
GameSurf/
├── src/                 # Source code
│   ├── main.c
│   ├── gs-application.c
│   ├── gs-window.c
│   ├── gs-web-view.c
│   ├── gs-gamepad-manager.c
│   ├── gs-cursor-controller.c
│   ├── gs-virtual-keyboard.c
│   ├── gs-video-controller.c
│   └── gs-settings.c
├── data/                # Data files
│   ├── gamesurf.desktop
│   ├── gamesurf.gschema.xml
│   └── style.css
├── meson.build         # Build configuration
└── README.md          # This file
```

## 🐛 Troubleshooting

### "Settings schema 'org.gamesurf.browser' is not installed"
```bash
# Recompile and reinstall the schema
sudo ninja -C build install
```

### Gamepad not detected
- Ensure your gamepad is connected and recognized by SDL2:
```bash
sdl2-jstest
```

### Style CSS not loading
- Verify that `/usr/local/share/gamesurf/style.css` exists:
```bash
ls -l /usr/local/share/gamesurf/style.css
```

## 📄 License
GPL-3.0

## 🤝 Contributing
Contributions are welcome! Please submit issues and pull requests.

## 📝 Notes
- Built for retro gaming handheld systems (ArkOS, Rocknix, etc.)
- Supports ARM and x86 architectures
- WebKit-based browser engine for web compatibility
