@echo off
REM Builds dinput8.dll (the lcudrive auto-loader). vcvars first, then pushd here - vcvars moves the cwd.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"
cl /nologo /LD /O2 /W4 /GS- dinput8_proxy.cpp /link /DEF:dinput8.def /OUT:dinput8.dll user32.lib kernel32.lib
if errorlevel 1 ( echo BUILD FAILED & popd & exit /b 1 )
echo BUILD OK - dinput8.dll
popd
