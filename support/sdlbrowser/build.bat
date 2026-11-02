@echo off
cd D:\Code\github\litehtml\support\sdlbrowser
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32
cmake --build build --config Release
.\build\Release\sdlbrowser.exe


