@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this from an x64 Native Tools Command Prompt for Visual Studio 2022.
  exit /b 1
)
pushd "%~dp0.."
if not exist build\tests mkdir build\tests
cl /nologo /std:c++20 /EHsc /W4 /WX tests\dedicated_settings_config_tests.cpp /Fo:build\tests\dedicated_settings_config_tests.obj /Fe:build\tests\dedicated_settings_config_tests.exe
if errorlevel 1 (popd & exit /b 1)
build\tests\dedicated_settings_config_tests.exe
set "test_result=%errorlevel%"
popd
exit /b %test_result%
