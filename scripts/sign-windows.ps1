param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$CertificateThumbprint,
    [string]$TimestampServer='https://timestamp.digicert.com'
)
$ErrorActionPreference='Stop'
if ($CertificateThumbprint -notmatch '^[a-fA-F0-9]{40}$' -or $TimestampServer -notmatch '^https://[^\s]+$') { throw 'Invalid certificate or timestamp URL.' }
$certificate=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$CertificateThumbprint)
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or $certificate.NotBefore -gt (Get-Date) -or -not ($certificate.EnhancedKeyUsageList.ObjectId.Value -contains '1.3.6.1.5.5.7.3.3')) { throw 'A valid code-signing certificate with private key is required.' }
$signature=Set-AuthenticodeSignature -LiteralPath $Path -Certificate $certificate -HashAlgorithm SHA256 -TimestampServer $TimestampServer
if ($signature.Status -ne 'Valid' -or (Get-AuthenticodeSignature -LiteralPath $Path).Status -ne 'Valid') { throw 'Authenticode signature verification failed.' }
Write-Output 'Authenticode signature and timestamp verified.'
