# Rayman 3 mod hook analysis — desktop REA/Ghidra pass, 2026-11-02

## What was analysed

Nine hashed DLL copies were transferred to an isolated desktop directory and
analysed **statically** by the desktop's already-registered REA 6.4.0 + Ghidra
12.1.2 provider. Nothing was executed: no game, mod, or transferred DLL. The
installed game and the desktop's existing REA checkout/config were not modified.

- Remote workspace: `~/Projects/rayman3-rtx-analysis/hooks_0s8j6upd/`
- Local hashes/inputs: `analysis/research-pass-2026-11-02/desktop-input-manifest.json`
- Raw extracts: `pass1-summary.txt`, `pass2-raw-matches.txt`,
  `pass3-window-hook-evidence.txt`, `pass4-caller-functions.txt`,
  `pass5-function-dossiers.txt`
- Remote per-call JSON (each with an REA Evidence ID): `evidence/pass1`…`pass5`

REA Evidence IDs cited below are from that remote session store; the JSON files
holding them are on the desktop.

## Main result: no loaded mod hooks the window procedure

| Artifact | Installed/loaded in the failing configuration | Window-procedure hook |
|---|---|---|
| Ultimate ASI Loader (`winmm.dll`) | Yes, recorded in earlier dialog dump | **No** (see below) |
| Creative ALchemy (`dsound.dll`) | Yes, recorded in earlier dialog dump | **No** |
| BetterRayman3 (`scripts\BetterRayman3.asi.disabled`) | **No — disabled** | Not loaded |
| Xidi `dinput8.dll` | No (system `dinput8.dll` was loaded) | Not loaded |
| DXWrapper `d3d8.dll` | No (Remix proxy is `D3D8.dll`) | Not loaded |
| dgVoodoo `D3D8.dll` | No (`_remix_off`) | Not loaded |

So within this evidence boundary, the only WndProc installed in Rayman is the
Remix bridge's own `RemixWndProc`. Mod interference with the window procedure is
not supported for the failing configuration.

## Ultimate ASI Loader (installed `winmm.dll`)

Evidence: `ev_231e16aa…`, `ev_555553b4…`, `ev_363525bd…`, `ev_0825a6ae…`.

- It is a genuine ASI loader: strings `*.asi`, `scripts\`, `plugins\`,
  `.\modloader\modloader.asi`, `scripts\global.ini`, `plugins\global.ini`.
- Loader routine `FUN_10019a50` (0x10019A50) calls `FindFirstFileW`/`FindNextFileW`
  → `SetCurrentDirectoryW` → `LoadLibraryW` → `GetProcAddress`, with `MessageBoxW`
  and `GetLastError` for failures. That is directory scanning and loading only.
- Its `SetWindowPos` / `SetWindowTextW` strings are **not** referenced by any code
  (xrefs on 0x10080E30 and 0x10080E82 returned zero). They belong to the module's
  `winmm` export/name tables, which the loader proxies. No `SetWindowLong`,
  `GetWindowLong`, `CreateWindow` or `WM_` handling strings were found.
- It therefore does not add a competing WndProc. It can still affect what loads at
  startup (`scripts\*.asi`), but `BetterRayman3.asi` is currently disabled.

## BetterRayman3 (present but disabled)

Evidence: `ev_378c38a5…`, `ev_22790a41…`, `ev_3e8a5a71…`, `ev_350c084d…`,
`ev_89790809…`, `ev_c03eea5c…`.

- Embedded build path:
  `…\BetterRayman3\Rayman3.WidescreenFix\external\Hooking.Patterns\Hooking.Patterns.h`
  — it includes RibShark's **WidescreenFix** and a **pattern-scanning hook library**.
  The header path string is referenced from 15 code sites, consistent with many
  pattern-based patch sites.
- Imports are exactly the `VirtualProtect` / `GetProcAddress` / `LoadLibraryExW`
  trio — runtime patching with late binding, no static D3D8/DirectInput import.
- `FUN_10001D20` resolves and reads `C:\Windows\Ubisoft\Ubi.ini`
  (`GetModuleFileNameA`, `GetModuleHandleExA`, string-splitting helpers).
- `FUN_10002080` parses numeric settings (`_strtol`, `invalid stoi argument`,
  `stoi argument out of range`) including `OuterDeadZone`, under config key
  `BetterRayman3`.
- `GetActiveWindow` and `GetProcessWindowStation` appear as strings only; the
  provider could not resolve xrefs to them, so they are most likely reached via
  `GetProcAddress` — unresolved, not proven unused.

This is the only mod here that patches game code, and it is currently disabled.
If it is ever re-enabled, its WidescreenFix pattern patches and window-station
probing are the ones to re-check against the Remix bridge.

## Creative ALchemy (`dsound.dll`)

Evidence: `ev_982e7380…`, `ev_441efaab…`.

A standard DirectSound proxy: exports `DirectSoundCreate8`, `DirectSoundEnumerateA/W`,
`DirectSoundCapture*`, forwards to the real `dsound.dll`, and logs per-buffer
`QueryInterface`/`AddRef` traffic. No window, input or device-hook behaviour.

## Limits

- Static only: no module was executed, so none of this is runtime attribution.
- Absence of a string/API match is not proof of absence; indirect or
  `GetProcAddress`-resolved calls (as seen in BetterRayman3) are not fully covered.
- `find_xrefs_to_name` could not resolve imported API names on this provider;
  conclusions use concrete string addresses and function dossiers instead.
- The earlier dialog-state minidump is historical module membership, not a capture
  of the latest run.
- The Remix shim's own mutex failure remains the leading explanation for the abort;
  this pass removes mod WndProc interference as a competing explanation for the
  currently installed configuration.
