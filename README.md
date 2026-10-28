# LITE Music Player

A lightweight, native Windows music player written in C++ with the Win32 API. No frameworks, no web tech — just a single `.cpp` file and a pure Win32 UI.
<img width="805" height="578" alt="image" src="https://github.com/user-attachments/assets/e4a49acf-dfb9-491e-9612-e0d747736d55" />

<img width="805" height="578" alt="image" src="https://github.com/user-attachments/assets/e4a49acf-dfb9-491e-9612-e0d747736d55" />

## Features

### Audio & Playback
- **Audio formats**: MP3, WAV, FLAC, OGG (Vorbis), AAC, M4A, WMA, Opus
- **Decoding**: MediaFoundation (all formats) + stb_vorbis.h (OGG fallback)
- **Output**: WASAPI shared mode with automatic sample rate conversion
- **10-band equalizer** with custom spline-based frequency curve and presets
- **Crossfading** between tracks (gapless transition)
- **Repeat modes**: normal, repeat one, repeat all
- **Volume control** with mute toggle
- **Seek bar** with click-to-seek and drag

### UI & Visuals
- **macOS-style glass blur** — DWM acrylic blur that shows content from behind the window (like macOS frosted glass)
- **Rainbow cycling title bar** with smooth feed-in / feed-out fade animation
- **Peak visualization** bar with cached rendering
- **Modern uniform icons** — all control buttons are the same size with clean, consistent styling
- **Dark / light theme** support
- **Compact mode** — minimal player with track title, time, and playback controls
- **Playlist browser** with embedded album art thumbnails
- **Always-visible search bar** with clear (X) button

### System Integration
- **SystemMediaTransportControls (SMTC)** — appears in Windows Quick Settings / Action Center media panel (like Firefox)
- **Global media hotkeys** — FN + Play/Pause, FN + Next, FN + Previous, FN + Stop work even when the app is not focused
- **AppUserModelID** — registered for proper Windows media session association
- **System tray icon** — minimize to tray instead of taskbar (optional)

### Playlist & Library
- **Drag & drop** files and folders onto the window
- **Search / filter** playlist in real time
- **Metadata caching** — background scanning of ID3 tags, durations, and album art
- **"Load All"** — cache metadata for the entire playlist
- **Context menu** — play, play next, remove, show in Explorer, properties

### Customizability
- **Accent color** picker with 14 preset colors + custom color dialog
- **Blur intensity** slider for acrylic glass effect
- **Window opacity** slider
- **Minimize to tray** instead of taskbar
- **Start in compact mode** option
- **Rainbow title** toggle
- **Settings** saved to Windows registry (portable)

## Bug Fixes

- **Acrylic blur** — removed `WS_EX_LAYERED` which was conflicting with DWM blur; now shows blurred content from behind the window correctly
- **"Load All" showed 0b** — settings window now refreshes to show the actual cached metadata size
- **Changing output device restarted music** — output switch now resumes playback at the same timestamp instead of starting from the beginning
- **Minimize crash** — guards against invalid window dimensions when minimized; no more crash/close on minimize
- **Repeat mode icons** — off / one / all now update correctly when clicking the shuffle button
- **UI lag / 5 FPS** — persistent backbuffer, cached bitmaps for rainbow title, glass overlay, button icons, and visualizer brush; no per-frame GDI object creation
- **Search UI bugs** — always-visible search bar with clear button; fixed row-click indexing for filtered results
- **Rainbow title bar** — smooth fade-in / fade-out instead of snapping on/off; tiles seamlessly across the window
- **Icon size mismatch** — all control buttons now uniformly sized with proportional icons

## Requirements

- Windows 10 or later (for acrylic blur, SMTC, and modern SDK features)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- Windows 10 SDK (10.0.19041.0 or later)

## Build

### Visual Studio 2022 (IDE)

1. Open `LitePlayerCPP.sln`
2. Select **Release | Win32** from the configuration dropdown
3. **Build → Build Solution** (or `F7`)

### Command Line (MSVC from SDK BuildTools)

The project can be built directly with `cl.exe` from the Windows Build Tools:

```cmd
vcvarsall.bat x86
rc.exe /r /fo obj\LitePlayerCPP.res LitePlayerCPP.rc
cl /nologo /EHsc /W3 /O2 /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS main.cpp ^
  /link /SUBSYSTEM:WINDOWS /OUT:bin\LitePlayerCPP.exe obj\LitePlayerCPP.res ^
  user32.lib gdi32.lib advapi32.lib shell32.lib comctl32.lib comdlg32.lib ole32.lib oleaut32.lib uuid.lib shlwapi.lib ^
  gdiplus.lib mfreadwrite.lib mfplat.lib mfuuid.lib mmdevapi.lib runtimeobject.lib propsys.lib dwmapi.lib
```

The output binary is placed in `bin\LitePlayerCPP.exe`.

### Platform Notes

- **Win32 (x86) only** — the project is configured for 32-bit builds. To build x64, add a new solution platform in Visual Studio.
- The project uses `v143` platform toolset (VS 2022). For older VS versions, retarget the solution in the IDE.
- Requires `dwmapi.lib` (Windows 10 SDK) for acrylic blur support.

## Project Structure

```
LitePlayerCPP/
├── main.cpp             # Entire application source (~4500 lines)
├── stb_vorbis.h         # OGG Vorbis decoder (single-header library)
├── LitePlayerCPP.vcxproj  # Visual Studio project file
├── LitePlayerCPP.sln      # Visual Studio solution file
├── LitePlayerCPP.rc       # Resource script (app icon)
├── resource.h             # Resource identifiers
├── songnoteicon.ico       # Application icon
├── bin/                   # Build output directory
└── obj/                   # Intermediate build files
```

**Note**: `bin/` and `obj/` are build artifacts and can be safely deleted.

## Usage

- **Open a folder**: Click the play button to browse for a music folder
- **Drag & drop**: Drop audio files or folders directly onto the window
- **Search**: Type in the search box at the top of the playlist to filter tracks
- **Clear search**: Click the (X) button that appears in the search box
- **Equalizer**: Click the EQ button to open the 10-band equalizer window
- **Compact mode**: Click the widget button to toggle to the compact player
- **Keyboard shortcuts**: Space (play/pause), media keys supported via SMTC
- **Global hotkeys**: FN + Play/Pause, FN + Next, FN + Previous, FN + Stop (works even when app is not focused)
- **Quick Settings**: The player appears in the Windows Quick Settings media panel when playing

## Controls

| Button | Function |
|--------|----------|
| ▶ / ⏸ | Play / Pause |
| ⏹ | Stop |
| ⏮ | Previous track |
| ⏯ | Next track |
| 🔀 | Repeat: Off / One / All |
| ⚙️ | Settings |
| 📍 | Locate current track in playlist |
| 🔊 | Volume (click to mute) |

## Credits

- OGG/Vorbis decoding via [stb_vorbis.h](https://github.com/nothings/stb) by Sean Barrett
- Icon from [SongNoteIcon](https://www.flaticon.com/)
- Acrylic blur via Windows DWM `SetWindowCompositionAttribute`

## License

MIT
