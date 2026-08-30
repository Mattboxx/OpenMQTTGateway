@echo off
chcp 65001 >NUL
title OpenMQTTGateway - Flash USB guidato
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-usb.ps1" %*
echo.
pause
