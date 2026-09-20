# Always On Top

A polished standalone Windows utility for pinning any window above all others — without installing the full PowerToys suite.

## Highlights

- Global custom shortcut (default: **Win + Ctrl + T**)
- Pin multiple windows at once
- Per-window opacity controls
- Default opacity for newly pinned windows
- Border color, thickness and opacity controls
- Windows accent-color support
- Excluded-app manager with **Add active app**
- Custom pin and unpin notification sounds
- Sound preview and on/off toggle
- System tray controls
- Modern Tauri 2 + React interface with Windows Mica support
- Settings persisted per user

## Tech stack

- **Tauri 2**
- **React + TypeScript**
- **Rust**
- Native Windows APIs for topmost windows, opacity, process detection and border overlays

## Build

Requirements:

- Node.js 20+
- Rust stable toolchain
- Windows build tools / Visual Studio C++ workload
- WebView2 (normally included with modern Windows)

```powershell
npm install
npm run tauri build
```

The portable executable is produced under `src-tauri/target/release/` and installers under `src-tauri/target/release/bundle/`.

## Sound design

`public/sounds/pin.wav` and `public/sounds/unpin.wav` are original short notification cues created for this project. The pin cue uses a soft tactile transient with a short rising glass-like chime; the unpin cue is a quieter descending release sound.

## License

MIT.
