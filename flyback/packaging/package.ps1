#requires -Version 7.0
[CmdletBinding()]
param(
 [string]$NativeBin,[string]$GodotExport,[string]$OutRoot,
 [string]$RuntimeBin,[string]$Objdump,
 [ValidateSet('both','game','server','client')][string]$Kind='both'
)
$ErrorActionPreference='Stop'
$project=(Get-Item -LiteralPath (Join-Path $PSScriptRoot '..')).FullName
$repo=(Get-Item -LiteralPath (Join-Path $project '..')).FullName
if(!$OutRoot){$OutRoot=Join-Path $repo $(if($Kind -eq 'client'){'out/交付'}else{'out'})}
if(!$NativeBin){
 $NativeBin=Join-Path $repo 'out/flyback-win/bin'
 # CMake stages the active competition maps next to bin, not inside the development client copy.
 $DataDir=Join-Path $repo 'out/flyback-win/data'
}else{$DataDir=Join-Path $NativeBin 'data'}
$MapDir=Join-Path $DataDir 'maps'
if(!$GodotExport){$GodotExport=Join-Path $project 'client/build/windows'}
if(!$Objdump){$Objdump=(Get-Command objdump.exe -ErrorAction Stop).Source}
if(!$RuntimeBin){$RuntimeBin=(Get-Command g++.exe -ErrorAction Stop).Source | Split-Path}
$NativeBin=(Resolve-Path -LiteralPath $NativeBin).Path
$RuntimeBin=(Resolve-Path -LiteralPath $RuntimeBin).Path
[void][IO.Directory]::CreateDirectory($OutRoot); $OutRoot=(Resolve-Path -LiteralPath $OutRoot).Path
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff')
$stage=Join-Path $OutRoot ('.flyback-stage-'+$stamp);[void][IO.Directory]::CreateDirectory($stage)
$mapNames=@('icefloes','ruins','skybridge','stonecircle','twinlakes','yinyang')+@(Get-ChildItem (Join-Path $project 'native/game/maps') -File -Filter '*.txt'|ForEach-Object BaseName)
$ids=(Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $repo 'boomerang/bots/roster.json')|ConvertFrom-Json).official|ForEach-Object {Split-Path -Leaf $_.dir}
if($ids.Count -ne 13){throw "Official roster is no longer 13 entries. Review packaging and notices before distributing."}
function Copy-File([string]$from,[string]$to){
 if(!(Test-Path -PathType Leaf -LiteralPath $from)){throw "Missing release input: $from"}
 [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to));[IO.File]::Copy($from,$to,$true)
}
function Copy-Runtimes([string]$dir){
 $files=[Collections.Generic.Queue[string]]::new(); $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($exe in Get-ChildItem -Recurse -File -Filter '*.exe' -LiteralPath $dir){$files.Enqueue($exe.FullName)}
 while($files.Count){
  $file=$files.Dequeue();if(!$seen.Add($file)){continue}
  # MinGW objdump handles a Unicode working directory, but not a Unicode path argument.
  Push-Location -LiteralPath ([IO.Path]::GetDirectoryName($file))
  try{$raw=& $Objdump -p ([IO.Path]::GetFileName($file));if($LASTEXITCODE -ne 0){throw "Could not inspect PE imports of $file"}}
  finally{Pop-Location}
  foreach($line in $raw){if($line -notmatch '^\s*DLL Name:\s*([^\s\\/:]+\.dll)\s*$'){continue};$dll=$Matches[1]
   if($dll -match '^(api-ms-win-|ext-ms-win-)'){continue}
   $native=Join-Path $NativeBin $dll;$runtime=Join-Path $RuntimeBin $dll;$dest=Join-Path $dir $dll
   if(Test-Path -LiteralPath $native -PathType Leaf){$source=$native}
   elseif(Test-Path -LiteralPath $runtime -PathType Leaf){$source=$runtime}
   elseif(Test-Path -LiteralPath (Join-Path ([Environment]::SystemDirectory) $dll) -PathType Leaf){continue}
   else{throw "Unresolved import $dll in $file"}
   if(!(Test-Path -LiteralPath $dest)){Copy-File $source $dest}
   $files.Enqueue($dest)
  }
 }
}
function Validate-Child([string]$path,[string]$parent){
 $full=[IO.Path]::GetFullPath($path);$base=[IO.Path]::GetFullPath($parent).TrimEnd('\','/');
 if(![string]::Equals([IO.Path]::GetDirectoryName($full),$base,[StringComparison]::OrdinalIgnoreCase)){throw "Path is outside the expected package parent: $full"}
 if(Test-Path -LiteralPath $full){if(((Get-Item -Force -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Refusing to replace linked path: $full"}}
 return $full
}
function Prepare([string]$name,[bool]$game){
 $dir=Join-Path $stage $name;[void][IO.Directory]::CreateDirectory($dir)
 $withServer=$Kind -ne 'client'
 if($withServer){Copy-File (Join-Path $NativeBin 'arena-server.exe') (Join-Path $dir 'arena-server.exe')}
 if($game){
  foreach($f in 'Flyback.exe','Flyback.pck'){Copy-File (Join-Path $GodotExport $f) (Join-Path $dir $f)}
  Copy-File (Join-Path $NativeBin 'flyback-bridge.exe') (Join-Path $dir 'flyback-bridge.exe')
  Copy-File (Join-Path $PSScriptRoot 'watch-duel.cmd') (Join-Path $dir 'watch-duel.cmd')
  Copy-File (Join-Path $repo 'docs/boomerang-fork/evidence/runtime/twinlakes-seed7.txt') (Join-Path $dir 'examples/twinlakes-seed7.txt')
 }
 foreach($id in $ids){Copy-File (Join-Path $DataDir "bots/$id.exe") (Join-Path $dir "data/bots/$id.exe")}
 $info=Join-Path $DataDir 'bots/build-info.json'
 if(Test-Path -LiteralPath $info -PathType Leaf){Copy-File $info (Join-Path $dir 'data/bots/build-info.json')}
 foreach($m in $mapNames){Copy-File (Join-Path $MapDir "$m.txt") (Join-Path $dir "data/maps/$m.txt")}
 if($withServer){
  foreach($f in 'index.html','styles.css','app.js'){Copy-File (Join-Path $project "admin-web/$f") (Join-Path $dir "admin-web/$f")}
  Copy-File (Join-Path $PSScriptRoot 'server.toml') (Join-Path $dir 'server.toml')
 }
 Copy-File (Join-Path $repo 'LICENSE') (Join-Path $dir 'LICENSE.txt')
 foreach($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'licenses') -File){if(!$game -and $file.Name -match '^(godot-|combat-audio\.txt$)'){continue};Copy-File $file.FullName (Join-Path $dir ('licenses/'+$file.Name))}
 if($game){
  $readme=Get-Content -LiteralPath (Join-Path $repo 'README.md') -Raw -Encoding utf8
  $cmake=Get-Content -LiteralPath (Join-Path $project 'CMakeLists.txt') -Raw -Encoding utf8
  if($cmake -notmatch 'project\(FlybackArena\s+VERSION\s+([0-9.]+)'){throw 'Missing FlybackArena release version.'}
  $ref='v'+$Matches[1];$web="https://github.com/CreatorAA/flyback-arena/blob/$ref/"
  $raw="https://raw.githubusercontent.com/CreatorAA/flyback-arena/$ref/"
  $readme=$readme.Replace('](LICENSE)','](LICENSE.txt)')
  $readme=[regex]::Replace($readme,'(!?\[[^\]]*\]\()((?:docs|flyback)/[^)]+|(?:UPSTREAM|BUILD)\.md)\)',{
   param($m)
   $base=if($m.Groups[1].Value.StartsWith('!')){$raw}else{$web}
   return $m.Groups[1].Value+$base+$m.Groups[2].Value+')'
  })
 }
 else{$readme=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dedicated-windows.md') -Raw -Encoding utf8}
 [IO.File]::WriteAllText((Join-Path $dir 'README.md'),$readme,[Text.UTF8Encoding]::new($false))
 if($game){$start="双击 Flyback.exe 开始游戏。Flyback.pck 与 flyback-bridge.exe 必须和它留在同一目录。无需安装 Godot、Blender、编译器或 Python。手柄或键鼠均可操作。`r`n`r`n"}
 else{$start="本包是 Windows x64 无头专服，无需显卡和 Godot。`r`n`r`n"}
 if($withServer){$start+="在本目录打开 PowerShell，执行：`r`n  .\arena-server.exe --config server.toml check-config`r`n  .\arena-server.exe --config server.toml admin init`r`n  .\arena-server.exe --config server.toml`r`n`r`n首次初始化管理员时在终端输入密码；专服启动后显示真实游戏 UDP 端口和管理台 TCP 端点。游戏大厅使用游戏 UDP 地址；跨网时按实际网络映射 UDP。配置、密钥、存档与回放由服主保管。控制台输入 stop 平滑停服。`r`n"}else{$start+="双击 watch-duel.cmd 看 Astra 对 Fable 的原局。局域网建房与连接专服均在游戏内操作。`r`n"}
 $start+="本衍生版的游戏开发、联机服务和发行由 Codex 和 ChatGPT 完成；专服 Web 管理台前端由 Gemini 贡献。`r`n"
 [IO.File]::WriteAllText((Join-Path $dir '开始使用.txt'),$start,[Text.UTF8Encoding]::new($true))
 Copy-Runtimes $dir
 $list=[string[]]@(Get-ChildItem -LiteralPath $dir -Recurse -File|ForEach-Object {[IO.Path]::GetRelativePath($dir,$_.FullName).Replace('\','/')});[Array]::Sort($list,[StringComparer]::Ordinal)
 [IO.File]::WriteAllLines((Join-Path $dir 'distribution-files.txt'),$list,[Text.UTF8Encoding]::new($false))
 $zip=Join-Path $stage "$name.zip";[IO.Compression.ZipFile]::CreateFromDirectory($dir,$zip,[IO.Compression.CompressionLevel]::Optimal,$withServer)
 return $name
}
$names=@();if($Kind -in 'both','game'){$names+=Prepare 'Flyback-Windows-x64' $true};if($Kind -in 'both','server'){$names+=Prepare 'Flyback-Server-Windows-x64' $false};if($Kind -eq 'client'){$names+=Prepare '飞返竞技场-客户端' $true}
# Stage and zip from an allowlist first. Never fold existing keys/replays/personal settings into distribution.
foreach($name in $names){
 $target=Validate-Child (Join-Path $OutRoot $name) $OutRoot
 if(Test-Path -LiteralPath $target){
  $list=Join-Path $target 'distribution-files.txt';if(!(Test-Path -LiteralPath $list)){throw "Not a generated package; refusing to move $target. Use -OutRoot for a new location."}
  $allowed=@{};foreach($entry in Get-Content -LiteralPath $list -Encoding utf8){$allowed[$entry]=$true};$allowed['distribution-files.txt']=$true
  foreach($f in Get-ChildItem -LiteralPath $target -Recurse -File -Force){$rel=[IO.Path]::GetRelativePath($target,$f.FullName).Replace('\','/');if(!$allowed.ContainsKey($rel)){throw "Package contains local data: $rel. Leave it in place and specify another -OutRoot."}}
  $oldZip=Join-Path $OutRoot "$name.zip"
  if(!(Test-Path -LiteralPath $oldZip -PathType Leaf)){throw 'Existing package lacks its original archive; choose a new -OutRoot.'}
  if($Kind -ne 'client'){
  $archive=[IO.Compression.ZipFile]::OpenRead($oldZip)
  try {
   $entry=$archive.GetEntry("$name/server.toml");if(!$entry){throw 'Original archive lacks server.toml.'}
   $reader=[IO.StreamReader]::new($entry.Open());try{$original=$reader.ReadToEnd()}finally{$reader.Dispose()}
  } finally {$archive.Dispose()}
  if([IO.File]::ReadAllText((Join-Path $target 'server.toml')) -cne $original){throw 'Packaged server.toml has local edits. Use a new output folder to preserve it.'}
  }
  $prefix=$target.TrimEnd('\')+'\';foreach($proc in Get-Process){try{$exe=$proc.Path}catch{continue};if($exe -and $exe.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "A packaged process is running: $exe. Do not replace its directory."}}
 }elseif(Test-Path -LiteralPath (Join-Path $OutRoot "$name.zip")){throw "Existing archive lacks a corresponding generated directory: $name.zip"}
}
# Keep replaced copies in an adjacent backup until the owner explicitly disposes of them.
$previous=Join-Path $OutRoot ('.flyback-previous-'+$stamp)
foreach($name in $names){
 foreach($leaf in $name,"$name.zip"){
  $backup=$null
  $to=Validate-Child (Join-Path $OutRoot $leaf) $OutRoot;$from=Validate-Child (Join-Path $stage $leaf) $stage
  if(Test-Path -LiteralPath $to){[void][IO.Directory]::CreateDirectory($previous);$backup=Validate-Child (Join-Path $previous $leaf) $previous;Move-Item -LiteralPath $to -Destination $backup}
  try{Move-Item -LiteralPath $from -Destination $to}catch{if($backup -and (Test-Path -LiteralPath $backup) -and !(Test-Path -LiteralPath $to)){Move-Item -LiteralPath $backup -Destination $to};throw}
 }
 $size=(Get-Item -LiteralPath (Join-Path $OutRoot "$name.zip")).Length;Write-Host "$name.zip  $([Math]::Round($size/1MB,1)) MiB"
}
if(!(Get-ChildItem -LiteralPath $stage -Force)){[IO.Directory]::Delete((Validate-Child $stage $OutRoot),$false)}
