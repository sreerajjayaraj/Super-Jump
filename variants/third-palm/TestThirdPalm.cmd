@echo off
setlocal
cd /d "%~dp0"
call BuildDependencies.cmd
if errorlevel 1 exit /b 1
if not exist "build\authored-recorder" mkdir "build\authored-recorder"
if not exist "build\authored-recorder" exit /b 2
cl /nologo /std:c++latest /O2 /EHsc /MD /W4 /WX /DNOMINMAX tests\PalmPolicyTests.cpp /Fo:build\PalmPolicyTests.obj /Fe:build\PalmPolicyTests.exe
if errorlevel 1 exit /b 1
build\PalmPolicyTests.exe
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\MotionFixture.obj tests\MotionFixture.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /O2 /EHsc /MD /W3 /DNOMINMAX /I"deps\safetyhook\include" tests\PalmIntegrationTests.cpp /Fo:build\PalmIntegrationTests.obj /Fe:build\PalmIntegrationTests.exe build\MotionFixture.obj /link user32.lib "build\dependencies\deps\safetyhook\safetyhook.lib" "build\dependencies\_deps\zydis-build\Zydis.lib" "build\dependencies\_deps\zydis-build\zycore\Zycore.lib"
if errorlevel 1 exit /b 1
build\PalmIntegrationTests.exe
exit /b %errorlevel%
