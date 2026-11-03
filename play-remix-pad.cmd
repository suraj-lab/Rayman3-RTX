@echo off
rem Rayman 3 with a launch-scoped virtual Xbox pad (ViGEm).
rem The pad exists ONLY while Rayman3.exe runs: the feeder adds it when the game appears and
rem removes it the moment the game exits, so no other game or app ever sees a controller.
rem Nothing is installed or left resident by this script.
set HERE=%~dp0
if not exist "%HERE%src\vigem_feeder.exe" (echo missing src\vigem_feeder.exe & exit /b 1)
if not exist "%HERE%src\ViGEmClient.dll"  (echo missing src\ViGEmClient.dll & exit /b 1)
start "" /min "%HERE%src\vigem_feeder.exe" --exe Rayman3.exe
call "%HERE%run_remix.cmd"
echo game exited - feeder is removing the virtual pad
