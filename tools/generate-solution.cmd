@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM Regenerates the local Winchisel.sln solution file, which is intentionally
REM not committed to git (see .gitignore). Run once after cloning:
REM
REM   tools\generate-solution.cmd
REM
REM Afterwards open Winchisel.sln in Visual Studio or build with:
REM   nuget restore src\app\packages.config -PackagesDirectory packages
REM   msbuild Winchisel.sln /p:Configuration=Debug /p:Platform=x64 /m
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0generate-solution.ps1" || exit /b 1
