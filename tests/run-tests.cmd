@echo off
setlocal
where cl.exe >nul 2>nul
if not errorlevel 1 goto run
set "S2X_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%S2X_TEST_VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%S2X_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "S2X_TEST_VS=%%i"
if not defined S2X_TEST_VS exit /b 1
call "%S2X_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:run
call "%~dp0run-dedicated-settings-config-tests.cmd"
if errorlevel 1 exit /b 1
call "%~dp0run-dedicated-settings-value-tests.cmd"
if errorlevel 1 exit /b 1
call "%~dp0run-dedicated-settings-copy-tests.cmd"
if errorlevel 1 exit /b 1
call "%~dp0dedicated_settings_command_tests.cmd"
if errorlevel 1 exit /b 1
call "%~dp0run-dedicated-settings-log-tests.cmd"
if errorlevel 1 exit /b 1
call "%~dp0dedicated_settings_toggle_tests.cmd"
if errorlevel 1 exit /b 1
call "%~dp0flags_tests.cmd"
exit /b %errorlevel%
