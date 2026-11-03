# Controller findings (2026-11-03)

## What the device actually is
DualSense over Bluetooth. Windows exposes it as HID/DirectInput, **not** XInput (all four XInput slots polled
as disconnected). DirectInput enumerates it as a 6-axis / 14-button / 1-POV device, with or without ribshark's
`dinput8` fix present: the fix wraps *XInput* pads, so it does nothing for this controller.

## Observed behaviour in game
Detection works (operator reached a new area using the pad), but the mapping is wrong: menus do not respond and
the left stick behaves digitally ("like WASD").

## R3_Setup_DX8.exe is a dead end for persistence
Its strings show `Joystick`, `JoystickChoose`, `JoystickGroup`, `JoystickHMvt`, `JoystickVMvt`,
`JoystickButton` and "Move the joysticks axis to detect them", i.e. it binds **camera** axes only.
Run twice, finished with OK/Apply: **nothing is written**. Verified by diffing
`HKLM\SOFTWARE\WOW6432Node\Ubi Soft\RAYMAN 3` against a `.reg` export taken beforehand (identical: only
InstallDir/Engine/CompleteInstall) and by a filesystem scan of the game folder, Documents, Saved Games and AppData
(no writes at all). So movement mapping is not configured by this tool, and the camera binding does not survive
the tool's own exit in any form we can find.

## Where the mapping fix has to live
Everything active under Remix mode must do the work: the game itself (in-game options) or the `dinput8` layer.
`BR3_Config.exe` exposes axis numbers and is the commonly cited fix, but BetterRayman3 is *disabled by
`r3mode.py remix`*, so its settings would not apply in Remix mode unless we change that decision deliberately.

## Static RE result (Ghidra on the desktop)
`Rayman3.exe` imports exactly one symbol from `dinput8.dll` (`DirectInput8Create`); device handling is COM
vtables, and the `IPT_fn_*` name strings have **zero static xrefs** (runtime name dispatch). Explaining
joystick-vs-pad selection from the binary alone is therefore expensive; empirical mapping is the cheaper path.
