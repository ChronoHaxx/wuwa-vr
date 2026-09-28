@call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
@if errorlevel 1 exit /b 1
@cd /d "%~dp0.."
@if not exist "extracted\eye-diff-check" mkdir "extracted\eye-diff-check"
@cl /nologo /utf-8 /std:c++latest /EHsc /MT /O2 dev\test-eye-diff.cpp /Fo"extracted\eye-diff-check\test.obj" /Fe"extracted\eye-diff-check\test.exe"
@if errorlevel 1 exit /b 1
@"extracted\eye-diff-check\test.exe"
@exit /b %errorlevel%
