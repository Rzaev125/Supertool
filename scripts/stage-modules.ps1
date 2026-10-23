param([Parameter(Mandatory=$true)][string]$ModuleRoot,[Parameter(Mandatory=$true)][string]$InventoryDirectory,[Parameter(Mandatory=$true)][string]$CatalogPath,[Parameter(Mandatory=$true)][string]$Destination,[switch]$PackagedSource,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent)).TrimEnd('\')
function Assert-Local([string]$Path){
 $full=[IO.Path]::GetFullPath($Path)
 if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Module staging path must stay inside the workspace.'}
 $p=$full
 while($p -and $p -ne $root){
  if((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw ('Reparse point in module staging: '+$p)}
  $p=Split-Path $p -Parent
 }
 return $full
}
foreach($p in @($ModuleRoot,$InventoryDirectory,$CatalogPath,$Destination)){[void](Assert-Local $p)}
if(Test-Path -LiteralPath $Destination){throw 'Module destination must be fresh.'}
$catalog=Get-Content -LiteralPath $CatalogPath -Raw -Encoding UTF8 | ConvertFrom-Json
$ids=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$plans=@()
foreach($m in $catalog){
 if($m.id -notmatch '^[a-z]+$' -or -not $ids.Add($m.id)){throw 'Invalid pinned module ID.'}
 $source=Assert-Local (Join-Path $ModuleRoot $m.id)
 $inv=if($PackagedSource){Assert-Local (Join-Path $source '.sw-inventory')}else{Assert-Local (Join-Path $InventoryDirectory ($m.id+'.inventory'))}
 if((Get-FileHash -LiteralPath $inv -Algorithm SHA256).Hash -ne $m.inventory_sha256){throw ('Pinned inventory hash mismatch: '+$m.id)}
 $lines=[IO.File]::ReadAllLines($inv)
 if($lines.Count -lt 2 -or $lines[0] -cne 'SWI1'){throw 'Invalid pinned inventory.'}
 $expected=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
 [UInt64]$total=(Get-Item -LiteralPath $inv).Length
 foreach($line in $lines | Select-Object -Skip 1){
  if($line -cnotmatch '^([a-f0-9]{64}) ([0-9]{1,12}) (.+)$'){throw 'Invalid inventory row.'}
  $hash=$Matches[1];[UInt64]$size=$Matches[2];$relative=$Matches[3]
  if($relative.Length -gt 220 -or $relative -cmatch '[\x00-\x1f\x7f-\uffff\\:"<>|?*]' -or $relative -in @('.sw-module','.sw-inventory')){throw 'Unsafe inventory path.'}
  foreach($part in $relative.Split('/')){if(-not $part -or $part -in @('.','..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)'){throw 'Unsafe inventory path.'}}
  if($expected.ContainsKey($relative) -or $size -gt 2147483647){throw 'Duplicate/oversize inventory file.'}
  $expected.Add($relative,[pscustomobject]@{path=$relative;bytes=$size;hash=$hash});$total+=$size
 }
 if($expected.Count+1 -ne $m.count -or $total -ne $m.unpacked){throw 'Pinned module bounds mismatch.'}
 if($PackagedSource){
  $receipt=Assert-Local (Join-Path $source '.sw-module')
  if([IO.File]::ReadAllText($receipt) -cne ($m.id+"`n"+$m.version+"`n"+$m.inventory_sha256)){throw 'Pinned module receipt mismatch.'}
 }
 $queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($source);$found=0
 while($queue.Count){
  foreach($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force){
   if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse point in module source.'}
   if($item.PSIsContainer){$queue.Enqueue($item.FullName);continue}
   $relative=$item.FullName.Substring($source.Length+1).Replace('\','/')
   if($PackagedSource -and $relative -cin @('.sw-inventory','.sw-module')){continue}
   if(-not $expected.ContainsKey($relative)){throw ('Unexpected module file: '+$m.id+'/'+$relative)}
   if($item.Length -ne $expected[$relative].bytes){throw ('Modified module file: '+$relative)}
   if($ValidateOnly -and (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash -ne $expected[$relative].hash){throw ('Modified module hash: '+$relative)}
   $found++
  }
 }
 if($found -ne $expected.Count){throw 'Missing pinned module files.'}
 $plans+= [pscustomobject]@{spec=$m;source=$source;inventory=$inv;files=$expected.Values}
}
foreach($item in Get-ChildItem -LiteralPath $ModuleRoot -Force){if(-not $item.PSIsContainer -or -not $ids.Contains($item.Name) -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected module root content.'}}
if($ValidateOnly){Write-Host 'PASS: packaged module tree matches pinned inventory.';return}
foreach($plan in $plans){
 $dir=Join-Path $Destination $plan.spec.id
 foreach($file in $plan.files){
  $target=Join-Path $dir $file.path
  [void][IO.Directory]::CreateDirectory((Split-Path $target -Parent))
  Copy-Item -LiteralPath (Assert-Local (Join-Path $plan.source $file.path)) -Destination $target
  if((Get-Item -LiteralPath $target).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.hash){throw ('Staged module hash mismatch: '+$file.path)}
 }
 Copy-Item -LiteralPath $plan.inventory -Destination (Join-Path $dir '.sw-inventory')
 if((Get-FileHash -LiteralPath (Join-Path $dir '.sw-inventory') -Algorithm SHA256).Hash -ne $plan.spec.inventory_sha256){throw 'Staged inventory hash mismatch.'}
 [IO.File]::WriteAllText((Join-Path $dir '.sw-module'),($plan.spec.id+"`n"+$plan.spec.version+"`n"+$plan.spec.inventory_sha256),[Text.UTF8Encoding]::new($false))
}
Write-Host 'PASS: staged exactly the pinned module inventory; all copied hashes verified.'
