@echo off
setlocal
for %%I in ("%~dp0..\..") do set "CODE=%%~fI"
set "OUT=%TEMP%\r2port-pcfconv-tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

rem The R2 -> R1 particle converter (engine\r2port\particles) on small synthetic PCF files.
set "P=%CODE%\engine\r2port\particles"
cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /I"%P%" /Fe:"%OUT%\pcfconv_tests.exe" /Fo"%OUT%\\" "%~dp0pcfconv_tests.cpp" "%P%\dmx.cpp" "%P%\pcfconv.cpp"
if errorlevel 1 exit /b %errorlevel%
"%OUT%\pcfconv_tests.exe"
if errorlevel 1 exit /b %errorlevel%
