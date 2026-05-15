@echo off
setlocal enabledelayedexpansion

REM Можно поменять на Debug при необходимости
set BUILD_TYPE=Release

REM Генератор. Если у тебя Ninja установлен, можно заменить на Ninja
set GENERATOR=MinGW Makefiles

if not exist build mkdir build

cmake -S . -B build -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 exit /b 1

cmake --build build -j
if errorlevel 1 exit /b 1

echo.
echo Done
endlocal
