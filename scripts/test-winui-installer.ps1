. "$PSScriptRoot/holy-bear-common.ps1"
$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet -or -not (& $dotnet --list-sdks | Where-Object { $_ -match '^10\.' })) {
    $dotnet = Join-Path $HolyBearRoot 'out/tools/dotnet10/dotnet.exe'
}
$destination = Join-Path $HolyBearRoot 'out/winui-setup-checks'
& $dotnet publish (Join-Path $HolyBearRoot 'installer/winui.tests/NitLinkSetupChecks.csproj') -c Release -r win-x64 -o $destination -nologo -v quiet
if ($LASTEXITCODE -ne 0) { throw 'Installer backend checks did not build.' }
& (Join-Path $destination 'NitLinkSetupChecks.exe')
if ($LASTEXITCODE -ne 0) { throw 'Installer backend checks failed.' }
