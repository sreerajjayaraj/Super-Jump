@echo off
setlocal
cd /d "%~dp0"
if /i "%~1"=="exact" goto exact
if not "%~1"=="" exit /b 2
call BuildDependencies.cmd
if errorlevel 1 exit /b 1
set "safetyhook=dependencies\deps\safetyhook\safetyhook.lib"
set "zydis=dependencies\_deps\zydis-build\Zydis.lib"
set "zycore=dependencies\_deps\zydis-build\zycore\Zycore.lib"
goto compile

:exact
set "safetyhook=..\deps\lib\safetyhook.lib"
set "zydis=..\deps\lib\Zydis.lib"
set "zycore=..\deps\lib\Zycore.lib"

:compile
if not exist "build" mkdir "build"
if not exist "release" mkdir "release"
cd /d "%~dp0build"
if not exist "%safetyhook%" exit /b 2
if not exist "%zydis%" exit /b 2
if not exist "%zycore%" exit /b 2
rc /nologo /fo ThirdPalm.res ..\source\SuperJump.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /O2 /GL /EHsc /MD /W4 /WX /DNOMINMAX /I"..\deps\safetyhook\include" /LD ..\source\SuperJump.cpp ThirdPalm.res /link /LTCG /Brepro /OUT:..\release\SuperJump_ThirdPalm.asi /IMPLIB:ThirdPalm.lib "%safetyhook%" "%zydis%" "%zycore%" user32.lib
if errorlevel 1 exit /b 1
node ..\tools\verify-tls-directory.js ..\release\SuperJump_ThirdPalm.asi
exit /b %errorlevel%
