# LITE Music Player

A lightweight, native Windows music player written in C++ with the Win32 API. No frameworks, no web tech — just a single `.cpp` file and a pure Win32 UI.

## Features

- **Audio formats**: MP3, WAV, FLAC, OGG (Vorbis), AAC, M4A, WMA, Opus
- **Decoding**: MediaFoundation (all formats) + stb_vorbis.h (OGG fallback)
- **Output**: WASAPI exclusive/shared mode with automatic sample rate conversion
- **10-band equalizer** with custom spline-based frequency curve
- **Crossfading** between tracks
- **SystemMediaTransportControls (SMTC)** integration — media keys, taskbar playback controls
- **Acrylic blur** background (Windows 10+)
- **Rainbow cycling title text** (toggleable)
- **Peak visualization** bar
- **Playlist browser** with thumbnail extraction (embedded album art)
- **Search/filter** playlist
- **Drag & drop** files/folders
- **Repeat modes**: normal, repeat one, repeat all
- **Volume control** with mute toggle
- **Dark/light theme** support
- **Portable** — settings saved to HKCU registry

## Requirements

- Windows 10 or later (for acrylic blur, SMTC, and modern SDK features)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- Windows 10 SDK (10.0.19041.0 or later)

## Build

### Visual Studio 2022 (IDE)

1. Open `LitePlayerCPP.sln`
2. Select **Release | Win32** from the configuration dropdown
3. **Build → Build Solution** (or `F7`)

### Command line (MSBuild)

```cmd
msbuild LitePlayerCPP.vcxproj /p:Configuration=Release /p:Platform=Win32 /t:Build
```

The output binary is placed in `bin\LitePlayerCPP.exe`.

### Platform notes

- **Win32 (x86) only** — the project is configured for 32-bit builds. To build x64, add a new solution platform in Visual Studio.
- The project uses `v143` platform toolset (VS 2022). For older VS versions, retarget the solution in the IDE.

## Project structure

```
LitePlayerCPP/
├── main.cpp             # Entire application source (~3840 lines)
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

- **Open a folder**: Click the play button or press `Ctrl+O` to browse for a music folder
- **Drag & drop**: Drop audio files or folders directly onto the window
- **Search**: Type in the search box to filter the playlist
- **Equalizer**: Click the EQ button to open the 10-band equalizer window
- **Keyboard shortcuts**: Space (play/pause), Ctrl+O (open folder), media keys supported via SMTC

## Credits

- OGG/Vorbis decoding via [stb_vorbis.h](https://github.com/nothings/stb) by Sean Barrett
- Icon from [SongNoteIcon](https://www.flaticon.com/)

## License

MIT
