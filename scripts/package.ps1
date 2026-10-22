param([string]$Configuration = 'Release', [switch]$StageOnly)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root = Split-Path $PSScriptRoot -Parent
$executable = Join-Path $root "build\$Configuration\ShiftWheel.exe"
$version = '0.7.0'
if ((Get-Item -LiteralPath $executable).VersionInfo.ProductVersion -ne $version) { throw 'Build the matching application version before packaging.' }
$modules = Join-Path $root 'tools\modules'
foreach ($file in @('ffmpeg\ffmpeg.exe','ffmpeg\ffprobe.exe','libreoffice\program\soffice.com','pdfium\pdfium.dll','avif\avifenc.exe','avif\avifdec.exe','avif\vcruntime140.dll','heic\libheif.dll','heic\heif-enc.exe','heic\heif-dec.exe','ocr\tesseract.exe','ocr\tessdata\eng.traineddata','ocr\tessdata\rus.traineddata','ocr\tessdata\aze.traineddata','ocr\tessdata\aze_cyrl.traineddata')) {
    if (-not (Test-Path -LiteralPath (Join-Path $modules $file))) { throw "Missing module: $file" }
}
$destination = Join-Path $root 'out\ShiftWheel'
[void][System.IO.Directory]::CreateDirectory($destination)
Copy-Item -LiteralPath $executable -Destination $destination -Force
Copy-Item -LiteralPath $modules -Destination $destination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $root 'docs\modules.md') -Destination $destination -Force
[void][System.IO.Directory]::CreateDirectory((Join-Path $destination 'docs'))
foreach ($document in @('heic-module.md','presentation-module.md','ocr-module.md','installer.md','hardware-video.md','updates.md')) {
    Copy-Item -LiteralPath (Join-Path $root "docs\$document") -Destination $destination -Force
    Copy-Item -LiteralPath (Join-Path $root "docs\$document") -Destination (Join-Path $destination 'docs') -Force
}
Copy-Item -LiteralPath (Join-Path $root 'docs\verification.md') -Destination (Join-Path $destination 'VERIFICATION.md') -Force
foreach ($kind in @('mp4','xlsx','pdf','avif','webp','wav','heic','pptx')) {
    Copy-Item -LiteralPath (Join-Path $root "build\wheel-$kind.png") -Destination $destination -Force
}
# Copy UTF-8 verbatim; Windows PowerShell 5.1 decodes BOM-less script literals as ANSI.
Copy-Item -LiteralPath (Join-Path $root 'resources\START.txt') -Destination (Join-Path $destination 'START.txt') -Force
$release = [ordered]@{ version=$version; executable_sha256=(Get-FileHash -LiteralPath (Join-Path $destination 'ShiftWheel.exe') -Algorithm SHA256).Hash }
[IO.File]::WriteAllText((Join-Path $destination 'release.json'), ($release | ConvertTo-Json) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
if ($StageOnly) { Get-Item -LiteralPath (Join-Path $destination 'ShiftWheel.exe'); return }
$archive = Join-Path $root "out\ShiftWheel-$version-win-x64.zip"
# tar uses ZIP64 where required and includes the directory as a portable bundle.
# Keep the user's portable settings in place, but never distribute them.
& tar.exe -a -cf $archive --exclude 'ShiftWheel/ShiftWheel.ini' --exclude 'ShiftWheel/ShiftWheel.ini.tmp-*' --exclude 'ShiftWheel/ShiftWheel.diagnostics.log' -C (Join-Path $root 'out') ShiftWheel
if ($LASTEXITCODE -ne 0) { throw 'Archive creation failed.' }
Get-Item -LiteralPath (Join-Path $destination 'ShiftWheel.exe'),$archive | Select-Object FullName,Length
