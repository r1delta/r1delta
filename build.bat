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
if "%clang%"=="1" set msvc=0 && echo [clang build]
if "%msvc%"=="1" set clang=0 && echo [msvc build]

set cl_common=     /DUNICODE=1 /D_UNICDE=1 /std:c++latest /I..\ /I..\sdk\ /I..\engine\ /I..\packages\minhook.1.3.3\lib\native\include\ /I..\thirdparty\nlohmann\ /I..\thirdparty\mimalloc\include\ /I..\thirdparty\capnp-sources\c++\src\ /I..\third_party\EOS-SDK\SDK\Include\ /I..\vsdk\ /I..\vsdk\public\ /I..\engine\core\ /I..\engine\memory\ /nologo /FC /Z7
set clang_common=  -DUNICODE=1 -DUNICODE=1 -std=c++20 -I..\ -I..\sdk\ -I..\engine\ -I..\packages\minhook.1.3.3\lib\native\include\ -I..\thirdparty\nlohmann\ -I..\thirdparty\mimalloc\include\ -I..\thirdparty\capnp-sources\c++\src\ -I..\third_party\EOS-SDK\SDK\Include\ -I..\vsdk\ -I..\vsdk\public\ -I..\engine\core\ -I..\engine\memory\ -gcodeview -fdiagnostics-absolute-paths -Wall -Wno-unknown-warning-option -Wno-missing-braces -Wno-unused-function -Wno-writable-strings -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf -ferror-limit=10000
set cl_debug=      call cl /Od /Ob1 /DBUILD_DEBUG=1 /DMI_DEBUG=4 /DMI_SECURE=0 /DKJ_DEBUG=1 %cl_common% %auto_compile_flags%
set cl_release=    call cl /O2 /DBUILD_DEBUG=0 /DKJ_NDEBUG=1 %cl_common% %auto_compile_flags%
set clang_debug=   call clang++ -g -O0 -DBUILD_DEBUG=1 -DMI_DEBUG=4 -DMI_SECURE=0 -DKJ_DEBUG=1 %clang_common% %auto_compile_flags%
set clang_release= call clang++ -g -O2 -DBUILD_DEBUG=0 -DKJ_NDEBUG=1 %clang_common% %auto_compile_flags%
set cl_link=       /link /MANIFEST:EMBED /INCREMENTAL:NO /pdbaltpath:%%%%_PDB%%%% /NATVIS:"%~dp0\base.natvis" /noexp Shell32.lib User32.lib Shlwapi.lib Ws2_32.lib
set clang_link=    -fuse-ld=lld -Xlinker /MANIFEST:EMBED -Xlinker /pdbaltpath:%%%%_PDB%%%% -Xlinker /NATVIS:"%~dp0\base.natvis" -lShell32.lib -lUser32.lib -lShlwapi.lib -lWs2_32.lib
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
if "%msvc%"=="1"      set force_cpp=/TP
if "%msvc%"=="1"      set lib_prefix=
if "%msvc%"=="1"      set out=%cl_out%
if "%clang%"=="1"     set compile_debug=%clang_debug%
if "%clang%"=="1"     set compile_release=%clang_release%
if "%clang%"=="1"     set compile_link=%clang_link%
if "%clang%"=="1"     set force_cpp=-x c++
if "%clang%"=="1"     set lib_prefix=-l
if "%clang%"=="1"     set out=%clang_out%
if "%debug%"=="1"     set compile=%compile_debug%
if "%release%"=="1"   set compile=%compile_release%

if not exist build mkdir build

set tier0_sources=..\tier0_amal.cc

set mimalloc_sources=..\thirdparty\mimalloc\mimalloc_amal.cc
set eos_sdk_lib=..\third_party\EOS-SDK\SDK\Lib\EOSSDK-Win64-Shipping.lib
set eos_sdk_bin=..\third_party\EOS-SDK\SDK\Bin\EOSSDK-Win64-Shipping.dll

set kj_sources=..\thirdparty\capnp-sources\c++\src\kj\array.c++ ..\thirdparty\capnp-sources\c++\src\kj\cidr.c++ ..\thirdparty\capnp-sources\c++\src\kj\kjlist.c++ ..\thirdparty\capnp-sources\c++\src\kj\common.c++ ..\thirdparty\capnp-sources\c++\src\kj\debug.c++ ..\thirdparty\capnp-sources\c++\src\kj\exception.c++ ..\thirdparty\capnp-sources\c++\src\kj\io.c++ ..\thirdparty\capnp-sources\c++\src\kj\memory.c++ ..\thirdparty\capnp-sources\c++\src\kj\mutex.c++ ..\thirdparty\capnp-sources\c++\src\kj\string.c++ ..\thirdparty\capnp-sources\c++\src\kj\source-location.c++ ..\thirdparty\capnp-sources\c++\src\kj\hash.c++ ..\thirdparty\capnp-sources\c++\src\kj\table.c++ ..\thirdparty\capnp-sources\c++\src\kj\thread.c++ ..\thirdparty\capnp-sources\c++\src\kj\main.c++ ..\thirdparty\capnp-sources\c++\src\kj\kjarena.c++ ..\thirdparty\capnp-sources\c++\src\kj\test-helpers.c++ ..\thirdparty\capnp-sources\c++\src\kj\units.c++ ..\thirdparty\capnp-sources\c++\src\kj\encoding.c++ ..\thirdparty\capnp-sources\c++\src\kj\refcount.c++ ..\thirdparty\capnp-sources\c++\src\kj\string-tree.c++ ..\thirdparty\capnp-sources\c++\src\kj\time.c++ ..\thirdparty\capnp-sources\c++\src\kj\filesystem.c++ ..\thirdparty\capnp-sources\c++\src\kj\filesystem-disk-unix.c++ ..\thirdparty\capnp-sources\c++\src\kj\filesystem-disk-win32.c++ ..\thirdparty\capnp-sources\c++\src\kj\parse\char.c++
set kj_objs=array.obj cidr.obj kjlist.obj common.obj debug.obj exception.obj io.obj memory.obj mutex.obj string.obj source-location.obj hash.obj table.obj thread.obj main.obj kjarena.obj test-helpers.obj units.obj encoding.obj refcount.obj string-tree.obj time.obj filesystem.obj filesystem-disk-unix.obj filesystem-disk-win32.obj char.obj
set capnp_sources=..\thirdparty\capnp-sources\c++\src\capnp\c++.capnp.c++ ..\thirdparty\capnp-sources\c++\src\capnp\blob.c++ ..\thirdparty\capnp-sources\c++\src\capnp\arena.c++ ..\thirdparty\capnp-sources\c++\src\capnp\layout.c++ ..\thirdparty\capnp-sources\c++\src\capnp\list.c++ ..\thirdparty\capnp-sources\c++\src\capnp\any.c++ ..\thirdparty\capnp-sources\c++\src\capnp\message.c++ ..\thirdparty\capnp-sources\c++\src\capnp\schema.capnp.c++ ..\thirdparty\capnp-sources\c++\src\capnp\stream.capnp.c++ ..\thirdparty\capnp-sources\c++\src\capnp\serialize.c++ ..\thirdparty\capnp-sources\c++\src\capnp\serialize-packed.c++ ..\thirdparty\capnp-sources\c++\src\capnp\schema.c++ ..\thirdparty\capnp-sources\c++\src\capnp\schema-loader.c++ ..\thirdparty\capnp-sources\c++\src\capnp\dynamic.c++ ..\thirdparty\capnp-sources\c++\src\capnp\stringify.c++ 
set capnp_objs=c++.capnp.obj blob.obj arena.obj layout.obj list.obj any.obj message.obj schema.capnp.obj stream.capnp.obj serialize.obj serialize-packed.obj schema.obj schema-loader.obj dynamic.obj stringify.obj

:: BUILD
set built=0
pushd build

:: %compile_debug% /c %force_cpp% %kj_sources% %capnp_sources% || exit /b 1
:: if "%msvc%"=="1" lib /nologo %kj_objs% %capnp_objs% /OUT:capnp_debug.lib || exit /b 1
:: %compile_release% /c %force_cpp% %kj_sources% %capnp_sources% || exit /b 1
:: if "%msvc%"=="1" lib /nologo %kj_objs% %capnp_objs% /OUT:capnp_release.lib || exit /b 1

if "%debug%"=="1" set capnp_lib=capnp_debug.lib
if "%release%"=="1" set capnp_lib=capnp_release.lib

if "%tier0%"=="1" ml64.exe /c /nologo /Zi /Fo"NetChanFixes.obj" /Fl"" /W3 /errorReport:prompt /Ta..\NetChanFixes.asm
:: TODO(mrsteyk): crucify wanderer and Allusive for adding capnp this way.
::                FIX FIX FIX.
if "%tier0%"=="1"    set built=1 && %compile% %tier0_sources% %compile_link%  NetChanFixes.obj ..\vstdlib.lib ..\tier0_orig.lib ..\packages\minhook.1.3.3\lib\native\lib\libMinHook-x64-v141-mt.lib %capnp_lib% %eos_sdk_lib% %link_dll% /DEF:..\test.def %out%tier0.dll || exit /b 1
if "%launcher%"=="1" set built=1 && %compile% ..\launch\main.cpp %compile_link% /STACK:0x7A1200 %out%r1delta.exe || exit /b 1

if "%tier0%"=="1"    for %%I in (tier0.dll tier0.pdb) do copy /Y %%I "C:\Program Files\EA Games\Titanfall\bin\x64_retail\"
if "%tier0%"=="1"    if exist %eos_sdk_bin% copy /Y %eos_sdk_bin% "C:\Program Files\EA Games\Titanfall\bin\x64_retail\"
if "%launcher%"=="1" for %%I in (r1delta.exe r1delta.pdb) do copy /Y %%I "C:\Program Files\EA Games\Titanfall\"
popd

if "%built%"=="0" echo WARNING: built nothing! && exit /b 1
