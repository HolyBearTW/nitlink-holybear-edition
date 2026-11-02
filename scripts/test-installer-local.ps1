param([Parameter(Mandatory)][string]$PackageDirectory, [Parameter(Mandatory)][string]$GeneratedDirectory, [Parameter(Mandatory)][string]$UiExe)
. "$PSScriptRoot/holy-bear-common.ps1"
. "$PSScriptRoot/get-nsis.ps1"
$info = Get-HolyBearVersion
$fixtureId = [guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path $HolyBearRoot "out/installer-check-$fixtureId"
$fixtureProduct = "NitLink Setup Check $fixtureId"
$stage = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath
$generated = (Resolve-Path -LiteralPath $GeneratedDirectory).ProviderPath
$ui = (Resolve-Path -LiteralPath $UiExe).ProviderPath
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$setup = Join-Path $fixtureRoot 'NitLink-InstallerCheck.exe'
$target = Join-Path $fixtureRoot '測試安裝 with spaces'
$compiler = Get-HolyBearNSIS
$license = Join-Path $generated 'LICENSE.rtf'
$installFiles = Join-Path $generated 'install-files.nsh'
$uninstallFiles = Join-Path $generated 'uninstall-files.nsh'
& $compiler /V2 /WX /INPUTCHARSET UTF8 "/DPRODUCT=$fixtureProduct" "/DREGKEY=Software\NitLinkSetupChecks\$fixtureId" "/DUNINSTALLKEY=Software\Microsoft\Windows\CurrentVersion\Uninstall\NitLink-Check-$fixtureId" "/DVERSION=$($info.Version)" "/DNUMERIC_VERSION=$($info.Numeric)" "/DWINDOWS_VERSION=$($info.WindowsVersion)" /DPACKAGE_KB=1 "/DPACKAGE_DIR=$stage" "/DWINUI_SETUP=$ui" "/DLICENSE_RTF=$license" "/DOUTPUT_FILE=$setup" "/DINSTALL_FILES=$installFiles" "/DUNINSTALL_FILES=$uninstallFiles" (Join-Path $HolyBearRoot 'installer/holy-bear.nsi')
if ($LASTEXITCODE -ne 0) { throw 'Isolated installer fixture did not compile.' }
try {
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        $operation = Start-Process -FilePath $setup -ArgumentList "/S /LANG=1028 /DESKTOP=0 /D=$target" -WindowStyle Hidden -Wait -PassThru
        if ($operation.ExitCode -ne 0) { throw 'Isolated install/upgrade failed.' }
        foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
            $installed = Join-Path $target $file.FullName.Substring($stage.Length + 1)
            if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Isolated installed payload mismatch.' }
        }
        if ((Get-FileHash -LiteralPath $ui).Hash -ne (Get-FileHash -LiteralPath (Join-Path $target 'setup-ui/NitLinkSetupUi.exe')).Hash) { throw 'Installed WinUI interface mismatch.' }
        if ($attempt -eq 0) {
            [IO.File]::WriteAllText((Join-Path $target 'nitlink.json'), '{"private_fixture":true}')
            [IO.File]::WriteAllText((Join-Path $target 'linked-accounts.dat'), 'synthetic-account-vault')
        } elseif ([IO.File]::ReadAllText((Join-Path $target 'nitlink.json')) -ne '{"private_fixture":true}' -or
            [IO.File]::ReadAllText((Join-Path $target 'linked-accounts.dat')) -ne 'synthetic-account-vault') { throw 'Upgrade modified user data.' }
    }
    $uninstaller = Join-Path $fixtureRoot 'Uninstall.exe'
    Copy-Item -LiteralPath (Join-Path $target 'Uninstall.exe') -Destination $uninstaller
    $operation = Start-Process -FilePath $uninstaller -ArgumentList "/S _?=$target" -WindowStyle Hidden -Wait -PassThru
    if ($operation.ExitCode -ne 0) { throw 'Isolated uninstall failed.' }
    foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
        if (Test-Path -LiteralPath (Join-Path $target $file.FullName.Substring($stage.Length + 1))) { throw 'Uninstall retained a packaged file.' }
    }
    if ((Test-Path -LiteralPath (Join-Path $target 'setup-ui/NitLinkSetupUi.exe')) -or
        -not (Test-Path -LiteralPath (Join-Path $target 'nitlink.json')) -or
        [IO.File]::ReadAllText((Join-Path $target 'linked-accounts.dat')) -ne 'synthetic-account-vault') { throw 'Uninstall retention policy failed.' }
    Write-Output 'PASS isolated native install, upgrade and uninstall; all payload hashes match; settings and account vault retained.'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $HolyBearRoot 'out')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
