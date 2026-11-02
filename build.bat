@echo off
rem Builds the loader (version.dll) with MSVC 2022, then runs the ini merge test.
setlocal
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (echo MSVC 2022 not found: %VCVARS% & exit /b 1)
call "%VCVARS%" >nul
cd /d "%~dp0"
if not exist out\obj mkdir out\obj
rc /nologo /fo out\obj\version.res src\version.rc || exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /LD /Fo:out\obj\ src\loader.cpp /Fe:out\version.dll /link /NOLOGO user32.lib out\obj\version.res || exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /Fo:out\obj\ /Fe:out\ini_test.exe tools\ini_test.cpp /link /NOLOGO user32.lib || exit /b 1
out\ini_test.exe || (echo ini merge test FAILED & exit /b 1)
echo built %~dp0out
