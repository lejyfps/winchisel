@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "VERSION=%~1"
set "MODE="
set "NIGHTLY="
if "%VERSION%"=="" goto :usage
if not "%~4"=="" goto :usage
for %%O in ("%~2" "%~3") do (
  if /i "%%~O"=="--nightly" (set "NIGHTLY=1") else if not "%%~O"=="" (
    if /i "%%~O"=="--unsigned" (set "MODE=--unsigned") else (echo Unknown option: %%~O& exit /b 2)
  )
)
if not defined NIGHTLY goto :have_version
REM With --nightly, VERSION is the 3-part base (e.g. 1.0.9). Resolve the full
REM t3code-style version BASE-nightly.YYYYMMDD.BUILD from today's date and
REM the next free per-day build number across local and remote tags.
echo(%VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul || (echo With --nightly, pass the 3-part base version, e.g. 1.0.9.& exit /b 2)
for /f %%D in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "NIGHTLY_DATE=%%D"
if not defined NIGHTLY_DATE (echo Could not determine today's date.& exit /b 2)
set "NIGHTLY_BASE=%VERSION%"
set "NIGHTLY_BUILD=0"
for /f %%T in ('git tag -l "v%NIGHTLY_BASE%-nightly.%NIGHTLY_DATE%.*" 2^>nul') do call :nightly_max "%%T"
for /f "tokens=2" %%T in ('git ls-remote --tags origin "refs/tags/v%NIGHTLY_BASE%-nightly.%NIGHTLY_DATE%.*" 2^>nul') do call :nightly_max "%%T"
set /a NIGHTLY_BUILD+=1
set "VERSION=%NIGHTLY_BASE%-nightly.%NIGHTLY_DATE%.%NIGHTLY_BUILD%"
echo Nightly version: %VERSION%
:have_version
echo(%VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if not errorlevel 1 goto :version_ok
echo(%VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if not errorlevel 1 goto :version_ok
REM Nightly form MAJOR.MINOR.PATCH-nightly.YYYYMMDD.BUILD (like t3code).
REM The full pattern exceeds findstr's regex length limit, so validate the
REM "-nightly." infix, the 3-part base and the date.build tail separately.
echo(%VERSION%| findstr /c:"-nightly." >nul || goto :invalid_version
for /f "delims=- tokens=1,*" %%A in ("%VERSION%") do set "NIGHTLY_BASE=%%A" & set "NIGHTLY_REST=%%B"
if not "%NIGHTLY_REST:~0,8%"=="nightly." goto :invalid_version
set "NIGHTLY_TAIL=%NIGHTLY_REST:~8%"
echo(%NIGHTLY_TAIL%| findstr /r "^[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]\.[0-9][0-9]*$" >nul || goto :invalid_version
echo(%NIGHTLY_BASE%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul || goto :invalid_version
goto :version_ok
:invalid_version
echo Invalid version. Use MAJOR.MINOR.PATCH[.HOTFIX] or MAJOR.MINOR.PATCH-nightly.YYYYMMDD.BUILD.& exit /b 2
:version_ok
REM Numeric file version for the installer metadata: nightlies carry the
REM base version (1.0.9-nightly... -> 1.0.9.0); stable versions pass through
REM unchanged (Inno pads a 3-part version with .0 itself).
set "NUMERICVERSION=%VERSION%"
echo(%VERSION%| findstr /c:"-nightly." >nul && (for /f "delims=- tokens=1" %%V in ("%VERSION%") do set "NUMERICVERSION=%%V.0")
if not "%MODE%"=="" if /i not "%MODE%"=="--unsigned" (echo Unknown option: %MODE%& exit /b 2)
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "OUTPUT=%ROOT%\release-output\%VERSION%"
set "STAGE=%OUTPUT%\stage"
set "KEY=%ROOT%\release-keys\winchisel-update-private.key"
if exist "%OUTPUT%" (echo Refusing to overwrite existing release output: %OUTPUT%& exit /b 3)
if /i not "%MODE%"=="--unsigned" if not exist "%KEY%" (echo Signing key not found: %KEY%& exit /b 3)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio Build Tools or Visual Studio is required.& exit /b 3)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (echo No Visual Studio installation with MSBuild was found.& exit /b 3)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul || exit /b 3
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (echo Inno Setup 6 is required to build the setup executable.& exit /b 3)
mkdir "%STAGE%" || exit /b 3
echo Building Winchisel %VERSION%...
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 "%ROOT%\tools\validate_catalogs.cpp" /Fo"%OUTPUT%\validate_catalogs.obj" /Fe"%OUTPUT%\validate_catalogs.exe" || exit /b 4
"%OUTPUT%\validate_catalogs.exe" "%ROOT%" "%OUTPUT%\catalog.golden" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /I"%ROOT%\src\core\include" "%ROOT%\tools\core_tests.cpp" "%ROOT%\src\core\src\settings.cpp" "%ROOT%\src\core\src\i18n.cpp" "%ROOT%\src\core\src\tweak.cpp" "%ROOT%\src\core\src\risk.cpp" "%ROOT%\src\core\src\revert.cpp" /Fo"%OUTPUT%\\" /Fe"%OUTPUT%\core_tests.exe" || exit /b 4
"%OUTPUT%\core_tests.exe" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 "%ROOT%\tools\release_smoke.cpp" /Fo"%OUTPUT%\release_smoke.obj" /Fe"%OUTPUT%\release_smoke.exe" || exit /b 4
"%OUTPUT%\release_smoke.exe" || exit /b 4
REM process_tests links process.cpp, which journals IFEO changes, so the revert
REM journal TUs come along. Platform and core revert.cpp share a basename, so
REM the platform TU gets its own object name (else one revert.obj overwrites
REM the other and the link fails with LNK2019).
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /c /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\process_tests.cpp" "%ROOT%\src\platform\src\process.cpp" "%ROOT%\src\platform\src\registry.cpp" "%ROOT%\src\platform\src\performance.cpp" "%ROOT%\src\platform\src\startup.cpp" "%ROOT%\src\platform\src\system.cpp" "%ROOT%\src\core\src\settings.cpp" "%ROOT%\src\core\src\i18n.cpp" "%ROOT%\src\core\src\tweak.cpp" "%ROOT%\src\core\src\risk.cpp" "%ROOT%\src\core\src\revert.cpp" /Fo"%OUTPUT%\\" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /c /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\src\platform\src\revert.cpp" /Fo"%OUTPUT%\platform_revert.obj" || exit /b 4
cl /nologo "%OUTPUT%\process_tests.obj" "%OUTPUT%\process.obj" "%OUTPUT%\registry.obj" "%OUTPUT%\performance.obj" "%OUTPUT%\startup.obj" "%OUTPUT%\system.obj" "%OUTPUT%\settings.obj" "%OUTPUT%\i18n.obj" "%OUTPUT%\tweak.obj" "%OUTPUT%\risk.obj" "%OUTPUT%\revert.obj" "%OUTPUT%\platform_revert.obj" /Fe"%OUTPUT%\process_tests.exe" || exit /b 4
"%OUTPUT%\process_tests.exe" || exit /b 5
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\update_tests.cpp" "%ROOT%\src\platform\src\update.cpp" /Fo"%OUTPUT%\\" /Fe"%OUTPUT%\update_tests.exe" || exit /b 4
"%OUTPUT%\update_tests.exe" || exit /b 5
if not exist "%ROOT%\Winchisel.sln" call "%ROOT%\tools\generate-solution.cmd" || exit /b 4
if defined WINCHISEL_PLATFORM_TOOLSET (
  msbuild "%ROOT%\Winchisel.sln" /m:1 /nologo /verbosity:minimal /clp:ErrorsOnly /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=%WINCHISEL_PLATFORM_TOOLSET% /t:Rebuild || exit /b 4
) else (
  msbuild "%ROOT%\Winchisel.sln" /m:1 /nologo /verbosity:minimal /clp:ErrorsOnly /p:Configuration=Release /p:Platform=x64 /t:Rebuild || exit /b 4
)
robocopy "%ROOT%\out\x64\Release" "%STAGE%" /E /R:2 /W:1 /XF *.pdb *.lib *.exp >nul
if errorlevel 8 (echo Staging runtime files failed.& exit /b 4)
REM RestartAgent.exe is auto-copied from the WindowsAppSDK Foundation NuGet
REM package. It is not our code, is never called by the app, and crashes
REM winget validation (STATUS_STACK_BUFFER_OVERRUN) when launched bare.
REM Exclude it so it is shipped in neither setup nor portable.
del "%STAGE%\RestartAgent.exe" 2>nul
if not exist "%STAGE%\Winchisel.exe" (echo Winchisel.exe was not produced.& exit /b 4)
> "%STAGE%\version.txt" echo %VERSION%
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 "%ROOT%\tools\release_bundle.cpp" /Fo"%OUTPUT%\bundle.obj" /Fe"%OUTPUT%\bundle.exe" || exit /b 5
rc /nologo /fo "%OUTPUT%\portable-icon.res" "%ROOT%\tools\Winchisel.Portable.rc" || exit /b 5
REM Portable stub is shipped (as the prefix of the portable EXE), so it must
REM also link the CRT statically (/MT) for clean machines without VCRedist.
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /MT "%ROOT%\tools\release_bootstrap.cpp" "%OUTPUT%\portable-icon.res" /Fo"%OUTPUT%\portable-stub.obj" /Fe"%OUTPUT%\portable-stub.exe" /link /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" || exit /b 5
echo Creating compressed portable executable...
"%OUTPUT%\bundle.exe" portable "%OUTPUT%\portable-stub.exe" "%STAGE%" "%OUTPUT%\Winchisel-Portable-x64.exe" || exit /b 5
echo Creating modern Inno Setup installer...
"%ISCC%" /Qp /DStagingDir="%STAGE%" /DOutputDir="%OUTPUT%" /DReleaseVersion="%VERSION%" /DVersionInfoVersion="%NUMERICVERSION%" "%ROOT%\installer\Winchisel.iss" || exit /b 5
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 "%ROOT%\tools\release_signer.cpp" /Fo"%OUTPUT%\release_signer.obj" /Fe"%OUTPUT%\release_signer.exe" || exit /b 6
"%OUTPUT%\release_signer.exe" manifest "%VERSION%" "%OUTPUT%\Winchisel-Setup-x64.exe" "%OUTPUT%\Winchisel-Portable-x64.exe" "%OUTPUT%\release.json" || exit /b 6
if /i "%MODE%"=="--unsigned" goto :done
"%OUTPUT%\release_signer.exe" sign "%KEY%" "%OUTPUT%\release.json" || exit /b 6
:done
del "%OUTPUT%\validate_catalogs.exe" & del "%OUTPUT%\validate_catalogs.obj"
del "%OUTPUT%\core_tests.exe" & del "%OUTPUT%\release_smoke.exe" & del "%OUTPUT%\process_tests.exe" & del "%OUTPUT%\update_tests.exe"
del "%OUTPUT%\*.obj"
del "%OUTPUT%\bundle.exe" & del "%OUTPUT%\bundle.obj" 2>nul & del "%OUTPUT%\portable-stub.exe" & del "%OUTPUT%\portable-stub.obj" 2>nul & del "%OUTPUT%\portable-icon.res" & del "%OUTPUT%\release_signer.exe" & del "%OUTPUT%\release_signer.obj"
echo Release artifacts are ready in: %OUTPUT%
exit /b 0
:usage
echo Usage: tools\release.cmd VERSION [--nightly] [--unsigned]
echo   VERSION is MAJOR.MINOR.PATCH[.HOTFIX], a full MAJOR.MINOR.PATCH-nightly.YYYYMMDD.BUILD,
echo   or the 3-part base with --nightly (build number resolved automatically).
exit /b 2

REM Tracks the highest per-day nightly build number for :resolve usage above.
REM Compares the trailing numeric component of local and remote tags like
REM v1.0.9-nightly.20260910.3 (annotated-tag peel refs and junk are skipped).
:nightly_max
set "NIGHTLY_TAG=%~1"
echo(%NIGHTLY_TAG%| findstr /c:"^{}" >nul && goto :eof
set "NIGHTLY_TAG=%NIGHTLY_TAG:refs/tags/=%"
for %%B in ("%NIGHTLY_TAG:.=" "%") do set "NIGHTLY_LAST=%%~B"
echo(%NIGHTLY_LAST%| findstr /r "^[0-9][0-9]*$" >nul || goto :eof
if %NIGHTLY_LAST% gtr %NIGHTLY_BUILD% set "NIGHTLY_BUILD=%NIGHTLY_LAST%"
goto :eof
