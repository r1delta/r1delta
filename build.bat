@echo off
setlocal enabledelayedexpansion
cd /D "%~dp0"

:: Stolen from gh:EpicGamesExt/raddebugger (MIT License)

for %%a in (%*) do set "%%a=1"

set auto_compile_flags=
:: TODO(mrsteyk): profiling...
if "%asan%"=="1" set auto_compile_flags=%auto_compile_flags% -fsanitize=address && echo [asan enabled]

:: Meta Game Build Type
:: if not "%shipping%"=="1" set internal=1
:: if "%internal%"=="1" set gametype=-DBUILD_INTERNAL=1 && echo [internal build meta]
:: if "%shipping%"=="1" set gametype=-DBUILD_INTERNAL=0 && echo [shipping build meta]

if not "%release%"=="1" set debug=1
if "%debug%"=="1" set release=0 && echo [debug build]
if "%release%"=="1" set debug=0 && echo [release build] 

if not "%msvc%"=="1" if not "%clang%"=="1" set msvc=1
if "%clang%"=="%1%" set msvc=0 && echo [clang build]
if "%msvc%"=="%1%" set clang=0 && echo [msvc build]

set cl_common=     /DUNICODE=1 /D_UNICDE=1 /std:c++latest /I..\ /I..\packages\minhook.1.3.3\lib\native\include\ /I..\thirdparty\nlohmann\ /I..\thirdparty\mimalloc\include\ /I..\thirdparty\capnp\include /I..\vsdk\ /I..\vsdk\public\ /nologo /FC /Z7
set clang_common=  -DUNICODE=1 -DUNICODE=1 -std=c++20 -I..\ -I..\packages\minhook.1.3.3\lib\native\include\ -I..\thirdparty\nlohmann\ -I..\thirdparty\mimalloc\include\ -I..\vsdk\ -I..\vsdk\public\ -gcodeview -fdiagnostics-absolute-paths -Wall -Wno-unknown-warning-option -Wno-missing-braces -Wno-unused-function -Wno-writable-strings -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf -ferror-limit=10000
set cl_debug=      call cl /Od /Ob1 /DBUILD_DEBUG=1 /DMI_DEBUG=4 /DMI_SECURE=0 %cl_common% %auto_compile_flags%
set cl_release=    call cl /O2 /DBUILD_DEBUG=0 %cl_common% %auto_compile_flags%
set clang_debug=   call clang++ -g -O0 -DBUILD_DEBUG=1 -DMI_DEBUG=4 -DMI_SECURE=0 %clang_common% %auto_compile_flags%
set clang_release= call clang++ -g -O2 -DBUILD_DEBUG=0 %clang_common% %auto_compile_flags%
set cl_link=       /link /MANIFEST:EMBED /INCREMENTAL:NO /pdbaltpath:%%%%_PDB%%%% /NATVIS:"%~dp0\base.natvis" /noexp
set clang_link=    -fuse-ld=lld -Xlinker /MANIFEST:EMBED -Xlinker /pdbaltpath:%%%%_PDB%%%% -Xlinker /NATVIS:"%~dp0\base.natvis"
set cl_out=        /out:
set clang_out=     -o
set cl_natvis=     /NATVIS:
set clang_natvis=  -Xlinker /NATVIS:

set link_dll=-DLL
set link_icon=logo.res
if "%msvc%"=="1"    set link_natvis=%cl_natvis%
if "%clang%"=="1"   set link_natvis=%clang_natvis%
if "%msvc%"=="1"    set only_compile=/c
if "%clang%"=="1"   set only_compile=-c
if "%msvc%"=="1"    set EHsc=/EHsc
if "%clang%"=="1"   set EHsc=
if "%msvc%"=="1"    set no_aslr=/DYNAMICBASE:NO
if "%clang%"=="1"   set no_aslr=-Wl,/DYNAMICBASE:NO
if "%msvc%"=="1"    set rc=call rc
if "%clang%"=="1"   set rc=call llvm-rc

if "%msvc%"=="1"      set compile_debug=%cl_debug%
if "%msvc%"=="1"      set compile_release=%cl_release%
if "%msvc%"=="1"      set compile_link=%cl_link%
if "%msvc%"=="1"      set out=%cl_out%
if "%clang%"=="1"     set compile_debug=%clang_debug%
if "%clang%"=="1"     set compile_release=%clang_release%
if "%clang%"=="1"     set compile_link=%clang_link%
if "%clang%"=="1"     set out=%clang_out%
if "%debug%"=="1"     set compile=%compile_debug%
if "%release%"=="1"   set compile=%compile_release%

if not exist build mkdir build

:: TODO(mrsteyk): amalgamation doesn't play nice because files are not namespaced at all.
set tier0_sources=..\bitbuf.cpp ..\client.cpp ..\compression.cpp ..\cvar.cpp ..\dedicated.cpp ..\dllmain.cpp ..\engine.cpp ..\factory.cpp ..\filecache.cpp ..\filesystem.cpp ..\keyvalues.cpp ..\load.cpp ..\logging.cpp ..\masterserver.cpp ..\memory.cpp ..\model_info.cpp ..\navmesh.cpp ..\netchanwarnings.cpp ..\newbitbuf.cpp ..\patcher.cpp ..\persistentdata.cpp ..\predictionerror.cpp ..\sendmoveclampfix.cpp ..\squirrel.cpp ..\TableDestroyer.cpp ..\thirdparty\zstd\zstd.c ..\utils.cpp ..\vsdk\tier0\dbg.cpp ..\vsdk\tier0\platformtime.cpp ..\vsdk\tier0\valve_tracelogging.cpp ..\vsdk\tier1\ipv6text.c ..\vsdk\tier1\netadr.cpp ..\vsdk\tier1\utlbuffer.cpp ..\vsdk\vstdlib\strtools.cpp

set mimalloc_sources=..\mimalloc_amal.cc

:: BUILD
set built=0
pushd build
ml64.exe /c /nologo /Zi /Fo"NetChanFixes.obj" /Fl"" /W3 /errorReport:prompt /Ta..\NetChanFixes.asm
:: TODO(mrsteyk): crucify wanderer and Allusive for adding capnp this way.
::                FIX FIX FIX.
if "%tier0%"=="1"    set built=1 && %compile% %tier0_sources% %mimalloc_sources% %compile_link% NetChanFixes.obj Advapi32.lib Shell32.lib User32.lib Ole32.lib ..\vstdlib.lib ..\tier0_orig.lib ..\packages\minhook.1.3.3\lib\native\lib\libMinHook-x64-v141-mt.lib ..\thirdparty\capnp\lib\Release\capnp.lib ..\thirdparty\capnp\lib\Release\kj.lib %link_dll% /DEF:..\test.def %out%tier0.dll || exit /b 1
:: if "%launcher%"=="1" set built=1 && %compile% ..\launcher\launcher_main.cc %compile_link% %out%r1delta.exe || exit /b 1
popd

if "%built%"=="0" echo WARNING: built nothing! && exit /b 1
