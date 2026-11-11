@echo off
rem usage: build_plugin.bat -FrameworkRoot <path to a GigaLearnCPP checkout> [-NoDeploy] [-DataFiles a.json,b.json]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\build_plugin.ps1" %*
