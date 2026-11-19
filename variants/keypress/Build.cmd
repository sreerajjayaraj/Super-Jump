@echo off
call "%~dp0../BuildClassic.cmd" keypress %*
exit /b %errorlevel%
