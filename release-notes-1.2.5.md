# NitLink 1.2.5

German and Korean interfaces, custom HDMI source names, improved Source frame rate pacing and capture-device selection, and fullscreen resize recovery.

## Changes

- **German and Korean interfaces**, including automatic system-language selection and Korean menu/HUD fonts.
- **Custom HDMI source names.** Pick or type a name for the device connected to the card; NitLink also uses it in Discord.
- **Improved Source frame rate pacing.** Duplicate decisions stay aligned with the captured picture, improving presentation timing when a 30 fps game arrives through a 60 fps capture stream.
- **Improved 4K X HDR detection and capture-format selection.** Valid source HDR reports reach capture selection and settings. Failed HDR reads retain the last valid state. Manual capture choices and the HDR output preference remain independent.
- **Improved fullscreen resize recovery (#29).** NitLink keeps drawing and retries when another application or overlay prevents a resize.
- **Capture cards before cameras.** Automatic device selection prefers capture cards and skips obvious webcams, VR headset cameras and virtual cameras.
- **Simplified Chinese HUD labels** completed, and all four translators listed in About and the README.

## Contributors

- [HolyBearTW](https://github.com/HolyBearTW): Traditional Chinese, custom HDMI source names and Simplified Chinese HUD labels.
- [CN_115](https://github.com/CN115): Simplified Chinese.
- [Noka123](https://github.com/Noka123): German.
- [iorph (@i0rph)](https://github.com/i0rph): Korean.

Full contributor and research credits are in ACKNOWLEDGMENTS.md.

## Install

Extract NitLink-1.2.5-win64.zip into a new writable folder and run NitLink.exe. To keep existing settings, copy nitlink.json from the previous NitLink folder into the new one.

## Hardware confirmation

The 4K X HDR correction still awaits confirmation on the reported Switch 2 setup. Issue #36 remains open for that check.

NitLink with built-in frame generation is in development for [Steam](https://store.steampowered.com/app/5346980/).
