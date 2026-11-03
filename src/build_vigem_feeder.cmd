@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_amd64.bat" >nul
cl /nologo /MT /O2 /EHsc vigem_feeder.cpp /link /OUT:vigem_feeder.exe /MACHINE:X64 setupapi.lib hid.lib user32.lib
echo CL_EXIT=%ERRORLEVEL%
