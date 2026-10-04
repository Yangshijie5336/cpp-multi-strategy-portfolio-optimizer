@echo off
call "D:\Dev\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=D:/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DENABLE_XLS=ON -DVCPKG_MANIFEST_MODE=OFF
if errorlevel 1 exit /b %errorlevel%
cmake --build build --parallel 4
exit /b %errorlevel%
