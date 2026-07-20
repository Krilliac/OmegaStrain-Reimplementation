@echo off
setlocal
cd /d "%~dp0" || exit /b 1
"%~dp0openomega.exe" %*
exit /b %ERRORLEVEL%
