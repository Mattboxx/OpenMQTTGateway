@echo off
chcp 65001 >NUL
title OpenMQTTGateway - Flash USB alternativo ROM
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-usb.ps1" -Rom %*
echo.
pause
