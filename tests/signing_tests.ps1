#requires -Version 7.2
param([Parameter(Mandatory)][string]$Verifier)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$fixture=Join-Path $root ('build\signing-test-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
$key=[Security.Cryptography.ECDsa]::Create([Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256'))
try {
    $encoding=[Text.UTF8Encoding]::new($false)
    $private=Join-Path $fixture 'test-only-key.pem'
    [IO.File]::WriteAllText($private,$key.ExportPkcs8PrivateKeyPem(),$encoding)
    $parameters=$key.ExportParameters($false)
    [byte[]]$blob=@(0x45,0x43,0x53,0x31,32,0,0,0)+$parameters.Q.X+$parameters.Q.Y
    $public=[Convert]::ToHexString($blob).ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $fixture 'public.txt'),$public,$encoding)
    $payload="version=0.8.0`nurl=https://example.com/setup.exe`nsize=3`nsha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`n"
    [IO.File]::WriteAllText((Join-Path $fixture 'plain.txt'),$payload,$encoding)
    & (Join-Path $root 'scripts\sign-update-manifest.ps1') -Manifest (Join-Path $fixture 'plain.txt') -PrivateKeyPath $private -PublicKeyHexPath (Join-Path $fixture 'public.txt') -KeyId 'test' -Destination (Join-Path $fixture 'signed.txt')
    & $Verifier (Join-Path $fixture 'signed.txt') $public 'test' '0.7.0'
    if ($LASTEXITCODE -ne 0) { throw 'Native verifier rejected PowerShell signature.' }
    Write-Output 'PASS PowerShell/.NET signature accepted by C++ BCrypt verifier'
} finally {
    if($key){$key.Dispose()}
    foreach ($name in @('test-only-key.pem','public.txt','plain.txt','signed.txt')) { $file=Join-Path $fixture $name;if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force } }
    [IO.Directory]::Delete($fixture)
}
