param(
    [Parameter(Mandatory=$true)][string]$PayloadDirectory,
    [Parameter(Mandatory=$true)][string]$StageDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [switch]$AllowTestPayload,
    [ValidateSet('full','core')][string]$Distribution='full'
)
$ErrorActionPreference = 'Stop'
# Native launchers can preserve PowerShell 7 module paths in a Windows PowerShell
# 5.1 child. Bind the utility module to the actual host, not inherited lookup order.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$source = [IO.Path]::GetFullPath($PayloadDirectory).TrimEnd('\')
$stage = [IO.Path]::GetFullPath($StageDirectory).TrimEnd('\')
foreach ($path in @($source,$stage)) {
    if (-not $path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Installer input and staging must stay inside this workspace.' }
    $ancestor = $path
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse point in installer input/staging path: ' + $ancestor) }
        }
        $parent = Split-Path $ancestor -Parent
        if ($parent -eq $ancestor) { break }
        $ancestor = $parent
    }
}
if ($stage.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase) -or $stage -eq $source) { throw 'Staging must not be inside the payload.' }
if (Test-Path -LiteralPath $stage) { throw 'Use a new staging directory; existing releases are never overwritten.' }
if (-not (Test-Path -LiteralPath (Join-Path $source 'ShiftWheel.exe'))) { throw 'Missing ShiftWheel.exe.' }

if (-not $AllowTestPayload) {
    $releasePath = Join-Path $source 'release.json'
    if (-not (Test-Path -LiteralPath $releasePath)) { throw 'Missing release.json; run scripts/package.ps1 first.' }
    $release = Get-Content -LiteralPath $releasePath -Raw | ConvertFrom-Json
    if ($release.version -ne $Version) { throw 'Payload release version does not match installer version.' }
    if ($release.executable_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $source 'ShiftWheel.exe') -Algorithm SHA256).Hash) { throw 'Payload executable hash does not match release.json.' }
    if ($release.distribution -notin @('full','core') -or $release.distribution -cne $Distribution) { throw 'Payload distribution is missing, invalid or does not match requested distribution.' }
    if ((Get-Item -LiteralPath (Join-Path $source 'ShiftWheel.exe')).VersionInfo.ProductVersion -ne $Version) { throw 'Payload executable product version does not match installer version.' }
}
$isCore = -not $AllowTestPayload -and $release.distribution -eq 'core'
if (-not $isCore -and -not (Test-Path -LiteralPath (Join-Path $source 'modules') -PathType Container)) { throw 'Missing portable modules.' }
if ($isCore -and (Test-Path -LiteralPath (Join-Path $source 'modules'))) { throw 'Core payload must not contain modules.' }
if (-not $AllowTestPayload -and -not $isCore) {
    & (Join-Path $root 'scripts\stage-modules.ps1') -ModuleRoot (Join-Path $source 'modules') -InventoryDirectory (Join-Path $root 'build\module-packages') -CatalogPath (Join-Path $root 'resources\module_catalog.json') -Destination (Join-Path $stage 'modules') -PackagedSource -ValidateOnly
}
$topFiles = @('ShiftWheel.exe','release.json','README.md','START.txt','VERIFICATION.md','modules.md','heic-module.md','presentation-module.md','ocr-module.md','installer.md','hardware-video.md','updates.md','wheel-mp4.png','wheel-xlsx.png','wheel-pdf.png','wheel-avif.png','wheel-webp.png','wheel-wav.png','wheel-heic.png','wheel-pptx.png')
$files = @(Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {
    $relative = $_.FullName.Substring($source.Length + 1)
    ($relative -in $topFiles -or $relative.StartsWith('modules\') -or $relative.StartsWith('docs\')) -and
    $_.Name -notlike 'ShiftWheel.ini*' -and $_.Name -notmatch '(?i)(\.tmp(?:-|$)|\.partial$|\.log$|\.options$|\.error\.txt$|^\.shiftwheel-)'
})
# Reparse points could make staging reach outside the authorized release tree.
foreach ($item in @(Get-Item -LiteralPath $source) + @(Get-ChildItem -LiteralPath $source -Recurse -Force)) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse point in payload: ' + $item.FullName) }
}
[void][IO.Directory]::CreateDirectory($stage)
$manifest = foreach ($file in $files | Sort-Object FullName) {
    $relative = $file.FullName.Substring($source.Length + 1)
    $target = Join-Path $stage $relative
    [void][IO.Directory]::CreateDirectory((Split-Path $target -Parent))
    Copy-Item -LiteralPath $file.FullName -Destination $target
    [ordered]@{ path=$relative.Replace('\','/'); bytes=$file.Length; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
}
[ordered]@{ version=$Version; files=@($manifest) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'installer-payload.json') -Encoding UTF8
Write-Host ('Staged ' + $files.Count + ' files: ' + $stage)
