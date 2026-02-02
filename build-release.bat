@echo off
echo ========================================
echo   MusicWidget Release Build
echo ========================================

taskkill /f /im MusicWidget.exe 2>nul

call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul

if not exist bin mkdir bin
if not exist obj mkdir obj
if not exist releases mkdir releases

echo Compiling (Release with static runtime)...
cl /nologo /W3 /O2 /EHsc /std:c++17 /MT /c /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\cppwinrt" /Fo:obj\media_info.obj src\media_info.cpp
cl /nologo /W3 /O2 /EHsc /MT /c /Fo:obj\gif_player.obj src\gif_player.cpp
cl /nologo /W3 /O2 /MT /c /I src /Fo:obj\settings.obj src\settings.c
cl /nologo /W3 /O2 /MT /c /I src /Fo:obj\main.obj src\main.c

echo Compiling resources...
rc /nologo /fo obj\resource.res src\resource.rc

if exist obj\resource.res (
    echo Linking with icon...
    link /nologo /OUT:bin\MusicWidget.exe obj\main.obj obj\media_info.obj obj\gif_player.obj obj\settings.obj obj\resource.res user32.lib gdi32.lib ole32.lib gdiplus.lib shell32.lib windowsapp.lib runtimeobject.lib advapi32.lib dwmapi.lib /SUBSYSTEM:WINDOWS
) else (
    echo Linking without icon...
    link /nologo /OUT:bin\MusicWidget.exe obj\main.obj obj\media_info.obj obj\gif_player.obj obj\settings.obj user32.lib gdi32.lib ole32.lib gdiplus.lib shell32.lib windowsapp.lib runtimeobject.lib advapi32.lib dwmapi.lib /SUBSYSTEM:WINDOWS
)

if %errorlevel% neq 0 (
    echo Build Failed!
    pause
    exit /b 1
)

echo.
echo Creating release package...

:: Get version from date or set manually
set VERSION=1.0.0

:: Create release folder
set RELEASE_DIR=releases\MusicWidget-%VERSION%
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%\assets"

:: Copy files
copy bin\MusicWidget.exe "%RELEASE_DIR%\"
copy assets\config.txt "%RELEASE_DIR%\assets\" 2>nul
copy assets\app.ico "%RELEASE_DIR%\assets\" 2>nul
copy README.md "%RELEASE_DIR%\"
copy LICENSE "%RELEASE_DIR%\"

:: Copy sample GIFs if exist
for %%f in (assets\*.gif) do copy "%%f" "%RELEASE_DIR%\assets\" 2>nul

:: Create ZIP (Update existing)
echo Updating ZIP archive...
powershell -Command "Compress-Archive -Path '%RELEASE_DIR%\*' -DestinationPath 'MusicWidget_Beta.zip' -Update"

echo.
echo ========================================
echo   Release package created:
echo   MusicWidget_Beta.zip
echo ========================================
pause
