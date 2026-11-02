# NitLink Setup UI

This independent installer interface is distributed under GPL-3.0.
It is adapted from @tavricccc's HarborSetup and shared controls/theme:
https://github.com/tavricccc/harbor/tree/2c243bbbaa1bba4f412b6082964ba03eca64b84a/src/HarborSetup

Copyright: Harbor contributors; NitLink adaptations by @HolyBearTW, 2026-11-02.
Changes: NitLink branding, six installer languages and their fonts, install/update
detection, an install-directory field and folder picker, and integration with
NitLink's existing NSIS install/uninstall engine. The setup and license viewer
also port Harbor's GlassWindow, GlassSurface, SaturationEffect and Motion:
https://github.com/tavricccc/harbor/tree/2c243bbbaa1bba4f412b6082964ba03eca64b84a/src/Harbor/Glass
https://github.com/tavricccc/harbor/blob/2c243bbbaa1bba4f412b6082964ba03eca64b84a/src/Harbor/Controls/Motion.cs

The port incorporates the existing NitLink main-window lifecycle fixes from
src/renderer/window.cpp and window_composition.cpp: create the content without
a redirection bitmap, keep content and material unowned, synchronize placement
before and after native moves without activating either window, explicitly
show the material on restore, suppress native non-client border painting, and
keep the rounded region and highlight within their corrected bounds. Opening,
minimizing and closing retain Harbor's motion curves and durations. Windows'
transparency/high-contrast and animation preferences are respected.
The engine preserves settings, linked accounts, captures and other user files.
The independent NitLink application retains its MIT license.

The UI uses Harbor's programmatic WinUI layout, controls and theme. Lucide icon
paths are covered by the ISC license in LICENSE-Lucide.txt. NitLink artwork and
the application license are preserved separately. There is no warranty.

Corresponding source and build instructions:
https://github.com/HolyBearTW/nitlink-holybear-edition/tree/holybear/integrated/installer/winui
An exact source archive is included at licenses/setup-ui-source.zip in the
portable payload and setup executable, and is retained in the installed folder.
The setup's payload can also be extracted with 7-Zip to read that archive.
Build with .NET 10 SDK and Windows SDK, using scripts/build-installer.ps1.
The WinUI interface requires Windows 11, .NET 10 and Windows App Runtime.
The native setup retains Windows 10 and silent installation support.

Validation on Windows 11 with the matching runtime installed:
scripts/test-winui-installer.ps1 checks the engine handoff and localization.
scripts/test-winui-glass.ps1 compiles the actual setup with a test-only entry
point and exercises install/update/uninstall, moves, focus notifications,
minimize/restore, hide/show and the actual glass license viewer on inactive
desktops. That test entry point is excluded from the shipped setup.
