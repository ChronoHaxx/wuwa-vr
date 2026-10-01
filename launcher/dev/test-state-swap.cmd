@call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
@if errorlevel 1 exit /b 1
@cd /d "%~dp0.."
@if not exist "extracted\state-swap-check" mkdir "extracted\state-swap-check"
@cl /nologo /utf-8 /std:c++latest /EHsc /MT /O2 dev\test-state-swap.cpp /Fo"extracted\state-swap-check\test.obj" /Fe"extracted\state-swap-check\test.exe"
@if errorlevel 1 exit /b 1
@"extracted\state-swap-check\test.exe"
@exit /b %errorlevel%
