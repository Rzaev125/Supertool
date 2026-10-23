param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '0.16.0',
    [string]$PayloadDirectory = '',
    [string]$TestRoot = '',
    [ValidateSet('full','core')][string]$Distribution='full'
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
. (Join-Path $PSScriptRoot 'installer-workspace.ps1')
if (-not $PayloadDirectory) { $PayloadDirectory = Join-Path $root ('out\ShiftWheel-'+$Version+'-'+$Distribution) }
$run = Join-Path $root ('build\installer-build-' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $run 'payload'
$output = Join-Path $run 'output'
$isolated = -not [string]::IsNullOrWhiteSpace($TestRoot)
if ($isolated) {
    $TestRoot = [IO.Path]::GetFullPath($TestRoot).TrimEnd('\')
    $allowed = Join-Path $root 'build\installer-tests'
    if (-not $TestRoot.StartsWith($allowed + '\', [StringComparison]::OrdinalIgnoreCase) -or $TestRoot.Contains('"')) { throw 'TestRoot must be a dedicated directory inside build\installer-tests.' }
    Assert-InstallerLocalPath $root $TestRoot
}
Assert-InstallerLocalPath $root $run
$compilerLog = Join-Path $run 'compiler.log'
try {
    [void][IO.Directory]::CreateDirectory($output)
    $fixturePayload = $isolated -and $Distribution -eq 'full' -and -not (Test-Path -LiteralPath (Join-Path $PayloadDirectory 'release.json'))
    & (Join-Path $root 'installer\stage-payload.ps1') -PayloadDirectory $PayloadDirectory -StageDirectory $stage -Version $Version -AllowTestPayload:$fixturePayload -Distribution $Distribution
    $name = 'ShiftWheel-' + $Version + $(if($Distribution -eq 'core'){'-core'}else{''}) + '-win-x64-setup'
    if ($isolated) { $name += '-ISOLATED-TEST' }
    $compilerArguments = @('/Qp',('/DAppVersion=' + $Version),('/DPayloadDir=' + $stage),('/DOutputDir=' + $output),('/DOutputName=' + $name))
    if ($isolated) { $compilerArguments += '/DIsolatedRoot=' + $TestRoot }
    $compilerArguments += Join-Path $root 'installer\ShiftWheel.iss'
    & (Join-Path $PSScriptRoot 'fetch-installer-tools.ps1') -UseCompiler {
        param($compiler)
        & $compiler @compilerArguments *> $compilerLog
        if ($LASTEXITCODE -ne 0) { throw 'Installer compiler failed.' }
    }
    $built = Join-Path $output ($name + '.exe')
    if (-not (Test-Path -LiteralPath $built)) { throw 'Compiler did not produce the installer.' }
    $destination = if ($isolated) { Join-Path $TestRoot 'setup' } else { Join-Path $root 'out' }
    Assert-InstallerLocalPath $root $destination
    [void][IO.Directory]::CreateDirectory($destination)
    $published = Join-Path $destination ($name + '.exe')
    Assert-InstallerLocalPath $root $published
    $hashPath = $published + '.sha256'
    $manifestPath = $published + '.payload.json'
    Assert-InstallerLocalPath $root $hashPath
    Assert-InstallerLocalPath $root $manifestPath
    if (-not $isolated) {
        if (Test-Path -LiteralPath $published) {
            $history = Join-Path $root ('out\installer-history\' + (Split-Path $run -Leaf))
            Assert-InstallerLocalPath $root $history
            [void][IO.Directory]::CreateDirectory($history)
            Copy-Item -LiteralPath $published -Destination $history
        }
    }
    Copy-Item -LiteralPath $built -Destination $published -Force
    $built = $published
    $hash = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash.ToLowerInvariant()
    ($hash + '  ' + (Split-Path $built -Leaf)) | Set-Content -LiteralPath $hashPath -Encoding ASCII
    Copy-Item -LiteralPath (Join-Path $stage 'installer-payload.json') -Destination $manifestPath
} catch {
    # Keep a bounded diagnostic instead of retaining an entire failed payload.
    $errorLog = Join-Path $root 'build\installer-last-error.log'
    Assert-InstallerLocalPath $root $errorLog
    $diagnostic = $_.ToString()
    if (Test-Path -LiteralPath $compilerLog) { $diagnostic += "`r`n" + (Get-Content -LiteralPath $compilerLog -Raw) }
    if ($diagnostic.Length -gt 65536) { $diagnostic = $diagnostic.Substring($diagnostic.Length - 65536) }
    Set-Content -LiteralPath $errorLog -Value $diagnostic -Encoding UTF8
    Write-Host $diagnostic
    throw
} finally {
    Remove-InstallerWorkspace $root $run
}
Write-Host ('Installer ready: ' + $built)
Write-Output $built
