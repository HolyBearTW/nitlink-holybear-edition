# NitLink 1.2.5

A German interface, custom HDMI source names, steadier Source frame rate pacing on noisy capture cards, and fixes for a frozen picture when entering fullscreen and for NitLink opening a camera instead of the capture card.

## Changes

- **German interface** with automatic system-language selection. By Noka123.
- **Custom HDMI source names.** Pick or type a name for what is plugged into the card; it shows in the title bar and in Discord. By HolyBearTW.
- **Steadier Source frame rate pacing on noisy capture cards.** On cards that don't repeat frames pixel-exactly, repeated frames are read as new ones much less often, and small changes in dark, slow scenes are missed less often. Results vary by card.
- **Duplicate detection no longer stalls when the GPU is busy** (for example VSync on with a frame generator running), which made Source frame rate present every captured frame.
- **Fullscreen no longer freezes the picture** when another program, such as an overlay, still holds it while the window changes size. NitLink keeps drawing and retries (#29).
- **Capture cards before cameras.** On first launch, or when the saved device is unplugged, NitLink picks a capture card over a webcam, a VR headset camera or a virtual camera.
- **Simplified Chinese HUD labels** completed. By HolyBearTW.

## Credits

- HolyBearTW: custom HDMI source names with their Discord identity, Simplified Chinese HUD labels (#25).
- Noka123 (github.com/Noka123): German translation (#35).

## Install

Unzip and run NitLink.exe from the new NitLink-1.2.5-win64 folder. To keep your settings, copy nitlink.json from your old NitLink folder into it.

A NitLink version with experimental frame generation (30 fps console games at 60 fps) is in closed beta and coming soon on Steam: https://store.steampowered.com/app/5346980. This free viewer stays free.
