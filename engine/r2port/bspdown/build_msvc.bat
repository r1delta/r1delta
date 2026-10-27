@echo off
rem Build bspdown with MSVC (Visual Studio 2022 or later, x64). Output: msvc\bspdown_msvc.exe
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist msvc mkdir msvc
cl /nologo /std:c++17 /O2 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:msvc\bspdown_msvc.exe /Fo:msvc\ main.cpp bspdown.cpp tricoll.cpp collision.cpp sprp.cpp meshclip.cpp tangents.cpp physics.cpp cubemaps.cpp shadow.cpp lighting.cpp
