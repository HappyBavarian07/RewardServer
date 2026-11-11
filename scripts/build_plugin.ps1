param(
    [Parameter(Mandatory)][string]$FrameworkRoot,
    [string]$Config = 'Release',
    [string[]]$DataFiles = @(),
    [switch]$NoDeploy
)
$ErrorActionPreference = 'Stop'
$kit = Split-Path $PSScriptRoot -Parent
$source = Join-Path $kit 'BakkesModPlugin'
$build = Join-Path $source 'build'
$bakkes = Join-Path $env:APPDATA 'bakkesmod\bakkesmod'

cmake -S $source -B $build "-DRS_FRAMEWORK_ROOT=$FrameworkRoot"
if ($LASTEXITCODE) { throw 'configure failed' }
cmake --build $build --config $Config
if ($LASTEXITCODE) { throw 'build failed' }

$dll = Join-Path $build "$Config\RewardTesterPlugin.dll"
if (-not (Test-Path $dll)) { throw "missing $dll" }
Write-Host "built $dll"
if ($NoDeploy) { return }

if (Get-Process RocketLeague -ErrorAction SilentlyContinue) {
    throw 'Rocket League is running and keeps the plugin DLL locked; close it or pass -NoDeploy.'
}
$plugins = Join-Path $bakkes 'plugins'
$installed = Join-Path $plugins 'RewardTesterPlugin.dll'
if (Test-Path $installed) { Move-Item $installed "$installed.old" -Force }
Copy-Item $dll $plugins

if ($DataFiles.Count) {
    $data = Join-Path $bakkes 'data'
    New-Item -ItemType Directory $data -Force | Out-Null
    Copy-Item $DataFiles $data
}
Write-Host "deployed to $plugins"
