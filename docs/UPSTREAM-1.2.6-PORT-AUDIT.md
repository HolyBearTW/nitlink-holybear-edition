# Upstream 1.2.6 integration audit

## Source

- Upstream: [nitlink-dev/nitlink v1.2.6](https://github.com/nitlink-dev/nitlink/releases/tag/v1.2.6), commit `f39b909517f95791d97e64463fc65e5be7a17fcc`.
- HolyBear integration base: `d805e00a30a0ac52208c08a1448564dcfb9637d1`.
- Ported upstream commits, with original authorship and cherry-pick provenance retained: `131e6a362b3181e8efbec06a8c9b994852a97617`, `91bcd619348f6bff961c7d5cce5b4bc04c1b4959`, `d0cf5666e1f5363e40e41137028ac48899ffd1fe`.
- All 53 changed upstream paths between v1.2.5 and v1.2.6 were classified in `out/upstream-1.2.6-integration-20261117/upstream-file-inventory.tsv` and its JSON counterpart. Website redesign, website fonts and Steam promotional assets are outside this application port.

## Integrated behavior

- Optional gameplay cursor hiding defaults to off and persists in configuration. The port handles HolyBear's video child window and excludes the custom caption. Settings, PiP, unavailable video, mouse capture and inactive windows retain the pointer. The independent settings window clears the gameplay settings flag when closed.
- Turkish has an independent, complete menu table and native table, including HolyBear games, artwork, linked accounts, Discord, updates, shortcuts and HUD. Verified Turkish title metadata is accepted; unrelated language metadata retains the original game name.
- Both WinUI and native NSIS installers support English, Traditional Chinese, Simplified Chinese, German, Japanese, Korean and Turkish. Native/WinUI handoffs preserve language IDs. The installer reads the main application's actual configuration format and retains its existing JSON compatibility.
- Every language has 657 menu, 106 native, 32 WinUI installer and 36 NSIS installer strings. Key coverage, nonempty values and placeholders are checked for each language. Existing language omissions in the native installer and German store text were completed; support-link text now identifies Buy Me a Coffee in all languages.
- @karsiyakali1912 appears in the existing contributor-card design. Its exact GitHub profile and avatar URLs, PR #48, Issue #41 and the upstream 1.2.6 source link are in the native allowlist. The audit checked 15 packaged script URLs, 16 HTML links, seven release-note links and all eight contributor profiles.
- VERSION, CMake, Windows resources, native update checking, HUD, About, package readme, release notes and source records identify `1.2.6-holybear`, with Windows numeric version `1.2.6.0`. All seven About descriptions identify NitLink 1.2.6 as their base.

## Toolchain

The pre-existing VS 2026 migration was necessary because the previous scripts required VS 2022 and VC143. The integrated scripts select VS 18.x, prefer Stable, and require Stable VC145 runtime files for packaging. Both workflows explicitly select `windows-2025-vs2026`.

Local validation uses VS 2026 Stable 18.12, MSVC 19.52 and the Windows 10.0.26100 SDK. Setting both console encodings to UTF-8 after environment initialization fixes CMake's localized include-prefix detection. A local-header probe confirmed Ninja records the header dependency and schedules recompilation when it changes. No personal toolchain path was added to project configuration.

## Validation

| Check | Result |
| --- | --- |
| Complete unfiltered CTest | 63/63 passed; failures 0, skipped 0; 359.89 seconds |
| Packaged settings readiness | All five required native tests passed before building the release application |
| JavaScript syntax and all seven locale catalogs | Passed |
| WinUI installer backend | 67 checks passed |
| Production WinUI glass lifecycle | 21 screens, 3,738 assertions passed on isolated desktops |
| Release-note regression | 66 checks passed |
| Public-key signing preflight | Seven tests passed |
| Package privacy boundary | Passed |
| NSIS automatic-update handoff | Passed, including custom Unicode path, one relaunch, payload hash and retained settings/account vault |

The validated implementation/test commit is `ce17e284fd44c180948d27df0191f0dadd5c5fa3`; subsequent bookkeeping changes only this audit and release-process documentation. CTest XML/logs are retained in `out/release-x64-20261117-v126-final/`; installer reports and the resource/link audit are under `out/upstream-1.2.6-integration-20261117/`. The formal tag attests to the successful unfiltered result. Packaging, detached signatures and final installer validation use that source and its validated application artifacts.

This integration preserves the existing HolyBear capture, HDR/VRR, audio, game, artwork, Discord and window behavior. It does not add the proprietary Steam frame-generation engine. This release did not repeat the earlier GC553Pro/Switch 2 hardware acceptance; automated cursor/configuration/bridge checks do not establish new capture-card hardware measurements.
