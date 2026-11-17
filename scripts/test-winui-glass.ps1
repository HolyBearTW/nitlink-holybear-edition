# Builds the actual installer host with a separate test-only entry point.
. "$PSScriptRoot/holy-bear-common.ps1"
$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet -or -not (& $dotnet --list-sdks | Where-Object { $_ -match '^10\.' })) {
    $dotnet = Join-Path $HolyBearRoot 'out/tools/dotnet10/dotnet.exe'
}
$destination = Join-Path $HolyBearRoot 'out/winui-glass-checks'
& $dotnet publish (Join-Path $HolyBearRoot 'installer/winui/NitLinkSetup.csproj') -c Release -r win-x64 -p:Platform=x64 -p:RestoreLockedMode=true -p:SetupLifecycleChecks=true -o $destination -nologo -v quiet
if ($LASTEXITCODE -ne 0) { throw 'Glass lifecycle host did not build.' }
$fixture = Join-Path $HolyBearRoot ('out/setup-glass-fixtures-' + [guid]::NewGuid().ToString('N'))
foreach ($language in @(1033, 1028, 2052, 1031, 1041, 1042, 1055)) {
    $languageFixture = Join-Path $fixture $language
    foreach ($mode in @('install', 'update', 'uninstall')) {
        $process = Start-Process -FilePath (Join-Path $destination 'NitLinkSetupLifecycleChecks.exe') -ArgumentList ('"' + $languageFixture + '"'), $mode, $language -WindowStyle Hidden -Wait -PassThru
        $report = Join-Path $languageFixture "$mode.json"
        if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report }
        if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report)) { throw "Setup glass lifecycle failed: $language/$mode (exit $($process.ExitCode)); report: $report" }
    }
}
Write-Output "PASS production install/update/uninstall glass lifecycle in seven languages on isolated desktops; reports: $fixture"
