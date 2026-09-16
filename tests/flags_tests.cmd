@echo off
setlocal
rem Requires Visual Studio / Build Tools with the C++ desktop workload and Windows SDK.
rem Run from any directory: tests\flags_tests.cmd
rem Optional first argument: alternate flags.cpp (for pre-fix regression checks).
set "FLAGS_SOURCE=%~dp0..\src\common\utils\flags.cpp"
if not "%~1"=="" set "FLAGS_SOURCE=%~f1"
where cl.exe >nul 2>nul
if not errorlevel 1 goto compile
set "FLAGS_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%FLAGS_VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%FLAGS_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FLAGS_VS=%%i"
if not defined FLAGS_VS exit /b 1
call "%FLAGS_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:compile
set "FLAGS_OUT=%TEMP%\s2x-flags-tests-%RANDOM%-%RANDOM%"
mkdir "%FLAGS_OUT%"
if errorlevel 1 exit /b 1
pushd "%FLAGS_OUT%"
rem Compile the actual parser and its utility dependencies, without the game.
cl /nologo /std:c++20 /EHsc /W4 /WX /Gy /O2 /I"%~dp0..\src\common\utils" /FI"%~dp0flags_test_prelude.hpp" "%~dp0flags_tests.cpp" "%FLAGS_SOURCE%" "%~dp0..\src\common\utils\string.cpp" "%~dp0..\src\common\utils\memory.cpp" "%~dp0..\src\common\utils\nt.cpp" /Fe:flags_tests.exe /link /OPT:REF shell32.lib user32.lib advapi32.lib
if errorlevel 1 goto failed
flags_tests.exe empty +set sv_hostname ""
if errorlevel 1 goto failed
flags_tests.exe dash +set sv_hostname "" -dedicated
if errorlevel 1 goto failed
flags_tests.exe next +set sv_hostname "" +set other "Mixed Case"
if errorlevel 1 goto failed
flags_tests.exe signed +set integer -1 +set decimal -0.25 +set text -DashValue
if errorlevel 1 goto failed
flags_tests.exe repeated +SET SV_HostName "First Name" +set sv_hostname SecondName +set sv_hostname ""
if errorlevel 1 goto failed
flags_tests.exe missing +set missing +set valid Value +set trailing
if errorlevel 1 goto failed
flags_tests.exe legacy -label "" MiXeD -label ignored +map MP_Test -number -1
if errorlevel 1 goto failed
echo All 7 flags scenarios passed. Build output: %FLAGS_OUT%
popd
exit /b 0
:failed
echo Flags tests failed. Build output: %FLAGS_OUT%
popd
exit /b 1
