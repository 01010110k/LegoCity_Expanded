@echo off
REM Builds lcutool.exe (release builder) and Setup.exe (the player's installer). vcvars first, then pushd here.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"
if not exist out mkdir out
cl /nologo /EHa /O2 /W4 /std:c++17 /utf-8 /Foout\ /Fe:out\lcutool.exe lcutool.cpp /link /BASE:0x10000000000 /DYNAMICBASE
if errorlevel 1 ( echo BUILD FAILED lcutool & popd & exit /b 1 )
if exist setup.cpp (
  rc /nologo /fo out\setup.res setup.rc
  if errorlevel 1 ( echo BUILD FAILED rc & popd & exit /b 1 )
  cl /nologo /EHa /O2 /W4 /std:c++17 /utf-8 /DUNICODE /D_UNICODE /Foout\ /Fe:out\Setup.exe setup.cpp out\setup.res /link /SUBSYSTEM:WINDOWS /BASE:0x10000000000 /DYNAMICBASE /MANIFEST:EMBED /MANIFESTINPUT:setup.manifest user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib shlwapi.lib
  if errorlevel 1 ( echo BUILD FAILED setup & popd & exit /b 1 )
)
echo BUILD OK
popd
