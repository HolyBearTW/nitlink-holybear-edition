param([Parameter(Mandatory)][string]$OutputDirectory)
. "$PSScriptRoot/holy-bear-common.ps1"
$info = Get-HolyBearVersion
$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet -or -not (& $dotnet --list-sdks | Where-Object { $_ -match '^10\.' })) {
    $dotnet = Join-Path $HolyBearRoot 'out/tools/dotnet10/dotnet.exe'
}
if (-not (Test-Path -LiteralPath $dotnet)) { throw 'Install the .NET 10 SDK to build the independent WinUI setup.' }
$destination = [IO.Path]::GetFullPath($OutputDirectory)
Push-Location (Join-Path $HolyBearRoot 'installer/winui')
try {
    & $dotnet publish NitLinkSetup.csproj -c Release -r win-x64 -p:Platform=x64 -p:RestoreLockedMode=true "-p:Version=$($info.Numeric)" -o $destination -nologo -v minimal
    if ($LASTEXITCODE -ne 0) { throw 'WinUI setup publish failed.' }
} finally { Pop-Location }
$files = @(Get-ChildItem -LiteralPath $destination -File | Where-Object { $_.Extension -ne '.pdb' })
if ($files.Count -ne 1 -or $files[0].Name -ne 'NitLinkSetupUi.exe') { throw 'WinUI setup must publish as one bundled executable.' }
Write-Output 'Independent GPLv3 WinUI setup built with NitLink branding.'
