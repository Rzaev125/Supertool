param([string]$Destination = (Join-Path $PSScriptRoot '..\tools\modules\ocr'))
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$destinationPath = [IO.Path]::GetFullPath($Destination)
if (-not $destinationPath.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OCR destination must be inside this workspace.'
}
$cachePath = Join-Path $project 'tools\downloads'
$stagePath = Join-Path $project ('tools\ocr-stage-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $cachePath,$destinationPath | Out-Null
New-Item -ItemType Directory -Path $stagePath | Out-Null
$downloads = @()
function Get-VerifiedDownload([string]$Name, [string]$Url, [string]$Sha256) {
    $path = Join-Path $cachePath $Name
    if (-not (Test-Path -LiteralPath $path)) { Invoke-WebRequest -Uri $Url -OutFile $path }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Sha256) { throw ('SHA256 mismatch: ' + $Name) }
    $script:downloads += [ordered]@{ filename=$Name; url=$Url; sha256=$Sha256.ToLowerInvariant() }
    return $path
}
try {
    # Both EXE archives are unpacked as data. Neither Windows installer runs.
    # The 7zr helper and extracted 7z CLI only operate inside this workspace.
    $sevenBootstrap = Get-VerifiedDownload '7zr.exe' 'https://github.com/ip7z/7zip/releases/download/26.05/7zr.exe' '9BF582B894DFA665F852D7E9112B02290A1C303C2D95A21FCDC41FFCB03143B7'
    $sevenArchive = Get-VerifiedDownload '7z2605-x64.exe' 'https://github.com/ip7z/7zip/releases/download/26.05/7z2605-x64.exe' 'FD99E86F57D8F429EF5ED045BF98315AF962E2309B0F1BA87A147A44C71647D0'
    $installer = Get-VerifiedDownload 'tesseract-5.5.3.exe' 'https://github.com/tesseract-ocr/tesseract/releases/download/5.5.3/tesseract-ocr-w64-setup-5.5.3.20260724.exe' 'BEE9E3434BD94FD65387D9BE28CD467A41F61B1275383B55B0F59A1331270AE4'
    $sevenPath = Join-Path $stagePath '7zip'
    & $sevenBootstrap x $sevenArchive ('-o' + $sevenPath) -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) { throw '7-Zip helper extraction failed.' }
    $runtimeFiles = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ocr-runtime-files.json') -Raw | ConvertFrom-Json
    foreach ($name in $runtimeFiles) {
        if ($name -notmatch '^[a-zA-Z0-9+._-]+\.(dll|exe)$') { throw 'Unsafe OCR runtime filename.' }
    }
    $extractPath = Join-Path $stagePath 'runtime'
    & (Join-Path $sevenPath '7z.exe') x $installer ('-o' + $extractPath) -y -bso0 -bsp0 @runtimeFiles
    if ($LASTEXITCODE -ne 0) { throw 'Tesseract archive extraction failed.' }
    foreach ($name in $runtimeFiles) {
        Copy-Item -LiteralPath (Join-Path $extractPath $name) -Destination (Join-Path $destinationPath $name) -Force
    }
    $modelPath = Join-Path $destinationPath 'tessdata'
    New-Item -ItemType Directory -Force -Path $modelPath | Out-Null
    $models = [ordered]@{
        eng = '7D4322BD2A7749724879683FC3912CB542F19906C83BCC1A52132556427170B2'
        rus = 'E16E5E036CCE1D9EC2B00063CF8B54472625B9E14D893A169E2B0DEDEB4DF225'
        aze = 'A365310848AECB739F19369CB3831D4660FCD9345D798E91A3042455F9CCC9F0'
        aze_cyrl = '2E96AB67817819E14A3A2D6250261130457C8E78DDE3077841AB391FE003551C'
    }
    $modelBase = 'https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/87416418657359cb625c412a48b6e1d6d41c29bd/'
    foreach ($language in $models.Keys) {
        $model = Get-VerifiedDownload ('ocr-' + $language + '.traineddata') ($modelBase + $language + '.traineddata') $models[$language]
        Copy-Item -LiteralPath $model -Destination (Join-Path $modelPath ($language + '.traineddata')) -Force
    }
    $licenses = Join-Path $destinationPath 'licenses'
    New-Item -ItemType Directory -Force -Path $licenses | Out-Null
    Copy-Item -Path (Join-Path $project 'docs\licenses\ocr\*') -Destination $licenses -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $project 'docs\licenses\ocr\tessdata-LICENSE') -Destination (Join-Path $modelPath 'LICENSE') -Force
    Copy-Item -LiteralPath (Join-Path $project 'docs\ocr-module.md') -Destination (Join-Path $destinationPath 'README.md') -Force
    $downloads | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destinationPath 'downloads.json') -Encoding utf8
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ocr-runtime-files.json') -Destination (Join-Path $destinationPath 'runtime-files.json') -Force
    # PowerShell 5.1 promotes stderr version output to an error record; cmdlet
    # error policy is relaxed only while querying this verified native executable.
    $savedPolicy = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $version = & (Join-Path $destinationPath 'tesseract.exe') --version 2>&1
    $versionCode = $LASTEXITCODE
    $ErrorActionPreference = $savedPolicy
    if ($versionCode -ne 0 -or ($version -join ' ') -notmatch '5\.5\.3\.20260724') { throw 'Tesseract version check failed.' }
    Write-Host ('OCR module ready: ' + $destinationPath)
} finally {
    # Only remove the unique staging directory; retain destination and download cache.
    $resolvedStage = [IO.Path]::GetFullPath($stagePath)
    if ($resolvedStage.StartsWith((Join-Path $project 'tools\ocr-stage-'), [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Parent $resolvedStage) -eq (Join-Path $project 'tools')) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
