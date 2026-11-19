@echo off
setlocal
cd /d "%~dp0"
set "variant=%~1"
if /i "%variant%"=="double-jump" set "asi=SuperJump.asi"
if /i "%variant%"=="keypress" set "asi=SuperJump_Keypress.asi"
if not defined asi exit /b 2
if /i "%~2"=="exact" goto exact
if not "%~2"=="" exit /b 2
call third-palm\BuildDependencies.cmd
if errorlevel 1 exit /b 1
set "dep=third-palm\build\dependencies"
set "shlib=%dep%\deps\safetyhook\safetyhook.lib"
set "zydis=%dep%\_deps\zydis-build\Zydis.lib"
set "zycore=%dep%\_deps\zydis-build\zycore\Zycore.lib"
goto compile

:exact
set "shlib=third-palm\deps\lib\safetyhook.lib"
set "zydis=third-palm\deps\lib\Zydis.lib"
set "zycore=third-palm\deps\lib\Zycore.lib"

:compile
if not exist "%shlib%" exit /b 2
if not exist "%zydis%" exit /b 2
if not exist "%zycore%" exit /b 2
if not exist "%variant%\build" mkdir "%variant%\build"
if not exist "%variant%\release" mkdir "%variant%\release"
rc /nologo /fo "%variant%\build\SuperJump-release.res" "%variant%\source\SuperJump.rc"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /O2 /EHsc /MD /DNOMINMAX /I"third-palm\deps\safetyhook\include" /W4 /WX /GL /LD /Fo:"%variant%\build\SuperJump-release.obj" "%variant%\source\SuperJump.cpp" "%variant%\build\SuperJump-release.res" /link /LTCG /Brepro /OUT:"%variant%\release\%asi%" /IMPLIB:"%variant%\build\SuperJump-release.lib" "%shlib%" "%zydis%" "%zycore%" user32.lib
if errorlevel 1 exit /b 1
node third-palm\tools\verify-tls-directory.js "%variant%\release\%asi%"
exit /b %errorlevel%
