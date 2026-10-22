param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
function HashFile([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToBase64String($sha.ComputeHash($stream)) }
    finally { $sha.Dispose(); $stream.Dispose() }
}
$root = Join-Path (Get-Location).Path ('test-output\worker-' + [guid]::NewGuid().ToString())
[void][System.IO.Directory]::CreateDirectory($root)
$inputFile = Join-Path $root 'original image.png'
$outputFile = Join-Path $root 'result image.jpg'
$temporaryFile = Join-Path $root 'result.tmp'
$bitmap = New-Object System.Drawing.Bitmap 16,16
try { $bitmap.Save($inputFile, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
$before = HashFile $inputFile
$args = '--convert "' + $inputFile + '" "' + $outputFile + '" "' + $temporaryFile + '"'
$child = Start-Process -FilePath $Executable -ArgumentList $args -PassThru -WindowStyle Hidden
if (-not $child.WaitForExit(10000)) { $child.Kill(); throw 'Worker timed out.' }
if ($child.ExitCode -ne 0) { throw ('Worker failed with exit code ' + $child.ExitCode) }
if (-not (Test-Path -LiteralPath $outputFile)) { throw 'No output file.' }
if (Test-Path -LiteralPath $temporaryFile) { throw 'Temporary file left behind.' }
if ((HashFile $inputFile) -ne $before) { throw 'Original modified.' }
$image = [System.Drawing.Bitmap]::FromFile($outputFile)
try {
    $pixel = $image.GetPixel(0,0)
    if ($pixel.R -lt 245 -or $pixel.G -lt 245 -or $pixel.B -lt 245) { throw 'Transparent PNG did not become white JPEG.' }
} finally { $image.Dispose() }
$outputHash = HashFile $outputFile
$child = Start-Process -FilePath $Executable -ArgumentList $args -PassThru -WindowStyle Hidden
if (-not $child.WaitForExit(10000)) { $child.Kill(); throw 'Collision test timed out.' }
if ($child.ExitCode -eq 0 -or (HashFile $outputFile) -ne $outputHash) { throw 'Worker overwrote existing output.' }
Write-Output 'PASS isolated worker, white JPEG, source preservation, collision refusal'
