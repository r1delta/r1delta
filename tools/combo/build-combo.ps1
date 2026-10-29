$ErrorActionPreference = 'Continue'
Set-Location C:\Users\r3muxd\tmp\r1delta-combined
$log = '.build\combo-build.log'
cmd /c ".build\build-local.cmd" *> $log
"exit $LASTEXITCODE" | Tee-Object -Append $log
