@echo off
setlocal
for %%I in ("%~dp0..\..") do set "CODE=%%~fI"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "%CODE%\.build\r2audio_tests" mkdir "%CODE%\.build\r2audio_tests"
cl /nologo /O2 /EHsc /std:c++17 /W3 /I"%CODE%" "%CODE%\engine\r2port\audio\bink_audio.cpp" "%CODE%\engine\r2port\audio\r2_sound_bank.cpp" "%CODE%\engine\r2port\audio\r2_sound_scripts.cpp" "%CODE%\engine\r2port\audio\r2_audio_source.cpp" "%~dp0r2audio_tests.cpp" /Fe:"%CODE%\.build\r2audio_tests.exe" /Fo:"%CODE%\.build\r2audio_tests\\"
if errorlevel 1 exit /b %errorlevel%
"%CODE%\.build\r2audio_tests.exe"
