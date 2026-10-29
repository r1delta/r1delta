@echo off
setlocal
for %%I in ("%~dp0..\..") do set "CODE=%%~fI"
set "OUT=%TEMP%\r2port-material-names-tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

rem The r2w/ and r2m/ namespace helpers of the material provider.
cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /I"%CODE%" /Fe:"%OUT%\material_names_tests.exe" /Fo"%OUT%\\" "%~dp0material_names_tests.cpp"
if errorlevel 1 exit /b %errorlevel%
"%OUT%\material_names_tests.exe"
if errorlevel 1 exit /b %errorlevel%

rem Rule 25 of the bspdown converter (world material namespace).
set "B=%CODE%\engine\r2port\bspdown"
cl /nologo /std:c++17 /O2 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:"%OUT%\check_material_namespace.exe" /Fo"%OUT%\\" "%B%\check_material_namespace.cpp" "%B%\bspdown.cpp" "%B%\tricoll.cpp" "%B%\collision.cpp" "%B%\sprp.cpp" "%B%\meshclip.cpp" "%B%\tangents.cpp" "%B%\vcalpha.cpp" "%B%\physics.cpp" "%B%\cubemaps.cpp" "%B%\shadow.cpp" "%B%\lighting.cpp"
if errorlevel 1 exit /b %errorlevel%
"%OUT%\check_material_namespace.exe"
if errorlevel 1 exit /b %errorlevel%
