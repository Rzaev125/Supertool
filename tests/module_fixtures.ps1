param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work=Join-Path $root ('build\module-fixture-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($work)
[IO.File]::WriteAllText((Join-Path $work 'file.txt'),'fixture',[Text.Encoding]::ASCII)
$hash=(Get-FileHash -LiteralPath (Join-Path $work 'file.txt') -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $work 'inventory.txt'),"SWI1`n$hash 7 sub/file.txt`n",[Text.Encoding]::ASCII)
foreach($kind in @('good','duplicate','traversal','collision')){
 $lines=@('.OPTION EXPLICIT','.Set Cabinet=ON','.Set Compress=ON','.Set CompressionType=MSZIP','.Set MaxDiskSize=0',('.Set CabinetNameTemplate='+$kind+'.cab'),('.Set DiskDirectoryTemplate='+$work),('.Set RptFileName='+$work+'\'+$kind+'.rpt'),('.Set InfFileName='+$work+'\'+$kind+'.inf'))
 $name=if($kind -eq 'traversal'){'sub/filex.txt'}else{'sub/file.txt'}
 $lines+=('"'+$work+'\file.txt" "'+$name+'"')
 if($kind -eq 'duplicate'){$lines+=('"'+$work+'\file.txt" "sub/file.tx2"')}
 if($kind -eq 'collision'){$lines+=('"'+$work+'\file.txt" "sub/file.tx3"')}
 $lines+=('"'+$work+'\inventory.txt" ".sw-inventory"')
 [IO.File]::WriteAllLines((Join-Path $work ($kind+'.ddf')),$lines,[Text.Encoding]::ASCII)
 & makecab.exe /F (Join-Path $work ($kind+'.ddf')) > (Join-Path $work ($kind+'.log'))
 if($LASTEXITCODE -ne 0){throw ('makecab fixture failed: '+$kind)}
 $from='';$to=''
 if($kind -eq 'duplicate'){$from='sub/file.tx2';$to='sub/file.txt'}
 if($kind -eq 'collision'){$from='sub/file.tx3';$to='SUB/FILE.txt'}
 if($kind -eq 'traversal'){$from='sub/filex.txt';$to='../escape.txt'}
 if($from){
  $path=Join-Path $work ($kind+'.cab');$bytes=[IO.File]::ReadAllBytes($path)
  $text=[Text.Encoding]::ASCII.GetString($bytes);$index=$text.IndexOf($from,[StringComparison]::Ordinal)
  if($index -lt 0){throw 'Fixture filename not found'}
  [Array]::Copy([Text.Encoding]::ASCII.GetBytes($to),0,$bytes,$index,$to.Length)
  [IO.File]::WriteAllBytes($path,$bytes)
 }
}
& $Executable $work
if($LASTEXITCODE -ne 0){throw 'Module tests failed'}
