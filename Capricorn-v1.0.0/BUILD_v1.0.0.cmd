@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-v1.0.0.ps1" %*
if errorlevel 1 (
  echo.
  echo Capricorn-v1.0.0 build failed. See the error above.
  pause
  exit /b 1
)
echo.
echo Capricorn-v1.0.0 build completed successfully.
echo Generated: Capricorn-v1.0.0-Windows-x64.zip
echo Generated: Capricorn-v1.0.0-Setup-x64.exe
pause
exit /b 0
