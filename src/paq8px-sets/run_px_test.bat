@echo off
cd /d "%~dp0"
set FAILED=0
for %%T in (T4 T5 T3 T2 T6 T8 T7 T1) do (
  px_test.exe %%T
  if errorlevel 1 set FAILED=1
)
if %FAILED%==1 (echo ONE OR MORE TESTS FAILED & exit /b 1)
echo ALL TESTS PASSED
exit /b 0
