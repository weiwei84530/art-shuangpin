@echo off
rem Art Shuangpin IME - double-click uninstaller.
rem Elevates (UAC prompt) and runs install.ps1 -Uninstall from this folder.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-NoExit','-File','\"%~dp0install.ps1\"','-Uninstall'"
