@echo off
setlocal
set "PLUGINS=%APPDATA%\bakkesmod\bakkesmod\plugins"
set "CFG=%APPDATA%\bakkesmod\bakkesmod\cfg\plugins.cfg"

if not exist "%PLUGINS%" (
    echo BakkesMod plugins folder not found: %PLUGINS%
    echo Install BakkesMod and start it once, then run this again.
    pause
    exit /b 1
)

tasklist /fi "imagename eq RocketLeague.exe" 2>nul | find /i "RocketLeague.exe" >nul
if not errorlevel 1 (
    echo Rocket League is running and keeps the old DLL locked. Close it and run this again.
    pause
    exit /b 1
)

if exist "%PLUGINS%\RewardTesterPlugin.dll" copy /y "%PLUGINS%\RewardTesterPlugin.dll" "%PLUGINS%\RewardTesterPlugin.dll.old" >nul
copy /y "%~dp0plugin\RewardTesterPlugin.dll" "%PLUGINS%\" >nul
if errorlevel 1 (
    echo Copy failed.
    pause
    exit /b 1
)

if exist "%CFG%" (
    findstr /i /c:"rewardtesterplugin" "%CFG%" >nul || (echo plugin load rewardtesterplugin>>"%CFG%")
) else (
    echo plugin load rewardtesterplugin>"%CFG%"
)

echo Installed RewardTesterPlugin.dll and set it to load on startup.
pause
