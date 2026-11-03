# REA brief: what to ask the desktop

Host: `arch` (Linux x86_64). REA verified there; **this laptop cannot run REA** (`rea doctor`: host
`win32 arm64` = unsupported_host, and no analysis engine installed).

## Transfer (once SSH is up)

    ssh suraj@arch 'mkdir -p ~/analysis/rayman3'
    scp "C:/Program Files/GOG Galaxy/Games/Rayman 3/Rayman3.exe" suraj@arch:~/analysis/rayman3/
    scp "C:/Program Files/GOG Galaxy/Games/Rayman 3/dinput8.dll.inputfix.bak" suraj@arch:~/analysis/rayman3/

Hashes of the copies to transfer:

| file | bytes | sha256 |
|---|---|---|
| Rayman3.exe | 2,916,704 | `64deefd3a7b14a32124aa9b31ebba84e1a7c9a4a54ee40d9b65b4043ff1be15c` |
| dinput8.dll.inputfix.bak | 156,160 | `a4e12d245f9212143449b5544296de832bdcebbc62e2270eb98cb3b0677dab8d` |
| r3probe.log (play run) | 14,068,092 | `e11583198c79670a0f25b76a3532dd4a698a907dc035b92689165589dd6cf374` |

## Question 1 (lighting, highest value)

How does the renderer consume per-vertex `DIFFUSE`? World draws are dominated by `FVF 0x112`
(XYZ|NORMAL|DIFFUSE|TEX1, 312k draws) and `0x352` (XYZ|NORMAL|DIFFUSE|TEX3). Remix documents
`rtx.vertexColorStrength` (default 0.6) and `rtx.ignoreAllVertexColorBakedLighting`, which implies it treats
vertex colour as baked lighting. What we need: does the game write *final* baked light into `DIFFUSE`
(so Remix's division by the largest component in `vertexColorIsBakedLighting` changes the balance), or is it
an albedo tint? Deliverable: where the vertex colour is produced, and whether any material flag marks
emissive surfaces.

## Question 2 (only if needed)

Rayman 3's IPT layer (`IPT_fn_vActiveJoystickControl`, `IPT_fn_vActivePaddleControl`,
`IPT_fn_bIsJoystickControlAvailable`) — what makes it pick joystick (analog) vs pad (digital) for a device.
Relevant only if `BR3_Config` axis mapping cannot fix the DualSense.

## Not a REA question

Remix texture hashes (`rtx.lightConverter` tagging) come from the runtime (XXH64 over image hash + asset data
hash), so they need a capture during play, not decompilation.

## Results so far (2026-11-03, Ghidra 12.1.2 on the desktop)

Import + auto-analysis of `Rayman3.exe` succeeded (`~/analysis/rayman3/proj1`, x86:LE:32, PE loader, sha256
`64deefd3...`). Two findings, both negative for static analysis of the input path:

1. **The `IPT_fn_*` strings have zero static references.** A Ghidra script located all of them
   (`IPT_fn_bIsJoystickControlAvailable @ 005b3783`, `vActiveJoystickControl @ 005b3dc6`,
   `vActivePaddleControl @ 005b3de4`, `vGetJoystickAxesForPadNumber @ 005b40ad/005b40d1`,
   `vSetJoystickAxesForPadNumber @ 005b433b/005b435f`) and found **no xrefs** to any of them, so the input layer
   resolves these by name at run time rather than through a call graph.
2. **Only one DirectInput import**: `Rayman3.exe` imports exactly `DirectInput8Create` from `dinput8.dll`
   (11 DLLs total). Everything else (`EnumDevices`, `CreateDevice`, `SetDataFormat`, `GetDeviceState`,
   `SetProperty`, ...) goes through COM vtables, so no named call sites exist.

Conclusion: explaining Rayman 3's joystick-vs-pad selection from the binary alone is expensive (vtable-driven,
runtime name dispatch). The controller mapping is better solved empirically — `R3_Setup_DX8.exe` (the game's own
setup tool, which the game demands at startup) or `BR3_Config.exe` axis numbers.

REA remains the right tool for the *lighting* question, where the code path is ordinary rendering, but even there
the empirical test (`rtx.vertexColorStrength` / `rtx.ignoreAllVertexColorBakedLighting`) is cheaper and answers the
same question first.
