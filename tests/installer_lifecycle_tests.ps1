param(
    [string]$SetupExe = '',
    [string]$TestRoot = '',
    [switch]$LaunchInstalledApp,
    [switch]$ConvertInstalledMedia
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$timeout = if ($SetupExe) { 300000 } else { 90000 }
if (-not $TestRoot) { $TestRoot = Join-Path $root ('build\installer-tests\' + [Guid]::NewGuid().ToString('N')) }
$TestRoot = [IO.Path]::GetFullPath($TestRoot).TrimEnd('\')
$allowed = Join-Path $root 'build\installer-tests'
if (-not $TestRoot.StartsWith($allowed + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Lifecycle tests may only write inside build\installer-tests.' }
$app = Join-Path $TestRoot 'app'
if (Test-Path -LiteralPath $app) { throw 'Lifecycle tests require a fresh dedicated test directory.' }
[void][IO.Directory]::CreateDirectory($TestRoot)
if (-not $SetupExe) {
    $fixture = Join-Path $TestRoot 'fixture'
    [void][IO.Directory]::CreateDirectory((Join-Path $fixture 'modules\engine'))
    [IO.File]::WriteAllText((Join-Path $fixture 'ShiftWheel.exe'), 'fixture v1 executable - never executed')
    [IO.File]::WriteAllText((Join-Path $fixture 'modules\engine\runtime.dll'), 'fixture runtime')
    [IO.File]::WriteAllText((Join-Path $fixture 'README.md'), 'fixture v1 document')
    [IO.File]::WriteAllText((Join-Path $fixture 'ShiftWheel.ini'), 'private source settings')
    $SetupExe = & (Join-Path $root 'scripts\build-installer.ps1') -Version '0.4.0' -PayloadDirectory $fixture -TestRoot $TestRoot
}
$SetupExe = [IO.Path]::GetFullPath($SetupExe)
if ($SetupExe -notlike '*-ISOLATED-TEST.exe') { throw 'Only the compile-time isolated installer is allowed in lifecycle tests.' }
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{00BEDB6F-6D79-4F1C-AAB6-B13F51553090}_is1'
$beforeKey = Test-Path -LiteralPath $key
$links = @((Join-Path ([Environment]::GetFolderPath('Programs')) 'ShiftWheel.lnk'),(Join-Path ([Environment]::GetFolderPath('Desktop')) 'ShiftWheel.lnk'))
$beforeLinks = @($links | ForEach-Object { if (Test-Path -LiteralPath $_) { (Get-FileHash -LiteralPath $_).Hash } else { 'absent' } })
function Run-Setup([string]$Executable, [string]$Name, [string[]]$Extra, [bool]$ExpectSuccess) {
    $log = Join-Path $TestRoot ($Name + '.log')
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LOG="' + $log + '"')) + $Extra
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit($timeout)) { throw "Installer did not finish within $($timeout / 1000) seconds: $Name; see $log" }
    if (($process.ExitCode -eq 0) -ne $ExpectSuccess) { throw "Unexpected installer exit $($process.ExitCode): $Name; see $log" }
    Write-Host ("PASS: {0} (exit {1})" -f $Name,$process.ExitCode)
}
$mutex = [Threading.Mutex]::new($false, 'Local\ShiftWheel.Tray')
try { Run-Setup $SetupExe 'running-app-blocks-install' @('/SHIFTWHEELTEST=1') $false }
finally { $mutex.Dispose() }
if (Test-Path -LiteralPath $app) { throw 'Blocked setup modified the installation directory.' }
Run-Setup $SetupExe 'explicit-test-mode-required' @() $false
Run-Setup $SetupExe 'unsafe-directory-rejected' @('/SHIFTWHEELTEST=1',('/DIR="' + (Join-Path $TestRoot 'wrong') + '"')) $false
if (Test-Path -LiteralPath (Join-Path $TestRoot 'wrong')) { throw 'Setup wrote into a rejected destination.' }
Run-Setup $SetupExe 'fresh-install' @('/SHIFTWHEELTEST=1') $true
if (-not (Test-Path -LiteralPath (Join-Path $app 'ShiftWheel.exe'))) { throw 'Application was not installed.' }
if (Test-Path -LiteralPath (Join-Path $app 'ShiftWheel.ini')) { throw 'User settings leaked into fresh installation.' }
$payload = Get-Content -LiteralPath (Join-Path $app 'installer-payload.json') -Raw | ConvertFrom-Json
foreach ($entry in $payload.files) {
    $path = Join-Path $app $entry.path
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw ('Installed file integrity mismatch: ' + $entry.path) }
}
Write-Host ('PASS: all ' + $payload.files.Count + ' installed file hashes match the build manifest')
if ($LaunchInstalledApp) {
    if ($fixture) { throw 'The executable fixture cannot be launched; provide the full isolated setup.' }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ShiftWheelInstallerLaunchProbe {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string className, string title);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, UIntPtr wParam, IntPtr lParam);
}
'@
    $launched = Start-Process -FilePath (Join-Path $app 'ShiftWheel.exe') -ArgumentList '--panel' -WorkingDirectory $app -WindowStyle Hidden -PassThru
    $controller = [IntPtr]::Zero
    $owner = [uint32]0
    for ($attempt=0; $attempt -lt 100; $attempt++) {
        if ($launched.HasExited) { throw 'Installed application exited before its controller window appeared.' }
        $controller = [ShiftWheelInstallerLaunchProbe]::FindWindow('ShiftWheel.Controller','ShiftWheel')
        if ($controller -ne [IntPtr]::Zero) {
            [void][ShiftWheelInstallerLaunchProbe]::GetWindowThreadProcessId($controller, [ref]$owner)
            if ($owner -eq $launched.Id) { break }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($controller -eq [IntPtr]::Zero -or $owner -ne $launched.Id) { throw 'Installed application did not create its controller window.' }
    Run-Setup $SetupExe 'launched-app-blocks-install' @('/SHIFTWHEELTEST=1') $false
    [void][ShiftWheelInstallerLaunchProbe]::PostMessage($controller, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    if (-not $launched.WaitForExit(10000)) { throw 'Installed idle application did not exit gracefully.' }
    if ($launched.ExitCode -ne 0) { throw 'Installed application failed during shutdown.' }
    Write-Host 'PASS: installed application launches, blocks update while running, and exits gracefully.'
}
if ($ConvertInstalledMedia) {
    if ($fixture) { throw 'The executable fixture cannot convert media; provide the full isolated setup.' }
    Push-Location -LiteralPath $TestRoot
    try {
        & python (Join-Path $root 'tests\media_progress_tests.py') --exe (Join-Path $app 'ShiftWheel.exe') 2>&1 | Tee-Object -FilePath (Join-Path $TestRoot 'installed-media.log') | Write-Host
        if ($LASTEXITCODE -ne 0) { throw 'Installed media conversion/progress/source-preservation regression failed.' }
    } finally { Pop-Location }
}
$ini = Join-Path $app 'ShiftWheel.ini'
$foreign = Join-Path $app 'personal.txt'
$nested = Join-Path $app 'modules\personal-output\conversion.txt'
[void][IO.Directory]::CreateDirectory((Split-Path $nested -Parent))
[IO.File]::WriteAllText($ini, "[general]`r`nuser_choice=preserve-me`r`n")
[IO.File]::WriteAllText($foreign, 'keep root user file')
[IO.File]::WriteAllText($nested, 'keep nested user file')
$userHashes = @($ini,$foreign,$nested | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash })
$upgrade = $SetupExe
if ($fixture) {
    [IO.File]::WriteAllText((Join-Path $fixture 'ShiftWheel.exe'), 'fixture v2 executable - never executed')
    $upgrade = & (Join-Path $root 'scripts\build-installer.ps1') -Version '0.5.0' -PayloadDirectory $fixture -TestRoot $TestRoot
}
Run-Setup $upgrade 'upgrade' @('/SHIFTWHEELTEST=1') $true
if ($fixture -and [IO.File]::ReadAllText((Join-Path $app 'ShiftWheel.exe')) -ne 'fixture v2 executable - never executed') { throw 'Upgrade did not replace the executable.' }
for ($i=0; $i -lt 3; $i++) {
    if ((Get-FileHash -LiteralPath @($ini,$foreign,$nested)[$i]).Hash -ne $userHashes[$i]) { throw 'Upgrade changed user settings/files.' }
}
$uninstaller = Join-Path $app 'unins000.exe'
$mutex = [Threading.Mutex]::new($false, 'Local\ShiftWheel.Tray')
try { Run-Setup $uninstaller 'running-app-blocks-uninstall' @() $false }
finally { $mutex.Dispose() }
if (-not (Test-Path -LiteralPath (Join-Path $app 'ShiftWheel.exe'))) { throw 'Blocked uninstall removed the application.' }
Run-Setup $uninstaller 'uninstall' @() $true
# Inno's original uninstaller returns before its temporary clone deletes it.
# Keep the harness alive until that cleanup completes; exiting sooner lets a CI
# process job/sandbox tear down the clone and leave only unins000.exe behind.
$cleanupComplete = $false
for ($attempt=0; $attempt -lt 150; $attempt++) {
    $uninstallLog = Get-Content -LiteralPath (Join-Path $TestRoot 'uninstall.log') -Raw
    $cleanupComplete = -not (Test-Path -LiteralPath $uninstaller) -and $uninstallLog.Contains('Uninstallation process succeeded.') -and $uninstallLog.Contains('Log closed.')
    if ($cleanupComplete) { break }
    Start-Sleep -Milliseconds 100
}
if (-not $cleanupComplete) { throw 'The uninstaller cleanup clone did not finish or remove its executable.' }
if (Test-Path -LiteralPath (Join-Path $app 'ShiftWheel.exe')) { throw 'Uninstall did not remove the installed executable.' }
for ($i=0; $i -lt 3; $i++) {
    if ((Get-FileHash -LiteralPath @($ini,$foreign,$nested)[$i]).Hash -ne $userHashes[$i]) { throw 'Uninstall changed user settings/files.' }
}
if ((Test-Path -LiteralPath $key) -ne $beforeKey) { throw 'Isolated test changed application registration.' }
for ($i=0; $i -lt $links.Count; $i++) {
    $now = if (Test-Path -LiteralPath $links[$i]) { (Get-FileHash -LiteralPath $links[$i]).Hash } else { 'absent' }
    if ($now -ne $beforeLinks[$i]) { throw 'Isolated test changed a real user shortcut.' }
}
Write-Host 'PASS: upgrade and uninstall preserve INI/foreign files; no registration or shortcut changes.'
[ordered]@{ passed=$true; setup=$SetupExe; test_root=$TestRoot; installed_files=$payload.files.Count; launched=[bool]$LaunchInstalledApp; media_conversion=[bool]$ConvertInstalledMedia; uninstall_self_cleanup=$true; checked_at=[DateTime]::UtcNow.ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $TestRoot 'results.json') -Encoding UTF8
