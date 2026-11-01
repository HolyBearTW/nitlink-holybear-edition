NitLink 1.2.5.3 HolyBear Edition (1.2.5.3-holybear)
Unofficial community integration build based on NitLink 1.2.5.
Not an official nitlink-dev release.

Extract the complete ZIP into a writable folder, then run NitLink.exe.
Keep its HTML, assets, locales and third_party directories together.
Extract before launching; running inside an archive can discard settings
with its temporary folder when the archive program closes.
No personal nitlink.json or capture-device preset is included. Settings are
created on first launch in the working directory; launching from Explorer
uses the application's folder. No separate development checkout is required.

Alternatively run the multilingual Setup.exe. It installs under your user's
standard Programs directory, creates Start menu shortcuts, and offers optional
desktop shortcut and launch. The settings interface supports English,
Traditional Chinese, Simplified Chinese, German, Japanese and Korean, with a
manual selector. The Setup wizard is available in English and both Chinese variants.
Upgrades and uninstall retain your settings and other generated user files.
The installer does not migrate or change an existing original NitLink installation.

Requirements: Windows 10 1809+ / Windows 11 x64, DirectX 11 GPU, suitable
capture/audio drivers, and Microsoft Edge WebView2 Evergreen Runtime.
Required release Microsoft VC++ DLLs are included with their notices.

F1 opens settings. Choose capture device/format and language there.
Ctrl+F3 toggles the FPS HUD; Alt+L toggles low latency; Alt+V toggles VSync;
Tab cycles Standard HUD, Detailed HUD and the transparent compact text row
while the HUD is visible and settings are closed. In Settings > Shortcuts,
select which compact metrics to show, their text color and size (10-32 px).
The compact row defaults to green text. These controls
appear only in compact mode. Hover over a metric for its definition.
The HUD selector also offers Off and reflects the actual visibility state.
Selecting a visible mode enables the HUD. Metric selection, color and size
apply on the next drawn frame in both SDR and HDR.
The keys listed here are defaults. Settings > Shortcuts supports recording
custom combinations. Save applies and persists changed bindings; Reset
restores the default draft, which takes effect after Save. Duplicate or
reserved system combinations cannot be saved. Shortcuts remain foreground-only.
Save and Reset are in the Shortcuts heading; the information button explains
recording and foreground-only operation. Wide Full mode places shortcuts and
HUD settings in two columns. The HUD text-size slider matches PiP opacity.
Video > Panel includes opacity above panel position. It defaults to 100%,
supports 10-100%, applies while dragging and is saved on normal exit. Lower
opacity reveals the playing picture in Full and either docked position.
F11 / Alt+Enter toggles fullscreen. Audio defaults to shared mode, drift
correction on, 12 ms FIFO and 10 ms render targets. Optional exclusive output
requests the sound device's supported minimum period; it falls back to shared
on focus loss and returns after stable foreground. Changes can briefly
restart audio. Buffer targets and diagnostics are not measured end-to-end latency.

F1 About includes Software Updates. Automatic checks are OFF by default.
Enable them to allow one background GitHub check per 24 hours, or check manually.
Only HolyBearTW/nitlink-holybear-edition's formal Releases are queried; no token, personal settings,
device IDs or game information is sent. View/Download opens the release webpage;
there is no automatic download or installation. See docs/HOLYBEAR-UPDATES.md.

To link Xbox, choose Open sign-in page and sign in with your personal
Microsoft account in the system browser. NitLink receives the result and
syncs your games automatically. No application registration or code entry
is required from the user.

Nintendo linking opens the system browser. Sign in, select your account,
and allow the browser to open NitLink when prompted. The result returns to
the running application automatically, which then syncs Play Activity.

PlayStation linking prefers your default Edge/Chrome, or uses another
installed Edge/Chrome, with its own temporary browser profile. Sign in to
Sony there; NitLink receives the
NPSSO session automatically and syncs PS4/PS5 play history. Edge or Chrome
must be installed. The ordinary browser profile is not used for this flow.

Nintendo and PlayStation also offer Enter link code manually if automatic
linking is unavailable. Automatic attempts offer Cancel and expire after
five minutes. Closing the link dialog cancels a pending attempt. For a
Nintendo/Xbox system-browser tab, use Cancel if you close only the tab.

When the console source is still Auto, NitLink selects the linked platform
with the most games, including accounts already saved at startup. A manual
source choice is kept. Automatic selection shows a brief notification.

Local acceptance checks still needed: complete each automatic sign-in and
confirm game sync; try cancellation, timeout and retry; close NitLink while
waiting; verify Nintendo callback ownership and the isolated Sony browser
cleanup; check repeated callbacks, occupied local ports and both manual
alternatives. BUILD.txt identifies checks actually run.

Present FPS is successful DXGI submissions, not game FPS or scan-out rate.
Content FPS is an estimate. Actual capture rate and monitor refresh are separate.

The window title bar reserves its own area above the game. Its gear opens
F1; its HUD icon cycles Standard, Detailed and Compact, or restores the
stored mode when the HUD is hidden. Fullscreen and PiP hide the title bar.
Supported Windows 11 systems use a separate desktop-blurred glass surface;
older systems use a solid dark title bar.
NitLink draws the single caption row, including the transparent upstream
lockup and window buttons. Those buttons issue normal Windows system
commands. Title text follows the effective F1 locale's font, including
System language; Archivo and the logo are embedded in the executable.
Window motion follows Harbor's separate glass/content design and motion
tokens. The glass springs from 94% to full size over 360 ms, while glass and
content fade in over 220 ms. Minimize/close shrink the glass and fade both
layers over 120 ms. Maximize/restore apply the final content layout once,
then glide the rounded clipping boundary over 180 ms. Content never scales
with the glass. Animations follow Windows' system animation preference.
NitLink uses native Windows Composition for the glass and caption without
adding a WinUI runtime or replacing the game's existing HWND swap chain.
F1 follows the picture HWND. Fullscreen and PiP retain their existing behavior.

Video > Panel > Panel background selects Blur glass (default) or Classic
transparency. Glass blurs the game behind F1 and changes only background
opacity; classic keeps the existing whole-panel transparency. Each mode
remembers its own opacity. F1 is a child of the same picture window that
owns the swap chain, so transparent areas reveal the blurred picture.
BUILD.txt records that this acceptance revision
was compiled without CTest or regression tests; visual review is pending.

BUILD.txt records the exact build commit/tag and integration sources.
SHA256SUMS.txt records package file hashes; the separate release checksum file
records ZIP, Setup and release-note hashes. Download their GPG signatures and
verify the full trusted fingerprint before installing; see
docs/HOLYBEAR-RELEASE-SIGNING.md and licenses/HOLYBEAR-RELEASE-PUBLIC-KEY.asc.
The executable/installer is not Authenticode-signed. GPG does not remove
SmartScreen warnings or prevent Windows Defender false positives.

Read RELEASE-NOTES.md for test results and hardware limitations.
Read LICENSE, LICENSES.md, ACKNOWLEDGMENTS.md and docs/cn115 for attribution.
Original NitLink: https://github.com/nitlink-dev/nitlink
Audio and Simplified Chinese by CN_115: https://github.com/CN115/nitlink-lowlatency
HolyBear Edition releases/support: https://github.com/HolyBearTW/nitlink-holybear-edition
