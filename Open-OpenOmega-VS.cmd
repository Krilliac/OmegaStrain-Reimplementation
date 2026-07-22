@echo off
setlocal
set "OPENOMEGA_ROOT=%~dp0"
set "OPENOMEGA_BUILD=%~dp0build\vs2022-x64"
set "OPENOMEGA_CMAKE=cmake.exe"

where cmake.exe >nul 2>nul
if not errorlevel 1 goto cmake_ready
set "OPENOMEGA_CMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if exist "%OPENOMEGA_CMAKE%" goto cmake_ready
echo CMake is required to configure the OpenOmega Visual Studio solution. 1>&2
exit /b 2

:cmake_ready
pushd "%OPENOMEGA_ROOT%" >nul
if errorlevel 1 (
    echo Unable to enter the OpenOmega repository: "%OPENOMEGA_ROOT%" 1>&2
    exit /b 2
)

"%OPENOMEGA_CMAKE%" --preset vs2022-x64
set "OPENOMEGA_EXIT_CODE=%ERRORLEVEL%"
if not "%OPENOMEGA_EXIT_CODE%"=="0" goto finish

rem cmake --open launches the generated Visual Studio 2022 solution; it does not build it.
"%OPENOMEGA_CMAKE%" --open "%OPENOMEGA_BUILD%"
set "OPENOMEGA_EXIT_CODE=%ERRORLEVEL%"

:finish
popd
endlocal & exit /b %OPENOMEGA_EXIT_CODE%
