@echo off
rem Build preview.exe against a local UEVR clone (ImGui, its config and nlohmann/json).
rem   dev\imgui-preview\build.cmd <UEVR clone> [output folder]
setlocal
if "%~1"=="" (echo usage: build.cmd ^<UEVR clone^> [output folder] & exit /b 2)
set UEVR=%~f1
set REPO=%~dp0..\..
set OUT=%~2
if "%OUT%"=="" set OUT=%TEMP%\wuwa-imgui-preview
if not exist "%OUT%" mkdir "%OUT%"
set JSON=%UEVR%\out\build\x64-RelWithDebInfo\_deps\json-src\include
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set IMGUI=%UEVR%\dependencies\submodules\imgui
cl /nologo /std:c++20 /EHsc /O2 /MD /utf-8 /W3 /DUNICODE /D_UNICODE ^
  /DIMGUI_USER_CONFIG=\"%UEVR%\src\uevr-imgui\uevr_imconfig.hpp\" ^
  /I"%REPO%\mod\uevr\src" /I"%UEVR%\src" /I"%UEVR%\src\uevr-imgui" /I"%IMGUI%" /I"%JSON%" ^
  /Fo"%OUT%\\" /Fe"%OUT%\preview.exe" ^
  "%~dp0preview.cpp" "%REPO%\mod\uevr\src\utility\WuWaShortcutSheet.cpp" "%REPO%\mod\uevr\src\utility\WuWaLocalization.cpp" ^
  "%UEVR%\src\uevr-imgui\imgui_impl_dx11.cpp" "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" ^
  d3d11.lib d3dcompiler.lib windowscodecs.lib ole32.lib icu.lib user32.lib
