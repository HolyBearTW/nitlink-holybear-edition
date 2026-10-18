# Upstream 1.2.5 Runtime Port Audit

## Source baseline

- Upstream repository: **nitlink-dev/nitlink**
- Official release tag: **v1.2.5**
- Tag target commit: **9fd614a2ffa871b50ccd27070667985e1356b18a**
- Local upstream tracking branch after the approved fast-forward: **upstream/main** at **19c011c235cb3457bfa54c789516205c3e6e9fc0**
- HolyBear integration base for this port: **632116ce5cf349cdf5c897b0dbbedefbbbbe7eb2**
- Product version remains **1.2.5-holybear**.

The requested runtime commits were present in the official v1.2.5 history and verified as ancestors of the tag target:

- Source FPS / readback: **9024e3ad56b6bdaa0b7b0a8324dbef0510f2e8d9**, **a01eaddff5fa3abba454ca456158797224ee71e8**, **e1a36ef194a733d865772fd6a91c0de975cbf18c**
- DXGI resize recovery: **ef17e856c121b39de74e0910f4d367d8daa89b96**
- Capture-device selection: **7716f3535888087eb11866616c727b77bf555ba9**

The official v1.2.5 tag resolves to the expected commit, but git tag verification reports that the upstream tag has no signature. The source identity was checked against the exact tag target SHA and commit ancestry; this audit does not claim an upstream GPG signature.

## Ported behavior

### Source FPS and GPU readback

- Replaced the differ's opportunistic ring-slot fallback with an ordered, bounded FIFO of readback results. A result is consumed at most once and in submission order.
- Kept the existing adaptive noise-floor estimator and the HolyBear tile-maximum motion-rescue behavior. The upstream deadband classifier continues a recent alternating cadence only when the last two votes alternate.
- Added current-frame classification state. A delayed or unknown GPU result does not suppress presentation of the current capture frame; cadence and content-FPS counters use only an available classification.
- Preserved the low-latency boundary: only Source-frame-rate pacing can request a bounded wait, capped at 8 ms per frame. Other pacing modes poll without waiting. Starved Source pacing discards stale comparisons and retries against the current frame.
- Added cumulative readback wait and timeout counters to the existing differ diagnostics.

### DXGI resize recovery

- DX11Renderer::Resize now reports success. A refused ResizeBuffers restores the previous dimensions and render target so the old swap-chain buffers keep rendering.
- The failed resize remains pending. The run loop retries after 250 ms, 500 ms, 1 s, then at most every 2 s; a changed client size is retried immediately. The window acknowledges the resize only after success.
- A refused HDR swap-chain format change restores the render target for the existing format.
- Kept the existing overlay release/rebind order, WebView2 bounds update, and device-loss notification path.

### Capture-device automatic selection

- A saved device still takes precedence.
- Automatic selection now prefers a non-camera Elgato device, then another recognized non-camera capture card, then an unrecognized non-camera device. It falls back to the first enumerated device only if every device looks like a camera.
- The same capture-card/camera name policy is used by the device list filter. Manual device selection remains unchanged.
- The regression test includes GC553Pro/AVerMedia capture-card selection alongside camera and fallback cases.

## Scope and compatibility

- No official application.cpp, configuration, menu, Discord, localization, or game-library file was copied wholesale. HolyBear-specific game, Discord, WebView, HDR/VRR, audio, and locale behavior remains on the integration base.
- The separate Elgato 4K X automatic HDR change from **ddc5280971de87809881fb8ea85112d14c8bd588** was not ported because upstream Issue #36 reports SDR/HDR misclassification. Existing HolyBear 4K X tone-mapping behavior was left intact.
- The new CTest cases are registered with their upstream behavior and are included in the existing full label. No existing tests or assertions were removed.

## Validation

- CMake configure: Windows x64, MSVC 19.44, Ninja, Release, BUILD_TESTING=ON; isolated output directory **out/upstream-1.2.5-runtime-port-release-x64**.
- Release build: passed, 214/214 build steps.
- Complete unfiltered CTest: passed, 58/58; 367.10 seconds, one test process at a time.
- Locale validation: passed; 451 menu strings and 86 native strings checked in all six locales (English, Traditional Chinese, Simplified Chinese, German, Korean, Japanese).
- git diff --check: passed.
- The seven added CTest cases are **frame_readback_queue**, **frame_differ_order**, **bounded_readback**, **current_frame_pacing**, **readback_starvation**, **swap_chain_resize_recovery**, and **device_pick**.

## Not verified on hardware

This validation did not include a live GC553Pro/Switch 2 hardware session. Real-device checks remain necessary for automatic device selection, 30 fps and 60 fps source pacing, HDR/SDR, P010/NV12, high GPU load, window resize under NVIDIA Overlay, and the observed end-to-end latency. The 8 ms bound is a code-level maximum for a permitted Source-pacing readback wait, not a measured hardware latency result.
