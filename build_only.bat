@echo off
cd /d D:\Dev\Microsoft^ Visual^ Studio\18\Community\Common7\Tools
call VsDevCmd.bat -arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
cmake --build build --parallel 4
exit /b %errorlevel%
