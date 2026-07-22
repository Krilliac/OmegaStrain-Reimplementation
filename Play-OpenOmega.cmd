@echo off
setlocal
set "OPENOMEGA_LAUNCHER=%~dp0scripts\run-openomega.ps1"

if not exist "%OPENOMEGA_LAUNCHER%" (
    echo OpenOmega native launcher is missing: "%OPENOMEGA_LAUNCHER%" 1>&2
    exit /b 2
)

powershell.exe -NoLogo -NoProfile -File "%OPENOMEGA_LAUNCHER%" %*
set "OPENOMEGA_EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %OPENOMEGA_EXIT_CODE%
