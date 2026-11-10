@echo off
rem Runs the HX 1.1.0 PC test. Results: results-HX-1.1.0-PC.txt in this folder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0hx-1.1.0-pc-test.ps1"
echo.
echo Test finished. Results are in results-HX-1.1.0-PC.txt
pause
