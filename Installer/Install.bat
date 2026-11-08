@echo off
rem Starts the installer (PowerShell GUI). Double-click this file.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-AIBlueprintGen.ps1" %*
if errorlevel 1 pause
