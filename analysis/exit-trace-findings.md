# Rayman exit-trace checkpoint — 2026-11-01, approximately 23:15 local

## Preserved runtime evidence

Exactly one newly authorized game launch used the exit-tracing proxy.
The user observed the same runtime error. No second launch occurred.
Evidence directory: `tests/exit_trace_game_2am85wxs` (logs and SHA256 manifest).
Pre-deployment backup: `backups/before_exit_trace_game_n90y__iq`.
Game, launcher and bridge were confirmed stopped at the final checkpoint.

Installed `D3D8.dll` is now the exit tracer, SHA256:
`ba32d48ccba9f40e05c1c7b02f07255ded8c955c228968d81658ba1da06125ab`.
Its project copy is `src/d3d8proxy.exit-trace.dll`; adjacent manifest binds tested source hashes.
`src/d3d8proxy.dll` still contains the older exception-only tracer; do not confuse it with the installed DLL.
Installed Remix `d3d9.dll` SHA256:
`a2cbebedf6cc495fa47feb79d197756242e547938784011a83f99b86458dc750`.
Ubi.ini, remix.conf, converter/shim and SUJ.sav matched pre-launch hashes after capture.
TnL remains unchanged. The game remains in Remix diagnostic mode, not normal dgVoodoo mode.

## Observed during this run

- Both host RaiseException and ExitProcess IAT hooks installed.
- CreateDevice succeeded, 2880x1800 fullscreen, behavior 0x44.
- Zero hooked draw calls; no host-import exception record.
- `EXITPROCESS code=3`, caller `0x004028B0` (Rayman3.exe RVA 0x28B0).
- Windows CaptureStackBackTrace returned 15 frames, including:

| Frame | Module-relative address | Interpretation/limit |
|---|---|---|
| 1 | Rayman3.exe +0x28B0 | Return site of the decoded CRT ExitProcess import call |
| 2 | Rayman3.exe +0x151A88 | Return site of signal call inside the previously identified abort-shaped routine |
| 3–9 | KERNELBASE / ntdll | Captured Windows dispatch frames; not symbolized |
| 10 | d3d9.dll +0x7FE2A | Return site of the decoded C++ exception RaiseException call |
| 11 | d3d9.dll +0x7D324 | Return site of the decoded throw helper |
| 12 | d3d9.dll +0x7D2E8 | Return site selecting a standard-library error |
| 13 | d3d9.dll +0x5FF04 | Error branch after a lock helper returned nonzero |
| 14 | d3d9.dll +0x5E0AA | Caller dispatches window messages 0x1C, 0x5, 0x2 to that handler |

These are Windows-captured frames, not a complete independently verified unwind.
This now supplies runtime evidence connecting the CRT termination path with the shim throw path.

## Read-only decoding of the installed shim

Used MSVC dumpbin and format-aware PE import / MSVC x86 exception-metadata parsing.
Preferred DLL image base: 0x10000000; addresses below are preferred VAs, not live ASLR addresses.

- 0x1007FE24 calls through its own import with exception code 0xE06D7363.
  Its return address is the captured RVA 0x7FE2A. This explains why the host-only RaiseException hook missed it.
- The throw helper passes ThrowInfo at 0x100C8018. Following its catchable-type metadata yields
  `std::system_error`, `std::_System_error`, `std::runtime_error`, `std::exception`.
- The error-selection branch represented by captured RVA 0x7D2E8 pushes index 1.
  Its message table entry is `invalid argument`, with numeric error value 0x16.
  This message is decoded from the throw path, not captured from the actual exception object.
- Handler at 0x1005FB30 passes global object 0x100D10A0 to helper 0x1007D23A.
  Nonzero return branches at 0x1005FB79 to 0x1005FEFE, which invokes the standard error helper.
- Lock helper 0x1007D23A calls 0x1007D014. The lock/unlock code maintains owner-thread
  and recursion-count fields (+0x28/+0x2C) and calls the underlying synchronization implementation.
- Caller at 0x1005E070 handles HWND/UINT/WPARAM/LPARAM-shaped arguments and routes
  WM_ACTIVATEAPP (0x1C), WM_SIZE (0x5), WM_DESTROY (0x2) to the failing handler.

**Strong binary-supported inference:** the Remix shim throws std::system_error on a failed lock
in its window-message handling path; termination reaches Rayman's CRT abort/signal/exit path.
The underlying reason for lock failure (reentrancy, initialization, lifetime or other state corruption)
is NOT established. The exact triggering message was not recorded.
No evidence currently makes TnL or geometry processing the cause.

## Source / symbols lead, not an established fix

The binary embeds PDB identity:
`d3d9.pdb`, GUID `DCD49AD3-3444-40C9-BC8D-E7550DE10F98`, age 1.
Embedded build path ends `dxvk-remix-nv\bridge\_compRelease_x86\src\client\d3d9.pdb`.
No local PDB was found in the bounded workspace scan. Export proximity is not function attribution.

Related public PR: https://github.com/NVIDIAGameWorks/dxvk-remix/pull/144
Title: Guard repeated WndProc setup and ImGui dispatch.
At lookup it was open/unmerged; head revision `be2d48df55ba2437ac4786390e71c0192d1d95e2`.
It changes `bridge/src/client/window.cpp` to avoid unsetting/rehooking an already-hooked identical HWND,
and separately guards overlay ImGui dispatch. It is context for investigation, NOT a confirmed fix
for this mutex failure. Its source was not matched to the installed binary.

## Next session

1. Map this handler/global mutex to the matching bridge source/build or obtain matching symbols.
2. Determine why that lock returns an error. Distinguish same-thread message reentrancy from
   initialization/lifetime/corruption; do not assume a recursive-mutex substitution is safe.
3. Extend the isolated standalone test to reproduce the actual window-lifecycle/fullscreen message path.
   Existing tiny windowed-triangle tests pass but do not reproduce Rayman's window behavior.
4. Only after an evidence-backed remedy/diagnostic is validated, request approval for installed-DLL,
   config changes and another single game launch. Do not suppress the exception or bypass abort.
5. Once real draws work, return to geometry/camera capture, offline replay and pinned NieR reuse.

Standalone exit-tracer validation: `tests/abi_6jd4pkuk`, full chain `tests/remix_abi_npvuorj_`.
The full backend matrix remains non-green (dgVoodoo direct/proxy and renamed DXWrapper failures).
BR3 direct also varied between development runs; do not claim universal backend stability.
