# NitLink 1.2.5

German and Korean interfaces, custom HDMI source names, improved Source frame rate pacing, 4K X HDR detection updates, and fixes for fullscreen freezes and unwanted camera selection.

## Changes

- **German interface** with automatic system-language selection. By Noka123.
- **Korean interface** with automatic system-language selection and Korean fonts in the menu and HUD. By iorph (@i0rph).
- **Custom HDMI source names.** Pick or type a name for what is plugged into the card; it shows in the title bar and in Discord. By HolyBearTW.
- **Improved Source frame rate pacing.** Each duplicate comparison is consumed once. Source-paced presentation uses the current picture's result, with a bounded readback wait and conservative presentation if that result is unavailable.
- **4K X HDR detection.** Valid live source reports now update capture format selection and HDR detection availability, including HDR/SDR changes at unchanged resolution and frame rate. Transient failed HDR reads preserve the last valid state. Manual capture choices and the HDR output preference stay independent.
- **Fullscreen no longer freezes the picture** when another program, such as an overlay, still holds it while the window changes size. NitLink keeps drawing and retries (#29).
- **Capture cards before cameras.** On first launch, or when the saved device is unplugged, NitLink picks a capture card over a webcam, a VR headset camera or a virtual camera.
- **Simplified Chinese HUD labels** completed. By HolyBearTW.

## Credits

- HolyBearTW: custom HDMI source names with their Discord identity, Simplified Chinese HUD labels (#25).
- Noka123 (github.com/Noka123): German translation (#35).
- iorph (github.com/i0rph): Korean translation and localization review (#37).

## Install

Unzip and run NitLink.exe from the new NitLink-1.2.5-win64 folder. To keep your settings, copy nitlink.json from your old NitLink folder into it.

NitLink with built-in frame generation is in development for Steam: https://store.steampowered.com/app/5346980.
