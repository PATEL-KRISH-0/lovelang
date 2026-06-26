@echo off
:: run_love.cmd — Run a Lovelang source file on Windows.
::
:: Usage:
::   run_love.cmd [file.love] [lovelang options...]
::
:: Examples:
::   run_love.cmd examples\01-romantic-hello.love
::   run_love.cmd myfile.love --mode shayari
::   run_love.cmd myfile.love --native --out build\myapp.exe
::   run_love.cmd myfile.love --tokens

setlocal EnableDelayedExpansion

:: ── locate binary ────────────────────────────────────────────────────────────
set "SCRIPT_DIR=%~dp0"
set "BIN="

if exist "%SCRIPT_DIR%lovelang.exe" (
  set "BIN=%SCRIPT_DIR%lovelang.exe"
  goto :found_bin
)
if exist "%SCRIPT_DIR%lovelang" (
  set "BIN=%SCRIPT_DIR%lovelang"
  goto :found_bin
)

:: Fall back to system PATH
where lovelang >nul 2>&1
if %ERRORLEVEL% equ 0 (
  set "BIN=lovelang"
  goto :found_bin
)

echo [ERROR] lovelang binary not found.
echo         Build it with:  make
echo         Or install via: npm install -g lovelang-cli
exit /b 1

:found_bin

:: ── resolve file argument ─────────────────────────────────────────────────────
set "FILE=%~1"
if "%FILE%"=="" set "FILE=examples\01-romantic-hello.love"

:: Check if it's a flag passthrough (e.g. --help), not a file
if "%FILE:~0,2%"=="--" goto :run_direct

:: Sanity-check the file exists
if not exist "%FILE%" (
  echo [ERROR] File not found: %FILE%
  exit /b 1
)

:: ── build argument list (skip first arg which is FILE, pass the rest) ─────────
:: CMD's %* always includes all args. We reconstruct args after the first.
set "EXTRA_ARGS="
set "ARG_INDEX=0"
for %%A in (%*) do (
  if !ARG_INDEX! gtr 0 (
    set "EXTRA_ARGS=!EXTRA_ARGS! %%A"
  )
  set /a ARG_INDEX+=1
)

:: ── run ──────────────────────────────────────────────────────────────────────
"%BIN%" "%FILE%"%EXTRA_ARGS%
exit /b %ERRORLEVEL%

:run_direct
"%BIN%" %*
exit /b %ERRORLEVEL%
