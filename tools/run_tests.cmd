@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (echo Visual Studio not found.& exit /b 3)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul || exit /b 3
mkdir "%ROOT%\out\tests" 2>nul
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /I"%ROOT%\src\core\include" "%ROOT%\tools\core_tests.cpp" "%ROOT%\src\core\src\settings.cpp" "%ROOT%\src\core\src\i18n.cpp" "%ROOT%\src\core\src\tweak.cpp" "%ROOT%\src\core\src\risk.cpp" "%ROOT%\src\core\src\revert.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\core_tests.exe" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 "%ROOT%\tools\validate_catalogs.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\validate_catalogs.exe" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 "%ROOT%\tools\release_smoke.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\release_smoke.exe" || exit /b 4
"%ROOT%\out\tests\core_tests.exe" || exit /b 5
"%ROOT%\out\tests\validate_catalogs.exe" "%ROOT%" "%ROOT%\out\tests\catalog.golden" || exit /b 5
"%ROOT%\out\tests\release_smoke.exe" || exit /b 5
REM process_tests links process.cpp, which journals IFEO changes, so the revert
REM journal TUs come along. Platform and core revert.cpp share a basename, so
REM the platform TU gets its own object name (else one revert.obj overwrites
REM the other and the link fails with LNK2019).
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /c /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\process_tests.cpp" "%ROOT%\src\platform\src\process.cpp" "%ROOT%\src\platform\src\registry.cpp" "%ROOT%\src\platform\src\performance.cpp" "%ROOT%\src\platform\src\startup.cpp" "%ROOT%\src\platform\src\system.cpp" "%ROOT%\src\core\src\settings.cpp" "%ROOT%\src\core\src\i18n.cpp" "%ROOT%\src\core\src\tweak.cpp" "%ROOT%\src\core\src\risk.cpp" "%ROOT%\src\core\src\revert.cpp" /Fo"%ROOT%\out\tests\\" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /c /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\src\platform\src\revert.cpp" /Fo"%ROOT%\out\tests\platform_revert.obj" || exit /b 4
cl /nologo "%ROOT%\out\tests\process_tests.obj" "%ROOT%\out\tests\process.obj" "%ROOT%\out\tests\registry.obj" "%ROOT%\out\tests\performance.obj" "%ROOT%\out\tests\startup.obj" "%ROOT%\out\tests\system.obj" "%ROOT%\out\tests\settings.obj" "%ROOT%\out\tests\i18n.obj" "%ROOT%\out\tests\tweak.obj" "%ROOT%\out\tests\risk.obj" "%ROOT%\out\tests\revert.obj" "%ROOT%\out\tests\platform_revert.obj" /Fe"%ROOT%\out\tests\process_tests.exe" || exit /b 4
"%ROOT%\out\tests\process_tests.exe" || exit /b 5
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\update_tests.cpp" "%ROOT%\src\platform\src\update.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\update_tests.exe" || exit /b 4
"%ROOT%\out\tests\update_tests.exe" || exit /b 5
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\cadence_tests.cpp" "%ROOT%\src\platform\src\usb_cadence.cpp" "%ROOT%\src\platform\src\usb_identity.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\cadence_tests.exe" || exit /b 4
"%ROOT%\out\tests\cadence_tests.exe" || exit /b 5
echo All tests passed.
exit /b 0
