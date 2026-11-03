# Exercise the real NSIS update handoff with a tiny marker app, never a user's NitLink.
. "$PSScriptRoot/holy-bear-common.ps1"
. "$PSScriptRoot/get-nsis.ps1"
Initialize-HolyBearToolchain
$info = Get-HolyBearVersion
$fixtureId = [guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path $HolyBearRoot "out/update-installer-check-$fixtureId"
$target = Join-Path $fixtureRoot '既有安裝 with spaces'
$fixtureProduct = "NitLink Update Check $fixtureId"
New-Item -ItemType Directory -Path $target | Out-Null
$fixture = Join-Path $fixtureRoot 'fixture.exe'
$source = Join-Path $fixtureRoot 'fixture.cpp'
[IO.File]::WriteAllText($source, @'
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command, int) {
    if (std::wstring_view(command) == L"--hold") { Sleep(1800); return 0; }
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto directory = std::filesystem::path(executable).parent_path();
    if (std::wstring_view(command).starts_with(L"--engine")) {
        int count = 0;
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        std::filesystem::path target;
        for (int i = 1; i < count; ++i) if (std::wstring_view(args[i]) == L"--directory") target = args[++i];
        LocalFree(args);
        const int length = WideCharToMultiByte(CP_UTF8, 0, command, -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, command, -1, utf8.data(), length, nullptr, nullptr);
        std::ofstream(target / "ui-forwarded.txt") << utf8.c_str();
        return 1; // Missing WinUI runtime: exercise the real silent native fallback.
    }
    std::ofstream(directory / "launches.txt", std::ios::app) << "launch\n";
    return 0;
}
'@, [Text.UTF8Encoding]::new($false))
$setup = Join-Path $fixtureRoot 'UpdateCheck.exe'
$regKey = "Software\NitLinkSetupChecks\$fixtureId"
$uninstallKey = "Software\Microsoft\Windows\CurrentVersion\Uninstall\NitLink-UpdateCheck-$fixtureId"
$passed = $false
try {
    & cl.exe /nologo /std:c++20 /EHsc /utf-8 /O1 /MT "/Fe$fixture" "/Fo$fixtureRoot/fixture.obj" $source shell32.lib /link /SUBSYSTEM:WINDOWS *> (Join-Path $fixtureRoot 'fixture-build.log')
    if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $fixtureRoot 'fixture-build.log'); throw 'Update installer marker fixture did not compile.' }
    $license = Join-Path $fixtureRoot 'LICENSE.rtf'
    [IO.File]::WriteAllText($license, '{\rtf1\ansi Synthetic installer fixture}')
    $install = Join-Path $fixtureRoot 'install.nsh'
    [IO.File]::WriteAllText($install, 'File /oname=NitLink.exe "' + $fixture + '"')
    $uninstall = Join-Path $fixtureRoot 'uninstall.nsh'
    [IO.File]::WriteAllText($uninstall, 'Delete "$INSTDIR\NitLink.exe"')
    $compiler = Get-HolyBearNSIS
    & $compiler /V2 /WX /INPUTCHARSET UTF8 "/DPRODUCT=$fixtureProduct" "/DREGKEY=$regKey" "/DUNINSTALLKEY=$uninstallKey" "/DVERSION=$($info.Version)" "/DNUMERIC_VERSION=$($info.Numeric)" "/DWINDOWS_VERSION=$($info.WindowsVersion)" /DPACKAGE_KB=1 "/DPACKAGE_DIR=$fixtureRoot" "/DWINUI_SETUP=$fixture" "/DLICENSE_RTF=$license" "/DOUTPUT_FILE=$setup" "/DINSTALL_FILES=$install" "/DUNINSTALL_FILES=$uninstall" (Join-Path $HolyBearRoot 'installer/holy-bear.nsi')
    if ($LASTEXITCODE -ne 0) { throw 'Update handoff NSIS fixture did not compile.' }
    [IO.File]::WriteAllText((Join-Path $target 'nitlink.json'), '{"synthetic_settings":true}')
    [IO.File]::WriteAllText((Join-Path $target 'linked-accounts.dat'), 'synthetic-account-vault')
    $holder = Start-Process -FilePath $fixture -ArgumentList '--hold' -WindowStyle Hidden -PassThru
    $operation = Start-Process -FilePath $setup -ArgumentList "/S --auto-update /WAITPID=$($holder.Id) /LANG=1028 /DESKTOP=0 /D=$target" -WindowStyle Hidden -Wait -PassThru
    if ($operation.ExitCode -ne 0 -or -not $holder.HasExited) { throw "Native auto-update wait failed: setup exit=$($operation.ExitCode); previous process exited=$($holder.HasExited)." }
    $launches = Join-Path $target 'launches.txt'
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not (Test-Path -LiteralPath $launches) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
    if (@(Get-Content -LiteralPath $launches).Count -ne 1) { throw 'Native update did not relaunch exactly once.' }
    $operation = Start-Process -FilePath $setup -ArgumentList "--auto-update /LANG=1028 /DESKTOP=0 /D=$target" -WindowStyle Hidden -Wait -PassThru
    if ($operation.ExitCode -ne 0) { throw 'WinUI fallback auto-update failed.' }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (@(Get-Content -LiteralPath $launches).Count -lt 2 -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
    if (@(Get-Content -LiteralPath $launches).Count -ne 2) { throw 'WinUI fallback did not relaunch exactly once.' }
    if ([Environment]::OSVersion.Version.Build -ge 22000) {
        $uiArgs = Get-Content -LiteralPath (Join-Path $target 'ui-forwarded.txt') -Raw
        if (-not $uiArgs.Contains('--auto-update') -or -not $uiArgs.Contains('--directory "' + $target + '"')) { throw 'Native setup did not forward auto-update and the exact target to WinUI.' }
    }
    if ((Get-FileHash -LiteralPath $fixture).Hash -ne (Get-FileHash -LiteralPath (Join-Path $target 'NitLink.exe')).Hash -or
        [IO.File]::ReadAllText((Join-Path $target 'nitlink.json')) -ne '{"synthetic_settings":true}' -or
        [IO.File]::ReadAllText((Join-Path $target 'linked-accounts.dat')) -ne 'synthetic-account-vault') { throw 'Auto-update modified retained user data or the payload.' }
    Write-Output 'PASS NSIS auto-update: process wait, custom Unicode path, one relaunch per update, WinUI fallback, payload hash and retained settings/account vault.'
    $passed = $true
} finally {
    $uninstaller = Join-Path $target 'Uninstall.exe'
    if ($passed -and (Test-Path -LiteralPath $uninstaller)) {
        $cleanup = Join-Path $fixtureRoot 'RemoveFixture.exe'
        Copy-Item -LiteralPath $uninstaller -Destination $cleanup
        Start-Process -FilePath $cleanup -ArgumentList "/S _?=$target" -WindowStyle Hidden -Wait | Out-Null
    }
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $HolyBearRoot 'out')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected update fixture cleanup path.' }
    if ($passed) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    else { Write-Output "Failed fixture retained for diagnosis: $resolved" }
}
