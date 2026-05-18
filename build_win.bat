@echo off
setlocal enabledelayedexpansion

set BUILD_TYPE=Release

set GENERATOR=Ninja

if not exist build mkdir build

cmake -S . -B build -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 exit /b 1

cmake --build build -j
if errorlevel 1 exit /b 1

echo.
echo Done
endlocal
