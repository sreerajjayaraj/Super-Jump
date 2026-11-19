@echo off
setlocal
cd /d "%~dp0"
set "variant=%~1"
if /i "%variant%"=="double-jump" goto test
if /i "%variant%"=="keypress" (
  set "testFlag=/DKEYPRESS_VARIANT"
  goto test
)
exit /b 2

:test
call third-palm\BuildDependencies.cmd
if errorlevel 1 exit /b 1
set "dep=third-palm\build\dependencies"
set "libs=%dep%\deps\safetyhook\safetyhook.lib %dep%\_deps\zydis-build\Zydis.lib %dep%\_deps\zydis-build\zycore\Zycore.lib user32.lib"
set "flags=/nologo /std:c++latest /O2 /EHsc /MD /DNOMINMAX /Ithird-palm\deps\safetyhook\include"
if not exist "%variant%\build" mkdir "%variant%\build"
ml64 /nologo /c /Fo"%variant%\build\MotionFixture.obj" third-palm\tests\MotionFixture.asm
if errorlevel 1 exit /b 1
cl %flags% %testFlag% /W3 shared\Menu60Integration.cpp /Fo:"%variant%\build\Menu60Integration.obj" /Fe:"%variant%\build\Menu60Integration.exe" "%variant%\build\MotionFixture.obj" /link %libs%
if errorlevel 1 exit /b 1
"%variant%\build\Menu60Integration.exe"
if errorlevel 1 exit /b 1
for %%T in (SuperJumpContextGuardTests SuperJumpBossGuardTests SuperJumpInputPolicyTests SuperJumpGameDoubleJumpPolicyTests SuperJumpAxiomGuardPolicyTests SuperJumpXInputProviderPolicyTests SuperJumpTraversalGuardTests) do (
  cl %flags% /W4 /WX "%variant%\tests\%%T.cpp" /Fo:"%variant%\build\%%T.obj" /Fe:"%variant%\build\%%T.exe"
  if errorlevel 1 exit /b 1
  "%variant%\build\%%T.exe"
  if errorlevel 1 exit /b 1
)
if /i "%variant%"=="double-jump" (
  cl %flags% /W3 double-jump\tests\SourceProcessGuardTests.cpp /Fo:double-jump\build\SourceProcessGuardTests.obj /Fe:double-jump\build\SourceProcessGuardTests.exe /link %libs%
  if errorlevel 1 exit /b 1
  double-jump\build\SourceProcessGuardTests.exe
  if errorlevel 1 exit /b 1
)
exit /b 0
