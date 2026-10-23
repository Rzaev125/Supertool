param([string]$Version='0.14.0',[string]$Repository='Rzaev125/Supertool')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
if ($Version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}$' -or $Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw 'Invalid version or repository.' }
$root=Split-Path $PSScriptRoot -Parent
$setup=Join-Path $root ('out\ShiftWheel-'+$Version+'-win-x64-setup.exe')
if ((Get-Item -LiteralPath $setup).VersionInfo.ProductVersion.Trim() -ne $Version) { throw 'Installer version mismatch.' }
$target=Join-Path $root 'updates\stable.txt'
[void][IO.Directory]::CreateDirectory((Split-Path $target -Parent))
$lines=@(
 ('version='+$Version)
 ('url=https://github.com/'+$Repository+'/releases/download/v'+$Version+'/'+(Split-Path $setup -Leaf))
 ('size='+(Get-Item -LiteralPath $setup).Length)
 ('sha256='+(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant())
)

[IO.File]::WriteAllText($target,($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
Write-Host 'Manifest generated locally. Publish only after the installer asset is available.'
