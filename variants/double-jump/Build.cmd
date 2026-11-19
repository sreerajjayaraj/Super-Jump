@echo off
call "%~dp0../BuildClassic.cmd" double-jump %*
exit /b %errorlevel%
