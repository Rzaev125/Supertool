param(
    [ValidateSet('Smoke','Conversions','Full')][string]$Suite='Smoke',
    [string]$Configuration='Release',
    [string]$BuildDirectory='',
    [string]$Python='',
    [switch]$Cleanup,
    [switch]$Apply
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) { $BuildDirectory=Join-Path $root 'build' }
if (-not $Python) {
    $cache=Join-Path $BuildDirectory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $entry=[IO.File]::ReadAllLines($cache) | Where-Object { $_ -match '^SHIFTWHEEL_TEST_PYTHON:FILEPATH=' } | Select-Object -First 1
        if ($entry) { $Python=$entry.Substring($entry.IndexOf('=')+1) }
    }
    if (-not $Python) { $Python=(Get-Command python -ErrorAction Stop).Source }
}
$parameters=@((Join-Path $PSScriptRoot 'local-check.py'),'--suite',$Suite.ToLowerInvariant(),'--configuration',$Configuration,'--build-dir',$BuildDirectory)
if ($Cleanup) { $parameters+='--cleanup' }
if ($Apply) { $parameters+='--apply' }
& $Python @parameters
if ($LASTEXITCODE) { throw ('Local check failed with exit code '+$LASTEXITCODE+'. See build/local-check/report.json and check.log.') }
