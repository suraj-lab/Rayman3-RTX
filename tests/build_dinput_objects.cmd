@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
cl /nologo /MT /O2 /EHsc dinput_objects.cpp /link /OUT:dinput_objects.exe /MACHINE:X86 user32.lib ole32.lib dxguid.lib
echo CL_EXIT=%ERRORLEVEL%
