@echo off
setlocal
for %%I in ("%~dp0..") do set "CODE=%%~fI"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
 "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "%CODE%\Dll1.sln" /m:6 /p:Configuration=Release /p:Platform=x64 /p:CI=1 /p:VcpkgEnabled=false /p:ForceImportAfterCppProps=C:\Users\r3muxd\tmp\r1delta-release-20261002\.build\local-deps.props /p:OutDir=%CODE%\.build\release\ /verbosity:minimal /nologo
exit /b %errorlevel%
