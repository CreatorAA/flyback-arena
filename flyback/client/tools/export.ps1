param(
    [Parameter(Mandatory)][string]$Godot,
    [Parameter(Mandatory)][string]$Destination,
    [ValidateSet("pack","windows","linux")][string]$Format="pack"
)
$ErrorActionPreference="Stop"
$root=(Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe=(Resolve-Path -LiteralPath $Godot).Path
$out=(New-Item -ItemType Directory -Force -Path $Destination).FullName
& $exe --headless --path $root --import
if($LASTEXITCODE -ne 0){throw "Asset import failed: $LASTEXITCODE"}
$preset=if($Format -eq "linux"){"Linux"}else{"Windows Desktop"}
$name=@{pack="Flyback.pck"; windows="Flyback.exe"; linux="Flyback.x86_64"}[$Format]
$op=if($Format -eq "pack"){"--export-pack"}else{"--export-release"}
& $exe --headless --path $root $op $preset (Join-Path $out $name)
if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath (Join-Path $out $name))){throw "Client export failed: $LASTEXITCODE"}
