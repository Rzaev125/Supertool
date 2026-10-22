param([string]$Destination = (Join-Path $PSScriptRoot '..\tools\modules\heic'))
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$destinationPath = [IO.Path]::GetFullPath($Destination)
if (-not $destinationPath.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'HEIC destination must be inside this workspace.'
}
$cachePath = Join-Path $project 'tools\heic-msys\packages'
$extractPath = Join-Path $project ('tools\heic-stage-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $cachePath,$extractPath,$destinationPath | Out-Null
$packages = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'heic-packages.json') -Raw | ConvertFrom-Json
$runtimeFiles = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'heic-runtime-files.json') -Raw | ConvertFrom-Json
try {
    foreach ($package in $packages) {
        if ($package.filename -notmatch '^mingw-w64-ucrt-x86_64-[A-Za-z0-9+._~-]+\.pkg\.tar\.zst$') { throw 'Unsafe package filename.' }
        $archive = Join-Path $cachePath $package.filename
        if (-not (Test-Path -LiteralPath $archive)) {
            Invoke-WebRequest -Uri ('https://repo.msys2.org/mingw/ucrt64/' + $package.filename) -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) {
            throw ('SHA256 mismatch: ' + $package.filename)
        }
        $members = @(& tar.exe -tf $archive | Where-Object {
            ($_ -match '^ucrt64/bin/([^/]+)$' -and $Matches[1].ToLowerInvariant() -in $runtimeFiles) -or
            ($_ -match '^ucrt64/share/licenses/[^.].*[^/]$')
        })
        if ($LASTEXITCODE -ne 0) { throw ('Archive listing failed: ' + $package.filename) }
        if ($members.Count -eq 0) { continue }
        & tar.exe -xf $archive -C $extractPath -- @members
        if ($LASTEXITCODE -ne 0) { throw ('Extraction failed: ' + $package.filename) }
    }
    foreach ($name in $runtimeFiles) {
        if ($name -notmatch '^[a-z0-9+._-]+\.(dll|exe)$') { throw 'Unsafe runtime filename.' }
        Copy-Item -LiteralPath (Join-Path $extractPath ('ucrt64\bin\' + $name)) -Destination (Join-Path $destinationPath $name) -Force
    }
    $licenses = Join-Path $destinationPath 'licenses'
    New-Item -ItemType Directory -Force -Path $licenses | Out-Null
    Copy-Item -Path (Join-Path $extractPath 'ucrt64\share\licenses\*') -Destination $licenses -Recurse -Force
    foreach ($codec in @('x264', 'x265')) {
        $codecLicensePath = Join-Path $licenses $codec
        New-Item -ItemType Directory -Force -Path $codecLicensePath | Out-Null
        Copy-Item -LiteralPath (Join-Path $project ('docs\licenses\heic-' + $codec + '-COPYING')) -Destination (Join-Path $codecLicensePath 'COPYING') -Force
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'heic-packages.json') -Destination (Join-Path $destinationPath 'packages.json') -Force
    Copy-Item -LiteralPath (Join-Path $project 'docs\heic-module.md') -Destination (Join-Path $destinationPath 'README.md') -Force
    $version = & (Join-Path $destinationPath 'heif-info.exe') --version 2>&1
    if ($LASTEXITCODE -ne 0 -or ($version -join ' ') -notmatch '1\.23\.6') { throw 'HEIC version check failed.' }
    $encoders = & (Join-Path $destinationPath 'heif-enc.exe') --list-encoders 2>&1
    if ($LASTEXITCODE -ne 0 -or ($encoders -join ' ') -notmatch 'x265') { throw 'HEVC encoder missing.' }
    Write-Host ('HEIC module ready: ' + $destinationPath)
} finally {
    # Only remove the unique extraction folder allocated above; never remove destination or cache.
    $resolvedStage = [IO.Path]::GetFullPath($extractPath)
    if ($resolvedStage.StartsWith((Join-Path $project 'tools\heic-stage-'), [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Parent $resolvedStage) -eq (Join-Path $project 'tools')) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
