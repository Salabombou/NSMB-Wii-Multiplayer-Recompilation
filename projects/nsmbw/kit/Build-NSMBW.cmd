@echo off
rem New Super Mario Bros. Wii build kit: builds the game for this PC from your own disc (see README.txt).
rem Options go to source\projects\nsmbw\tools\build-game.ps1: -Game <folder> -Output <folder> -Jobs N -Compat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0source\projects\nsmbw\tools\build-game.ps1" %*
set code=%errorlevel%
echo.
pause
exit /b %code%
