# Always On Top

A lightweight standalone Windows utility for pinning any window above all other windows.

Press **Win + Ctrl + T** by default to toggle the currently active window between normal and always-on-top states. The shortcut can be replaced with your own key combination directly in the app.

## Features

- Global always-on-top hotkey
- Full custom hotkey recorder
- Multiple pinned windows at once
- Smooth configurable border around pinned windows
- Windows accent-color border option
- Custom border color picker
- Border thickness and opacity controls
- Excluded-app list with quick "Add active app"
- Optional sound feedback
- System tray controls
- Persistent per-user settings
- Modern compact glass-inspired interface with Mica on supported Windows 11 systems
- High-DPI aware native Windows executable
- No PowerToys installation or runtime required

## Default shortcut

```text
Win + Ctrl + T
```

Click the shortcut field inside the app, then press any new key combination. Press **Esc** while recording to cancel.

## Usage

1. Run `AlwaysOnTop.exe`.
2. Focus the window you want to keep above others.
3. Press the configured shortcut.
4. Press the shortcut again to unpin it.

Closing the settings window keeps the utility running in the notification area. Use the tray menu to reopen it or exit.

## Build

Requirements:

- Windows 10 or Windows 11
- Visual Studio 2022 Build Tools with **Desktop development with C++**
- CMake

Build locally:

```bat
build-msvc.bat
```

or:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output:

```text
build\Release\AlwaysOnTop.exe
```

## GitHub Actions

Pushes to `main` build the Windows x64 executable automatically. Manual builds are available through **Actions → Build Windows EXE → Run workflow**.

## Relationship to Microsoft PowerToys

This repository is an independent standalone implementation of always-on-top functionality. It does not require or bundle the PowerToys runner, settings application, telemetry, or shared PowerToys components.

Microsoft PowerToys is an open-source Microsoft project. This project is not affiliated with or endorsed by Microsoft.

## License

MIT. See [LICENSE](LICENSE).
