param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '0.12.0',
    [string]$PayloadDirectory = '',
    [string]$TestRoot = '',
    [ValidateSet('full','core')][string]$Distribution='full'
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
if (-not $PayloadDirectory) { $PayloadDirectory = Join-Path $root ('out\ShiftWheel-'+$Version+'-'+$Distribution) }
$run = Join-Path $root ('build\installer-build-' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $run 'payload'
$output = Join-Path $run 'output'
[void][IO.Directory]::CreateDirectory($output)
$isolated = -not [string]::IsNullOrWhiteSpace($TestRoot)
if ($isolated) {
    $TestRoot = [IO.Path]::GetFullPath($TestRoot).TrimEnd('\')
    $allowed = Join-Path $root 'build\installer-tests'
    if (-not $TestRoot.StartsWith($allowed + '\', [StringComparison]::OrdinalIgnoreCase) -or $TestRoot.Contains('"')) { throw 'TestRoot must be a dedicated directory inside build\installer-tests.' }
}
$fixturePayload = $isolated -and $Distribution -eq 'full' -and -not (Test-Path -LiteralPath (Join-Path $PayloadDirectory 'release.json'))
& (Join-Path $root 'installer\stage-payload.ps1') -PayloadDirectory $PayloadDirectory -StageDirectory $stage -Version $Version -AllowTestPayload:$fixturePayload -Distribution $Distribution
$compiler = & (Join-Path $PSScriptRoot 'fetch-installer-tools.ps1')
$name = 'ShiftWheel-' + $Version + $(if($Distribution -eq 'core'){'-core'}else{''}) + '-win-x64-setup'
if ($isolated) { $name += '-ISOLATED-TEST' }
$arguments = @('/Qp',('/DAppVersion=' + $Version),('/DPayloadDir=' + $stage),('/DOutputDir=' + $output),('/DOutputName=' + $name))
if ($isolated) { $arguments += '/DIsolatedRoot=' + $TestRoot }
$arguments += Join-Path $root 'installer\ShiftWheel.iss'
& $compiler @arguments *> (Join-Path $run 'compiler.log')
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath (Join-Path $run 'compiler.log') -Tail 30 | Write-Host
    throw ('Installer compiler failed; see ' + (Join-Path $run 'compiler.log'))
}
$built = Join-Path $output ($name + '.exe')
if (-not (Test-Path -LiteralPath $built)) { throw 'Compiler did not produce the installer.' }
if (-not $isolated) {
    $published = Join-Path $root ('out\' + $name + '.exe')
    if (Test-Path -LiteralPath $published) {
        $history = Join-Path $root ('out\installer-history\' + (Split-Path $run -Leaf))
        [void][IO.Directory]::CreateDirectory($history)
        Copy-Item -LiteralPath $published -Destination $history
    }
    Copy-Item -LiteralPath $built -Destination $published -Force
    $built = $published
}
$hash = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash.ToLowerInvariant()
($hash + '  ' + (Split-Path $built -Leaf)) | Set-Content -LiteralPath ($built + '.sha256') -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $stage 'installer-payload.json') -Destination ($built + '.payload.json')
Write-Host ('Installer ready: ' + $built)
Write-Output $built
