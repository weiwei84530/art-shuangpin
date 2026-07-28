@echo off
rem Art Shuangpin IME - double-click installer.
rem Elevates (UAC prompt) and runs install.ps1 from this folder; the
rem window stays open so the result/messages remain visible.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-NoExit','-File','\"%~dp0install.ps1\"'"
