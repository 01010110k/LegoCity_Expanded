@echo off
REM Build lcudrive.dll. vcvars first, then pushd here (vcvars moves the cwd).
REM If the game is running it holds lcudrive.dll open: the link then falls back to lcudrive_new.dll.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"
cl /nologo /EHa /O2 /W4 /std:c++17 /LD /Fe:lcudrive.dll lcudrive.cpp user32.lib
if errorlevel 1 goto :locked
echo BUILD OK - lcudrive.dll
popd
goto :eof
:locked
cl /nologo /EHa /O2 /W4 /std:c++17 /LD /Fe:lcudrive_new.dll lcudrive.cpp user32.lib
if errorlevel 1 ( echo BUILD FAILED & popd & exit /b 1 )
echo BUILD OK - lcudrive_new.dll (game running? close it, then rename over lcudrive.dll)
popd
