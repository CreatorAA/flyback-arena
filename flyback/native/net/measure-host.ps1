param(
 [Parameter(Mandatory)][string]$Server,
 [Parameter(Mandatory)][string]$Assets,
 [Parameter(Mandatory)][string]$LoadTool,
 [Parameter(Mandatory)][string]$Output,
 [ValidateSet('idle','match')][string]$Mode='match',
 [ValidateRange(10,7200)][int]$Seconds=1800
)
$ErrorActionPreference='Stop'
$Server=(Resolve-Path -LiteralPath $Server).Path; $Assets=(Resolve-Path -LiteralPath $Assets).Path; $LoadTool=(Resolve-Path -LiteralPath $LoadTool).Path
if(Test-Path -LiteralPath $Output){throw "Choose a new output directory: $Output"}
$Output=(New-Item -ItemType Directory -Path $Output).FullName
$running=@()
function New-Server([string]$name) {
 $where=(New-Item -ItemType Directory -Path (Join-Path $Output $name)).FullName
 $toml=@"
[server]
name="Flyback local capacity fixture"
listen="127.0.0.1:0"
max_rooms=128
max_active_matches=4
max_spectators_per_room=128
idle_room_ttl_seconds=7200
[storage]
data_dir="./data"
[admin.web]
enabled=false
[replays]
enabled=false
"@
 $config=Join-Path $where 'server.toml'; [IO.File]::WriteAllText($config,$toml,[Text.UTF8Encoding]::new($false))
 $si=[Diagnostics.ProcessStartInfo]::new($Server);$si.CreateNoWindow=$true;$si.UseShellExecute=$false;$si.RedirectStandardOutput=$true;$si.RedirectStandardInput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$where
 foreach($a in '--config',$config,'--assets',$Assets){$si.ArgumentList.Add($a)}
 $proc=[Diagnostics.Process]::Start($si);$err=$proc.StandardError.ReadToEndAsync()
 $line=$proc.StandardOutput.ReadLine();if(!$line){throw "startup failed: $($err.Result)"}
 $ready=$line|ConvertFrom-Json;if($ready.type -ne 'server_started'){throw "Unexpected first line: $line"}
 [pscustomobject]@{Name=$name;P=$proc;Port=$ready.udp_port;Pin=$ready.fingerprint;Out=$proc.StandardOutput.ReadToEndAsync();Err=$err}
}
function Run-Load($h,[string]$mode){
 & $LoadTool $mode $h.Port $h.Pin | Tee-Object -FilePath (Join-Path $Output "$mode.txt")
 if($LASTEXITCODE -ne 0){throw "fixture client failed: $LASTEXITCODE"}
}
function Meter($h){$h.P.Refresh(); [pscustomobject]@{cpu=$h.P.TotalProcessorTime.TotalSeconds;ws=$h.P.WorkingSet64;private=$h.P.PrivateMemorySize64;threads=$h.P.Threads.Count}}
function Result($h,$begin,$end,[double]$sec){[ordered]@{name=$h.Name;pid=$h.P.Id;one_core_cpu_pct=100*($end.cpu-$begin.cpu)/$sec;working_set_mib=$end.ws/1MB;private_mib=$end.private/1MB;threads=$end.threads}}
try{
 if($Mode -eq 'idle'){
  $base=New-Server baseline;$running+= $base; $full=New-Server hundred_empty;$running+= $full
  Run-Load $base warm;Run-Load $full empty;Start-Sleep -Seconds 3
  $b=Meter $base;$f=Meter $full;$clock=[Diagnostics.Stopwatch]::StartNew();$last=0
  while($clock.Elapsed.TotalSeconds -lt $Seconds){
    $wait=[Math]::Min(60,$Seconds-$clock.Elapsed.TotalSeconds);if($wait -gt 0){Start-Sleep -Milliseconds ([int](1000*$wait))}
    $elapsed=$clock.Elapsed.TotalSeconds;$bm=Meter $base;$fm=Meter $full
    $report=[ordered]@{mode='idle';seconds=$elapsed;baseline=(Result $base $b $bm $elapsed);hundred_empty=(Result $full $f $fm $elapsed);rss_delta_mib=($fm.ws-$bm.ws)/1MB}
    $report|ConvertTo-Json -Depth 5 -Compress |Tee-Object -FilePath (Join-Path $Output 'idle-samples.jsonl') -Append
  }
 }else{
  $h=New-Server match;$running+=$h
  $si=[Diagnostics.ProcessStartInfo]::new($LoadTool);$si.CreateNoWindow=$true;$si.UseShellExecute=$false;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
  foreach($a in 'match',([string]$h.Port),$h.Pin){$si.ArgumentList.Add($a)}
  $load=[Diagnostics.Process]::Start($si);$err=$load.StandardError.ReadToEndAsync();$first=$load.StandardOutput.ReadLine()
  if($first -ne 'steady_window_start'){throw "load failed before steady window: $first $($err.Result)"}
  $b=Meter $h;$clock=[Diagnostics.Stopwatch]::StartNew();$line=$load.StandardOutput.ReadLine();$elapsed=$clock.Elapsed.TotalSeconds;$f=Meter $h
  $load.WaitForExit();if(!$line){throw "load failed: $($err.Result)"}
  $report=[ordered]@{mode='match';process=(Result $h $b $f $elapsed);network=($line|ConvertFrom-Json);host=$env:COMPUTERNAME;cpu=(Get-CimInstance Win32_Processor |Select-Object -First 1 -ExpandProperty Name)}
  $report|ConvertTo-Json -Depth 6|Tee-Object -FilePath (Join-Path $Output 'match.json')
  if($load.ExitCode -ne 0){throw "load did not meet its criteria: $($err.Result)"}
 }
}finally{
 foreach($h in $running){
  if(!$h.P.HasExited){$h.P.StandardInput.WriteLine('stop');$h.P.StandardInput.Flush();if(!$h.P.WaitForExit(15000)){throw "fixture PID $($h.P.Id) did not stop"}}
  [IO.File]::WriteAllText((Join-Path $Output "$($h.Name).log"),$h.Out.Result+$h.Err.Result)
 }
}
