function Get-TestArtifactRoot([string]$ProjectRoot) {
    $base=if ($env:SHIFTWHEEL_TEST_ARTIFACT_ROOT) { $env:SHIFTWHEEL_TEST_ARTIFACT_ROOT } else { Join-Path $ProjectRoot 'build' }
    . (Join-Path $ProjectRoot 'scripts\installer-workspace.ps1')
    Assert-InstallerLocalPath $ProjectRoot $base
    return [IO.Path]::GetFullPath($base)
}
