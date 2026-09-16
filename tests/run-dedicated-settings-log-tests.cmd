@echo off
setlocal
pushd "%~dp0.."
if not exist build\tests mkdir build\tests
cl /nologo /std:c++20 /EHsc /W4 /WX tests\dedicated_settings_log_tests.cpp /Fo:build\tests\dedicated_settings_log_tests.obj /Fe:build\tests\dedicated_settings_log_tests.exe
if errorlevel 1 (popd & exit /b 1)
build\tests\dedicated_settings_log_tests.exe
set "test_result=%errorlevel%"
popd
exit /b %test_result%
