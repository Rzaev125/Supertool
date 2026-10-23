param([string]$Configuration='Release',[switch]$StageOnly,[ValidateSet('full','core')][string]$Distribution='full')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root=Split-Path $PSScriptRoot -Parent
$version='0.13.0'
$exe=Join-Path $root "build\$Configuration\ShiftWheel.exe"
if((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $version){throw 'Build the matching version first.'}
# Fresh release tree; never remove engines or state from a portable user's folder.
$name="ShiftWheel-$version-$Distribution"
$destination=Join-Path $root ('out\'+$name)
if(Test-Path -LiteralPath $destination){throw 'Release tree already exists. Preserve it or choose a new release version.'}
[void][IO.Directory]::CreateDirectory($destination)
Copy-Item -LiteralPath $exe -Destination $destination
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $destination
Copy-Item -LiteralPath (Join-Path $root 'resources\START.txt') -Destination $destination
$docs=Join-Path $destination 'docs';[void][IO.Directory]::CreateDirectory($docs)
foreach($file in @('avif-transforms.md','review-0.13.0.md','acceptance-0.13.0.md','video-codecs.md','image-orientation.md','review-0.12.0.md','acceptance-0.12.0.md','review-0.11.0.md','acceptance-0.11.0.md','review-0.10.0.md','acceptance-0.10.0.md','animated-images.md','performance-0.9.md','performance-0.9.json','modules-0.9.md','modules.md','heic-module.md','presentation-module.md','ocr-module.md','installer.md','hardware-video.md','updates.md','signing.md','recovery.md','acceptance-0.7.0.md','acceptance-0.8.0.md','acceptance-0.9.0.md')){
 Copy-Item -LiteralPath (Join-Path $root ('docs\'+$file)) -Destination $docs
}
Copy-Item -LiteralPath (Join-Path $root 'docs\verification.md') -Destination (Join-Path $destination 'VERIFICATION.md')
if($Distribution -eq 'full'){
 & (Join-Path $PSScriptRoot 'stage-modules.ps1') -ModuleRoot (Join-Path $root 'tools\modules') -InventoryDirectory (Join-Path $root 'build\module-packages') -CatalogPath (Join-Path $root 'resources\module_catalog.json') -Destination (Join-Path $destination 'modules')
}
$release=[ordered]@{version=$version;distribution=$Distribution;executable_sha256=(Get-FileHash -LiteralPath (Join-Path $destination 'ShiftWheel.exe') -Algorithm SHA256).Hash}
[IO.File]::WriteAllText((Join-Path $destination 'release.json'),($release|ConvertTo-Json)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
if($StageOnly){Write-Output $destination;return}
$suffix=if($Distribution -eq 'core'){'-core'}else{''}
$archive=Join-Path $root "out\ShiftWheel-$version$suffix-win-x64.zip"
& tar.exe -a -cf $archive -C (Join-Path $root 'out') $name
if($LASTEXITCODE -ne 0){throw 'Archive creation failed.'}
Get-Item -LiteralPath $archive | Select-Object FullName,Length
