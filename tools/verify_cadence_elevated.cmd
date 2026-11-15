@echo off
REM Winchisel cadence live verification - MUST run elevated (Admin).
REM Checks provider preflight, live UCX/HUB3 traffic + loss gate, and
REM records an ETL fixture for parser tests.
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
net session >nul 2>&1
if errorlevel 1 (
  echo NOT ELEVATED: right-click ^> Run as administrator, then retry.
  exit /b 3
)
if not exist "%ROOT%\out\tests\etw_spike.exe" (
  echo etw_spike.exe missing - build it first, then retry.
  exit /b 4
)
"%ROOT%\out\tests\etw_spike.exe" --seconds 4
if errorlevel 1 (
  echo LIVE GATE FAILED - move USB devices during capture and retry.
  exit /b 1
)
echo.
echo Live gate passed. Recording 10 s ETL fixture...
"%ROOT%\out\tests\etw_spike.exe" --seconds 10 --save-etl="%ROOT%\out\tests\cadence_fixture.etl"
if errorlevel 1 (
  echo FIXTURE RECORDING FAILED.
  exit /b 1
)
echo.
echo ALL PASS - fixture: out\tests\cadence_fixture.etl
exit /b 0
