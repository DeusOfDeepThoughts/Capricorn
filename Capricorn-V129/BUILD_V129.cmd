@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-v129.ps1"
if errorlevel 1 (
  echo.
  echo Capricorn-V129 build failed. See the error above.
  pause
  exit /b 1
)
echo.
echo Capricorn-V129 build completed successfully.
echo Generated: Capricorn-V129-Windows-x64.zip
echo Generated: Capricorn-V129-Setup-x64.exe
pause
exit /b 0
