@echo off
REM AddressSanitizer variant of run_tests.cmd. Instruments the native test
REM binaries (settings parser, catalog validators, unpack helpers, process
REM helpers) and runs the same suites. Needs a Visual Studio environment
REM with the ASan runtime (used by the sanitizer CI job).
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (echo Visual Studio not found.& exit /b 3)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul || exit /b 3
mkdir "%ROOT%\out\tests" 2>nul
set "ASAN=/fsanitize=address"
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 %ASAN% /Zi /Fd"%ROOT%\out\tests\asan_vc.pdb" /I"%ROOT%\src\core\include" "%ROOT%\tools\core_tests.cpp" "%ROOT%\src\core\src\settings.cpp" "%ROOT%\src\core\src\i18n.cpp" "%ROOT%\src\core\src\tweak.cpp" "%ROOT%\src\core\src\risk.cpp" "%ROOT%\src\core\src\revert.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\core_tests_asan.exe" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 %ASAN% /Zi /Fd"%ROOT%\out\tests\asan_vc.pdb" "%ROOT%\tools\validate_catalogs.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\validate_catalogs_asan.exe" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 %ASAN% /Zi /Fd"%ROOT%\out\tests\asan_vc.pdb" "%ROOT%\tools\release_smoke.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\release_smoke_asan.exe" || exit /b 4
"%ROOT%\out\tests\core_tests_asan.exe" || exit /b 5
"%ROOT%\out\tests\validate_catalogs_asan.exe" "%ROOT%" "%ROOT%\out\tests\catalog_asan.golden" || exit /b 5
"%ROOT%\out\tests\release_smoke_asan.exe" || exit /b 5
REM Same split build as run_tests.cmd: platform and core revert.cpp share a
REM basename, so the platform TU gets its own object name (else LNK2019).
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 %ASAN% /Zi /Fd"%ROOT%\out\tests\asan_vc.pdb" /c /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\process_tests.cpp" "%ROOT%\src\platform\src\process.cpp" "%ROOT%\src\platform\src\registry.cpp" "%ROOT%\src\platform\src\performance.cpp" "%ROOT%\src\platform\src\startup.cpp" "%ROOT%\src\platform\src\system.cpp" "%ROOT%\src\core\src\settings.cpp" "%ROOT%\src\core\src\i18n.cpp" "%ROOT%\src\core\src\tweak.cpp" "%ROOT%\src\core\src\risk.cpp" "%ROOT%\src\core\src\revert.cpp" /Fo"%ROOT%\out\tests\\" || exit /b 4
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 %ASAN% /Zi /Fd"%ROOT%\out\tests\asan_vc.pdb" /c /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\src\platform\src\revert.cpp" /Fo"%ROOT%\out\tests\platform_revert_asan.obj" || exit /b 4
cl /nologo %ASAN% "%ROOT%\out\tests\process_tests.obj" "%ROOT%\out\tests\process.obj" "%ROOT%\out\tests\registry.obj" "%ROOT%\out\tests\performance.obj" "%ROOT%\out\tests\startup.obj" "%ROOT%\out\tests\system.obj" "%ROOT%\out\tests\settings.obj" "%ROOT%\out\tests\i18n.obj" "%ROOT%\out\tests\tweak.obj" "%ROOT%\out\tests\risk.obj" "%ROOT%\out\tests\revert.obj" "%ROOT%\out\tests\platform_revert_asan.obj" /Fe"%ROOT%\out\tests\process_tests_asan.exe" || exit /b 4
"%ROOT%\out\tests\process_tests_asan.exe" || exit /b 5
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 %ASAN% /Zi /Fd"%ROOT%\out\tests\asan_vc.pdb" /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\update_tests.cpp" "%ROOT%\src\platform\src\update.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\update_tests_asan.exe" || exit /b 4
"%ROOT%\out\tests\update_tests_asan.exe" || exit /b 5
cl /nologo /std:c++latest /EHsc /W4 /WX /utf-8 %ASAN% /Zi /Fd"%ROOT%\out\tests\asan_vc.pdb" /I"%ROOT%\src\core\include" /I"%ROOT%\src\platform\include" "%ROOT%\tools\cadence_tests.cpp" "%ROOT%\src\platform\src\usb_cadence.cpp" "%ROOT%\src\platform\src\usb_identity.cpp" /Fo"%ROOT%\out\tests\\" /Fe"%ROOT%\out\tests\cadence_tests_asan.exe" || exit /b 4
"%ROOT%\out\tests\cadence_tests_asan.exe" || exit /b 5
echo All ASan tests passed.
exit /b 0
