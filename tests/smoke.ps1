param([Parameter(Mandatory=$true)][string]$Executable, [ValidateRange(1,30)][int]$IdleSeconds = 2)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ShiftWheelSmoke {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string name);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
}
'@
$process = Start-Process -FilePath $Executable -PassThru -WindowStyle Hidden
try {
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $process.Refresh()
        if ($process.HasExited) { throw 'Application exited before creating its tray controller.' }
        $candidate = [ShiftWheelSmoke]::FindWindow('ShiftWheel.Controller', 'ShiftWheel')
        [uint32]$ownerId = 0
        if ($candidate -ne [IntPtr]::Zero) {
            [void][ShiftWheelSmoke]::GetWindowThreadProcessId($candidate, [ref]$ownerId)
            if ($ownerId -eq $process.Id) { $window = $candidate; break }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Tray controller not found within 5 seconds.' }
    $process.Refresh()
    $before = $process.TotalProcessorTime.TotalMilliseconds
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds $IdleSeconds
    $clock.Stop()
    $process.Refresh()
    $cpuMs = $process.TotalProcessorTime.TotalMilliseconds - $before
    Write-Output ('Idle sample: CPU {0:N1} ms / {1:N0} ms, working set {2:N1} MiB' -f $cpuMs, $clock.Elapsed.TotalMilliseconds, ($process.WorkingSet64 / 1MB))
    [void][ShiftWheelSmoke]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Application did not exit cleanly.' }
    if ($process.ExitCode -ne 0) { throw ('Application failed: ' + $process.ExitCode) }
    Write-Output 'PASS tray startup and clean shutdown'
} finally {
    $process.Refresh()
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
