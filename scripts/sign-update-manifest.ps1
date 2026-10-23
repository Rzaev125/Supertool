#requires -Version 7.2
param(
    [Parameter(Mandatory)][string]$Manifest,
    [Parameter(Mandatory)][string]$PrivateKeyPath,
    [Parameter(Mandatory)][string]$PublicKeyHexPath,
    [Parameter(Mandatory)][string]$KeyId,
    [Parameter(Mandatory)][string]$Destination
)
$ErrorActionPreference='Stop'
if ([IO.Path]::GetFullPath($PrivateKeyPath) -eq [IO.Path]::GetFullPath($Destination)) { throw 'Output must not replace the private key.' }
if ($KeyId -notmatch '^[A-Za-z0-9_-]{1,64}$') { throw 'Invalid key identifier.' }
$encoding=[Text.UTF8Encoding]::new($false,$true)
$payload=[IO.File]::ReadAllText([IO.Path]::GetFullPath($Manifest),$encoding).Replace("`r`n","`n")
if (-not $payload.EndsWith("`n") -or $payload.Contains("`r") -or $payload.Length -gt 14000 -or $payload -match '(?m)^(signature|key_id)=') { throw 'Expected an unsigned LF-terminated manifest.' }
$publicHex=[IO.File]::ReadAllText([IO.Path]::GetFullPath($PublicKeyHexPath)).Trim().ToLowerInvariant()
if ($publicHex -notmatch '^[a-f0-9]{144}$') { throw 'Expected a P-256 BCRYPT public blob.' }
$key=[Security.Cryptography.ECDsa]::Create()
try {
    $key.ImportFromPem([IO.File]::ReadAllText([IO.Path]::GetFullPath($PrivateKeyPath)))
    if ($key.KeySize -ne 256) { throw 'Only ECDSA P-256 is supported.' }
    $parameters=$key.ExportParameters($false)
    [byte[]]$blob=@(0x45,0x43,0x53,0x31,32,0,0,0)+$parameters.Q.X+$parameters.Q.Y
    if ([Convert]::ToHexString($blob).ToLowerInvariant() -cne $publicHex) { throw 'Signing key does not match the public pin.' }
    $data=$encoding.GetBytes("ShiftWheel-update-v1`nkey_id=$KeyId`n"+$payload)
    $signature=$key.SignData($data,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
    $signed=$payload+"key_id=$KeyId`nsignature="+[Convert]::ToHexString($signature).ToLowerInvariant()+"`n"
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Destination),$signed,$encoding)
} finally { $key.Dispose() }
Write-Output 'Manifest signed; verify it with the native verifier before publishing.'
