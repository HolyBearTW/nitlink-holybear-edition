# In-app HolyBear updates

Settings → About contains one update card, following the interaction of
[Harbor's updater](https://github.com/tavricccc/harbor/blob/2c243bbbaa1bba4f412b6082964ba03eca64b84a/src/Harbor/Services/UpdateService.cs).
The native NitLink and WebView implementation retains the existing capture,
settings and release infrastructure.

1. A newer release adds a dot and localized tooltip to the main window's settings gear.
   Clicking the gear opens Settings on About. Settings never opens automatically at startup.
2. The card displays the current version, available version, download size, release
   date and release notes. Markdown is rendered locally with headings, lists,
   emphasis, code blocks, tables and HTTPS links.
3. Download Update streams the installer in the background and displays progress.
   Cancel, connection errors and failed integrity checks leave the app running and
   allow retry. The capture/Present thread never performs network or file transfers.
4. Only a completed, size- and SHA-256-verified download enables Install Update.
   This button launches the native installer at the current application directory.
   The installer waits for NitLink's normal shutdown, updates the existing files,
   retains settings/account data, and reopens NitLink after success. No release web
   page, file browser or manual installer-file selection is required.

The Windows 11 setup uses its existing glass progress interface in auto-update
mode. Windows 10 and missing-runtime fallback use the existing native NSIS engine.
Normal manual setup retains its existing installation choices.

## Automatic checks and notifications

Automatic checking is **on by default** for new settings. An existing saved false
preference remains false. Checks run at startup and when enabled, at most once
per 24 hours after the last attempt, including failed attempts. Manual checking
bypasses that interval. Downloading and installing always require deliberate
button clicks.

Turning automatic checking off cancels a pending automatic/version check and
immediately suppresses the gear dot and unread update badge. About still allows
manual checking, reading notes, downloading and installing. Turning it off does
not cancel a download the user has already started. Preferences, last attempt and
viewed release tag use the existing atomic configuration save.

## Network and integrity boundaries

Version checks make an unauthenticated HTTPS GET to:

```text
https://api.github.com/repos/HolyBearTW/nitlink-holybear-edition/releases/latest
```

Only non-draft, non-prerelease HolyBear releases with the canonical tag and
release-page URL are accepted. Version ordering compares three/four numeric
components with the exact `-holybear` suffix. Missing revision means zero.

The selected installer must have the expected versioned filename, exact release
download URL in this repository, positive bounded size, and a valid GitHub asset
`sha256` digest. Other repositories, arbitrary filenames, missing hashes and
malformed metadata cannot supply an installable package. API responses have a
15-second budget and 256 KiB limit. Downloads have a 10-minute budget and a
200 MiB size ceiling, with cancellation and phase timeouts.

The initial download URL is the validated repository asset. Redirects are checked
individually and restricted to HTTPS on `github.com`, `api.github.com`,
`release-assets.githubusercontent.com` and `objects.githubusercontent.com`.
Credentials, alternate ports, fragments and other hosts are rejected. Windows'
normal TLS certificate checks remain enabled. No tokens, settings, device IDs,
games or linked-account data are sent. Windows may use the configured proxy.

Installer bytes stream into a private `.partial` file under
`%LOCALAPPDATA%\NitLink\Updates`, with SHA-256 computed during writing. Only the
exact expected size and digest promote that file to Ready. Cancelled or failed
partial files are removed. WebView messages carry operation names, never a
download URL, command line or executable path. The native host selects the
validated release and private cache path. Release publication retains its
separate existing GPG signing and package-verification gates.

## Markdown and WebView

Marked and DOMPurify are pinned and bundled with their licenses. The WebView
does not contact a CDN or GitHub. Markdown output is sanitized once at the HTML
boundary to read-only document elements; scripts, event handlers, images,
forms, embedded content and application action attributes are removed. Existing
CSP, external-navigation policy and the native Ready guard remain in force.
The three new scripts are explicitly included in the native resource allowlist
and package inventory.

## Verification

`holybear_update_checker` uses offline fixtures for release metadata, redirect
policy, streaming writes, SHA-256/length failures, cancellation, retryable network
errors and explicit installer launch through a non-executing test launcher.
The native WebView tests cover Markdown rendering/sanitization, check/download/
progress/install actions, automatic opt-out, localization and native About
navigation. Tests do not install updates into a user's daily installation.
