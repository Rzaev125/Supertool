param()
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Archive\Microsoft.PowerShell.Archive.psd1') -Force
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$cache = Join-Path $root 'tools\installer-downloads'
$stage = Join-Path $root ('tools\installer-tool-stage-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($cache)
[void][IO.Directory]::CreateDirectory($stage)
$manifestPath = Join-Path $PSScriptRoot 'installer-tools.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
    if ($entry.filename -notmatch '^[a-z0-9.-]+\.(exe|zip)$') { throw 'Unsafe installer tool filename.' }
    $archive = Join-Path $cache $entry.filename
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $entry.url -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $entry.sha256) { throw ('Installer tool SHA256 mismatch: ' + $entry.filename) }
}
# Extract the official installer as data. Never run it or register the compiler.
$unpacker = Join-Path $stage 'unpacker'
Expand-Archive -LiteralPath (Join-Path $cache 'innounp-267.zip') -DestinationPath $unpacker
$extract = Join-Path $stage 'inno'
& (Join-Path $unpacker 'innounp.exe') -x -y ('-d' + $extract) (Join-Path $cache 'innosetup-6.7.3.exe') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup archive extraction failed.' }
$compilerDirectory = Join-Path $extract '{app}'
if (-not (Test-Path -LiteralPath (Join-Path $compilerDirectory 'license.txt'))) { throw 'Missing compiler license.' }
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $stage 'provenance.json')
$compiler = Join-Path $compilerDirectory 'ISCC.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Missing Inno Setup compiler.' }
Copy-Item -LiteralPath (Join-Path $root 'installer\tool-licenses') -Destination (Join-Path $stage 'licenses') -Recurse
Write-Output $compiler
