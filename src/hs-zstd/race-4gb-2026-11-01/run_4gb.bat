@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prep2_4gb.ps1" > prep2_log.txt 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0race2_4gb.ps1" > race2_log.txt 2>&1
echo DONE >> race2_log.txt
