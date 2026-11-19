@echo off
setlocal
cd /d "%~dp0"
cmake -S . -B build\dependencies -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if errorlevel 1 exit /b 1
cmake --build build\dependencies --target safetyhook
exit /b %errorlevel%
