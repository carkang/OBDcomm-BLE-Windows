@echo off
setlocal
set "where=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%where%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vs=%%i"
if not defined vs (
    echo Visual Studio C++ Build Tools are required.
    exit /b 1
)
call "%vs%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
pushd "%~dp0"
if not exist "..\build\windows" mkdir "..\build\windows"
rc /nologo /fo "..\build\windows\app.res" app.rc || goto failed
cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Fo"..\build\windows\main.obj" main.cpp "..\build\windows\app.res" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"..\build\windows\OBDcomm-Windows-x64.exe" user32.lib gdi32.lib comdlg32.lib windowsapp.lib bcrypt.lib || goto failed
popd
exit /b 0
:failed
popd
exit /b 1


