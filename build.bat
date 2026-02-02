@echo off
echo ========================================
echo   MusicWidget Build
echo ========================================

taskkill /f /im MusicWidget.exe 2>nul

call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul

if not exist bin mkdir bin
if not exist obj mkdir obj

echo Compiling...
cl /nologo /W3 /O2 /EHsc /std:c++17 /c /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\cppwinrt" /Fo:obj\media_info.obj src\media_info.cpp
cl /nologo /W3 /O2 /EHsc /c /Fo:obj\gif_player.obj src\gif_player.cpp
cl /nologo /W3 /O2 /c /I src /Fo:obj\settings.obj src\settings.c
cl /nologo /W3 /O2 /c /I src /Fo:obj\main.obj src\main.c

echo Compiling resources...
rc /nologo /fo obj\resource.res src\resource.rc 2>nul
if exist obj\resource.res (
    echo Linking with icon...
    link /nologo /OUT:bin\MusicWidget.exe obj\main.obj obj\media_info.obj obj\gif_player.obj obj\settings.obj obj\resource.res user32.lib gdi32.lib ole32.lib gdiplus.lib shell32.lib windowsapp.lib runtimeobject.lib advapi32.lib dwmapi.lib /SUBSYSTEM:WINDOWS
) else (
    echo Linking without icon...
    link /nologo /OUT:bin\MusicWidget.exe obj\main.obj obj\media_info.obj obj\gif_player.obj obj\settings.obj user32.lib gdi32.lib ole32.lib gdiplus.lib shell32.lib windowsapp.lib runtimeobject.lib advapi32.lib dwmapi.lib /SUBSYSTEM:WINDOWS
)

if %errorlevel%==0 (
    echo Copying assets...
    xcopy /E /Y /I assets bin\assets >nul 2>&1
    echo Build Success! Run: bin\MusicWidget.exe
) else (
    echo Build Failed!
)
pause
