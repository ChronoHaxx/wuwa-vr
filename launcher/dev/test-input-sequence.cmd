@call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
@if errorlevel 1 exit /b 1
@cd /d "%~dp0.."
@if not exist "extracted\input-sequence-check" mkdir "extracted\input-sequence-check"
@cl /nologo /utf-8 /std:c++latest /EHsc /MT /O2 dev\test-input-sequence.cpp /Fo"extracted\input-sequence-check\test.obj" /Fe"extracted\input-sequence-check\test.exe"
@if errorlevel 1 exit /b 1
@"extracted\input-sequence-check\test.exe"
@exit /b %errorlevel%
