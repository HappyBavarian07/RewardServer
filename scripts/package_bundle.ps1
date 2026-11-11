param(
    [string]$PluginDll = "$env:APPDATA\bakkesmod\bakkesmod\plugins\RewardTesterPlugin.dll",
    [string]$OutZip = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'RewardServerBundle.zip')
)
$ErrorActionPreference = 'Stop'
$kit = Split-Path $PSScriptRoot -Parent
$stage = Join-Path ([IO.Path]::GetTempPath()) ('rtbm_' + [guid]::NewGuid().ToString('N'))
$root = Join-Path $stage 'RewardServerBundle'
$kitOut = Join-Path $root 'RewardServer'

New-Item -ItemType Directory $kitOut, "$root\plugin" -Force | Out-Null
foreach ($dir in 'reward_server', 'rlpr', 'examples', 'docs') {
    Copy-Item (Join-Path $kit $dir) $kitOut -Recurse
}
Get-ChildItem $kitOut -Recurse -Include build, __pycache__ -Directory | Remove-Item -Recurse -Force
Copy-Item $PluginDll "$root\plugin\RewardTesterPlugin.dll"
Copy-Item "$kit\bundle\install_plugin.bat", "$kit\bundle\README.md" $root

if (Test-Path $OutZip) { Remove-Item $OutZip }
Compress-Archive -Path $root -DestinationPath $OutZip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force
Get-Item $OutZip | ForEach-Object { '{0} {1:N2} MB' -f $_.FullName, ($_.Length / 1MB) }
