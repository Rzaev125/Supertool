# Only owned installer staging trees may be removed, never payloads or releases.
function Assert-InstallerLocalPath([string]$Root, [string]$Path) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $full.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw ('Installer path must stay inside this workspace: ' + $full) }
    $ancestor = $full
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw ('Reparse point in installer path: ' + $ancestor) }
        $ancestor = Split-Path $ancestor -Parent
    }
}

function Remove-InstallerWorkspace([string]$Root, [string]$Path) {
    Assert-InstallerLocalPath $Root $Path
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $parent = Split-Path $full -Parent
    $leaf = Split-Path $full -Leaf
    $allowed = ($parent -eq (Join-Path $Root 'tools') -and $leaf -match '^installer-tool-stage-[0-9a-f]{32}$') -or
        ($parent -eq (Join-Path $Root 'build') -and $leaf -match '^installer-build-[0-9a-f]{32}$')
    if (-not $allowed) { throw ('Refusing to remove a non-temporary installer directory: ' + $full) }
    if (-not (Test-Path -LiteralPath $full)) { return }
    # Inspect one level at a time so no directory link is traversed.
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($full)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse point in installer temporary directory: ' + $item.FullName) }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
    Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction Stop
}
