@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_ablation2.ps1" > run_log2.txt 2>&1
echo DONE >> run_log2.txt
