# MusicWidget

A lightweight Windows desktop widget that displays currently playing music information with animated GIF support.

![Windows](https://img.shields.io/badge/Windows-10%2B-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## Features

- 🎵 **Now Playing Display** - Shows current song title and artist from any media player
- 🖼️ **Album Art** - Displays album cover when available
- 🎞️ **Animated GIFs** - Add custom GIF animations that sync with music playback
- 🖱️ **Draggable** - Position the widget and GIFs anywhere on screen
- 📐 **Resizable GIFs** - Resize GIF windows by dragging edges
- ⚙️ **Settings Menu** - Control GIF visibility, speed, and auto-start
- 💾 **Position Memory** - Remembers widget and GIF positions across sessions
- 🚀 **Auto-start** - Option to start with Windows

## Screenshots

*(Add screenshots here)*

## Installation

### Option 1: Download Release
1. Download the latest release from [Releases](../../releases)
2. Extract the ZIP file
3. Run `MusicWidget.exe`

### Option 2: Build from Source
Requirements:
- Visual Studio 2022 (Build Tools or Community)
- Windows 10 SDK

```batch
git clone https://github.com/yourusername/MusicWidget.git
cd MusicWidget
build.bat
```

## Configuration

### Adding GIFs
1. Place your GIF files in the `assets` folder
2. Edit `assets/config.txt`:
```
gif,your-animation.gif,100,100,150,150
```
Format: `gif,filename,x,y,width,height`

### Settings Menu
Right-click the gear icon (⚙️) to access:
- Show/Hide All GIFs
- GIF Speed (0.5x - 2.0x)
- Start with Windows
- Exit

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| ESC | Minimize widget |
| Shift + Scroll | Zoom GIF (when hovering) |

## System Requirements

- Windows 10 or later
- Any media player that supports Windows Media Transport Controls (Spotify, Windows Media Player, Chrome, etc.)

## How It Works

MusicWidget uses the Windows System Media Transport Controls (SMTC) API to retrieve now-playing information from any compatible media player. GIFs are rendered using GDI+ with per-frame timing for smooth animations.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Acknowledgments

- Uses Windows SMTC API for media information
- GDI+ for GIF rendering
- C++/WinRT for modern Windows API access
