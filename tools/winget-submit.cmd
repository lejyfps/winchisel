@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM Submits a Winchisel release to winget-pkgs.
REM Usage: tools\winget-submit.cmd 1.2.3[.4] [--new]
REM   1.2.3[.4] Version without leading v (must match the GitHub release tag v1.2.3).
REM   --new   First submission (package does not exist in winget-pkgs yet, interactive).
REM Without --new this runs: komac update <id> --version <v> --urls <setup-url> --submit
REM Requires komac (winget install komac) and a classic GitHub token with public_repo scope.
REM Token sources, in order: WINGET_GITHUB_TOKEN, GITHUB_TOKEN, komac stored token (komac token add).

set "VERSION=%~1"
set "MODE=%~2"
if "%VERSION%"=="" goto :usage
if "%VERSION:~0,1%"=="v" set "VERSION=%VERSION:~1%"
echo(%VERSION%| findstr /c:"-nightly." >nul && (echo Nightly pre-releases are not submitted to winget.& exit /b 2)
echo(%VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if not errorlevel 1 goto :version_ok
echo(%VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul || (echo Invalid version. Use MAJOR.MINOR.PATCH[.HOTFIX], e.g. 1.0.7.& exit /b 2)
:version_ok
if not "%MODE%"=="" if /i not "%MODE%"=="--new" (echo Unknown option: %MODE%& exit /b 2)

if not defined WINGET_PACKAGE_ID set "WINGET_PACKAGE_ID=lejy.Winchisel"
set "TAG=v%VERSION%"
set "URL=https://github.com/lejyfps/winchisel/releases/download/%TAG%/Winchisel-Setup-x64.exe"

if defined WINGET_GITHUB_TOKEN if not defined GITHUB_TOKEN set "GITHUB_TOKEN=%WINGET_GITHUB_TOKEN%"

where komac >nul 2>&1
if errorlevel 1 goto :no_komac

if /i "%MODE%"=="--new" (
  echo Creating NEW winget package %WINGET_PACKAGE_ID% %VERSION% - interactive...
  komac new --version "%VERSION%" --urls "%URL%" || exit /b 4
  echo Done. Follow the komac prompts and submit the PR to microsoft/winget-pkgs.
  exit /b 0
)

echo Updating winget package %WINGET_PACKAGE_ID% to %VERSION%...
echo URL: %URL%
komac update "%WINGET_PACKAGE_ID%" --version "%VERSION%" --urls "%URL%" --submit || exit /b 4
echo Winget PR submitted.
exit /b 0

:no_komac
echo komac was not found. Install it once with:
echo   winget install komac
echo Then store your token once (classic token, public_repo scope):
echo   komac token add --token=%GITHUB_TOKEN%
echo Or set WINGET_GITHUB_TOKEN and rerun this script.
echo.
echo Manual fallback: https://github.com/microsoft/winget-pkgs ^> New manifest for %WINGET_PACKAGE_ID% %VERSION%:
echo   %URL%
exit /b 3

:usage
echo Usage: tools\winget-submit.cmd MAJOR.MINOR.PATCH[.HOTFIX] [--new]
exit /b 2
