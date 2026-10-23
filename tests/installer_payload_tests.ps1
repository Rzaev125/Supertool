param([switch]$TestReparsePoints)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'test-artifacts.ps1')
$work = Join-Path (Get-TestArtifactRoot $root) ('installer-payload-test-' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $work 'input'
New-Item -ItemType Directory -Path (Join-Path $source 'modules\ffmpeg'),(Join-Path $source 'docs') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $source 'ShiftWheel.exe'), 'binary fixture')
[IO.File]::WriteAllText((Join-Path $source 'modules\ffmpeg\ffmpeg.exe'), 'engine fixture')
[IO.File]::WriteAllText((Join-Path $source 'modules\ffmpeg\LICENSE'), 'engine license')
[IO.File]::WriteAllText((Join-Path $source 'docs\installer.md'), 'install guide')
foreach ($name in @('ShiftWheel.ini','ShiftWheel.ini.tmp-123','private.txt','job.options','job.log','modules\ffmpeg\conversion.partial')) {
    [IO.File]::WriteAllText((Join-Path $source $name), 'private user data')
}
$stage = Join-Path $work 'stage'
$script = Join-Path $root 'installer\stage-payload.ps1'
if (-not (Test-Path -LiteralPath $script)) { throw 'FAIL: installer payload staging is not implemented.' }
& $script -PayloadDirectory $source -StageDirectory $stage -Version '0.5.0' -AllowTestPayload
foreach ($name in @('ShiftWheel.exe','modules\ffmpeg\ffmpeg.exe','modules\ffmpeg\LICENSE','docs\installer.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $name))) { throw "FAIL: missing distributed file $name" }
}
foreach ($name in @('ShiftWheel.ini','ShiftWheel.ini.tmp-123','private.txt','job.options','job.log','modules\ffmpeg\conversion.partial')) {
    if (Test-Path -LiteralPath (Join-Path $stage $name)) { throw "FAIL: distributed private/work file $name" }
}
$records = Get-Content -LiteralPath (Join-Path $stage 'installer-payload.json') -Raw | ConvertFrom-Json
if ($records.files.Count -ne 4) { throw 'FAIL: unexpected staged manifest contents.' }
if (($records.files | Where-Object path -eq 'ShiftWheel.exe').sha256 -ne (Get-FileHash (Join-Path $stage 'ShiftWheel.exe')).Hash.ToLowerInvariant()) { throw 'FAIL: manifest does not identify installed executable.' }
$rejected = $false
try { & $script -PayloadDirectory $source -StageDirectory (Join-Path $work 'production') -Version '0.5.0' } catch { $rejected = $_.Exception.Message -match 'release.json' }
if (-not $rejected) { throw 'FAIL: production staging accepted an unverified release.' }
[IO.File]::WriteAllText((Join-Path $source 'release.json'), '{"version":"0.4.0","executable_sha256":"bad"}')
$rejected = $false
try { & $script -PayloadDirectory $source -StageDirectory (Join-Path $work 'wrong-version') -Version '0.5.0' } catch { $rejected = $_.Exception.Message -match 'version' }
if (-not $rejected) { throw 'FAIL: production staging accepted the wrong release version.' }
[IO.File]::WriteAllText((Join-Path $source 'release.json'), '{"version":"0.5.0","executable_sha256":"bad"}')
$rejected = $false
try { & $script -PayloadDirectory $source -StageDirectory (Join-Path $work 'wrong-hash') -Version '0.5.0' } catch { $rejected = $_.Exception.Message -match 'hash' }
if (-not $rejected) { throw 'FAIL: production staging accepted a tampered executable.' }
if ($TestReparsePoints) {
    $redirected = Join-Path $work 'redirect-target'
    [void][IO.Directory]::CreateDirectory($redirected)
    $link = Join-Path $work 'redirect-link'
    New-Item -ItemType Junction -Path $link -Target $redirected | Out-Null
    $rejected = $false
    try { & $script -PayloadDirectory $source -StageDirectory (Join-Path $link 'stage') -Version '0.5.0' -AllowTestPayload } catch { $rejected = $_.Exception.Message -match 'Reparse' }
    if (-not $rejected) { throw 'FAIL: staging followed a directory junction.' }
    Write-Host 'PASS: staging rejects a directory junction.'
}
Write-Host 'PASS: payload includes modules/licenses, excludes private/work files, records hashes, and rejects unverified releases.'

$core=Join-Path $work 'core'
[void][IO.Directory]::CreateDirectory($core)
Copy-Item -LiteralPath (Join-Path $root 'build\Release\ShiftWheel.exe') -Destination $core
$coreVersion=(Get-Item -LiteralPath (Join-Path $core 'ShiftWheel.exe')).VersionInfo.ProductVersion
$coreHash=(Get-FileHash -LiteralPath (Join-Path $core 'ShiftWheel.exe')).Hash
[IO.File]::WriteAllText((Join-Path $core 'release.json'),('{"version":"'+$coreVersion+'","executable_sha256":"'+$coreHash+'","distribution":"core"}'))
& $script -PayloadDirectory $core -StageDirectory (Join-Path $work 'core-stage') -Version $coreVersion -Distribution core
if(Test-Path -LiteralPath (Join-Path $work 'core-stage\modules')){throw 'FAIL: core unexpectedly includes modules'}
[IO.File]::WriteAllText((Join-Path $core 'release.json'),('{"version":"'+$coreVersion+'","executable_sha256":"'+$coreHash+'","distribution":"full"}'))
$rejected=$false
try{& $script -PayloadDirectory $core -StageDirectory (Join-Path $work 'bad-full') -Version $coreVersion}catch{$rejected=$_.Exception.Message -match 'modules'}
if(-not $rejected){throw 'FAIL: full payload accepted without modules'}
Write-Host 'PASS: verified core permits no modules; full rejects missing modules.'
