param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$Test, [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Target='')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeExe = $cmakeCommand.Source } else {
    $candidates = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    )
    $cmakeExe = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $cmakeExe) { throw 'Install Visual Studio 2022 C++ Build Tools with Windows SDK and CMake.' }
$buildDirectory = Join-Path $projectRoot 'build'
& $cmakeExe -S $projectRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
$buildArgs=@('--build',$buildDirectory,'--config',$Configuration)
if ($Target) { $buildArgs+=@('--target',$Target) }
$buildArgs+=@('--parallel','1','--','/nodeReuse:false')
& $cmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) {
    & (Join-Path $PSScriptRoot 'check-local.ps1') -Suite Full -Configuration $Configuration -BuildDirectory $buildDirectory
}
