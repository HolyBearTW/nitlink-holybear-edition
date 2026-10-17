param(
    [Parameter(Mandatory)][string]$Report,
    [Parameter(Mandatory)][int]$ExpectedTests,
    [Parameter(Mandatory)][int]$CtestExitCode,
    [Parameter(Mandatory)][string]$Summary
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[xml]$xml = Get-Content -LiteralPath $Report -Raw
$suite = $xml.testsuite
$cases = @($suite.testcase)
if ($ExpectedTests -lt 1 -or $cases.Count -ne $ExpectedTests -or
    [int]$suite.tests -ne $ExpectedTests -or [int]$suite.skipped -ne 0) {
    throw 'Incomplete/skipped CTest suite; release rejected.'
}
$failures = @()
foreach ($case in $cases) {
    if ($case.SelectSingleNode('skipped') -or $case.status -notin @('run', 'fail')) { throw "Unexecuted test: $($case.name)" }
    if ($case.SelectSingleNode('failure')) { $failures += $case }
    elseif ($case.status -ne 'run') { throw "Inconsistent test status: $($case.name)" }
}
if ([int]$suite.failures -ne $failures.Count) { throw 'Inconsistent JUnit failure count.' }
if ($failures.Count -ne 0) { throw 'CTest failures; release rejected.' }
if ($CtestExitCode -ne 0) { throw 'CTest returned a failure without a complete failure report.' }
$result = [ordered]@{
    tests = $ExpectedTests; passed = $ExpectedTests
    failures = 0; skipped = 0; knownFailure = $null
}
$result | ConvertTo-Json | Set-Content -LiteralPath $Summary -Encoding utf8
Write-Output "CTest: $($result.passed)/$ExpectedTests; failures=0; skipped=0."
