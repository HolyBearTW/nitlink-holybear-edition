$ErrorActionPreference = 'Stop'
$privacyScratch = Join-Path ([IO.Path]::GetTempPath()) ('NitLink-package-privacy-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $privacyScratch | Out-Null
try {
    foreach ($privateFile in @('linked-accounts.dat','nitlink.json','account-library.json','accounts.json','debug.log','WebView2/Default/Cookies','WebView2/Default/Local Storage/leveldb/000001.log')) {
        $fixture = Join-Path $privacyScratch $privateFile
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fixture)) | Out-Null
        [IO.File]::WriteAllText($fixture,'synthetic-private-account-fixture')
        $rejected = $false
        try { & "$PSScriptRoot/validate-package.ps1" -Path $privacyScratch -HolyBear | Out-Null }
        catch { $rejected = $_.Exception.Message -eq "Unexpected package file: $privateFile" }
        if (-not $rejected) { throw "Package privacy boundary did not reject $privateFile" }
        Remove-Item -LiteralPath $fixture
    }
    Write-Output 'PASS package inventory rejects account vaults, settings, account exports, logs and WebView login data.'
} finally {
    $privacyResolved = [IO.Path]::GetFullPath($privacyScratch)
    $privacyTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $privacyResolved.StartsWith($privacyTemp,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected privacy test cleanup path.' }
    Remove-Item -LiteralPath $privacyResolved -Recurse -Force
}
