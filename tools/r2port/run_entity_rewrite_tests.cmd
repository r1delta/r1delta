@echo off
setlocal
for %%I in ("%~dp0..\..") do set "CODE=%%~fI"
set "DATA_ROOT_1=%~1"
if not defined DATA_ROOT_1 set "DATA_ROOT_1=%CODE%\..\r2port\entityport\ents"
set "DATA_ROOT_2=%~2"
if not defined R1_CORE_DIR set "R1_CORE_DIR=S:\game\r1delta"
if not defined R1_CORE_SHA set "R1_CORE_SHA=316993bd0142e706ea5386943530f2b8d2c711dd"

call "%CODE%\.build\build-entity-rewrite.cmd"
if errorlevel 1 exit /b %errorlevel%

"%CODE%\.build\entity_rewrite_tests.exe"
if errorlevel 1 exit /b %errorlevel%

git -C "%R1_CORE_DIR%" show "%R1_CORE_SHA%:maps/mp_box_script.ent" | "%CODE%\.build\entity_rewrite_cli.exe" --validate - --strict
if errorlevel 1 exit /b %errorlevel%
git -C "%R1_CORE_DIR%" show "%R1_CORE_SHA%:maps/mp_nest2_script.ent" | "%CODE%\.build\entity_rewrite_cli.exe" --validate - --strict
if errorlevel 1 exit /b %errorlevel%
git -C "%R1_CORE_DIR%" show "%R1_CORE_SHA%:maps/mp_mia_script.ent" | "%CODE%\.build\entity_rewrite_cli.exe" --validate - --strict
if errorlevel 1 exit /b %errorlevel%

if defined DATA_ROOT_2 (
  "%CODE%\.build\entity_rewrite_cli.exe" --batch --strict --report "%CODE%\.build\entity_rewrite_dataset.md" "%DATA_ROOT_1%" "%DATA_ROOT_2%"
) else (
  "%CODE%\.build\entity_rewrite_cli.exe" --batch --strict --report "%CODE%\.build\entity_rewrite_dataset.md" "%DATA_ROOT_1%"
)
if errorlevel 1 exit /b %errorlevel%
echo dataset report: "%CODE%\.build\entity_rewrite_dataset.md"
