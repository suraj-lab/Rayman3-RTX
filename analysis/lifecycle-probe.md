# Offline window-lifecycle probe — 2026-11-02

No game launch, no installed-DLL replacement, no settings change.
Installed game DLL hashes were checked unchanged by `run_remix_abi.py` on every run.

## What was added

- `src/d3d8abi.cpp` gained `--lifecycle` (drives `WM_ACTIVATEAPP`, `WM_SIZE`,
  `WM_DESTROY` after device creation, catching and logging anything thrown by the
  shim) and `--fullscreen` (matches Rayman's recorded request: fullscreen at the
  desktop size, behaviour `0x44`, format A8R8G8B8).
- Diagnostics were added for this probe: `std::set_terminate` handler,
  `SetUnhandledExceptionFilter`, a vectored handler that dumps a first-chance C++
  throw's type name / exception object / throw stack, and a module+offset
  resolver for any fault address.
- `tests/run_remix_abi.py --lifecycle [--fullscreen]` stages the same isolated
  chain (proxy -> d3d8to9 -> x86 Remix shim -> ARM64 renderer) and reports
  evidence instead of a pass/fail gate.

## Results

| Case | Outcome |
|---|---|
| Windowed device + 3 lifecycle messages | device/draw/Present S_OK, messages clean, exit 0 |
| Fullscreen A8R8G8B8 + 3 lifecycle messages | device/draw/Present S_OK, messages clean, exit 0 |
| Fullscreen with `BackBufferFormat = UNKNOWN (0)` | **fatal, exit code 3** — see below |
| crosire d3d8to9 -> system D3D9, fullscreen `fmt=0` | `0x8876086C` D3DERR_INVALIDCALL, returned normally |
| crosire d3d8to9 -> system D3D9, windowed | pass |

Evidence directories: `tests/remix_abi_5fqb45rw` (windowed),
`tests/remix_abi_n0ymaxj8` (fullscreen, clean), `tests/remix_abi_d42aj6tg` and
`tests/remix_abi_4g8ca0jl` (`fmt=0` fullscreen faults), `tests/ctl_*` (controls).

## Two things this establishes

1. **The three window messages alone do not reproduce the abort.** Both windowed
   and fullscreen runs handled `WM_ACTIVATEAPP` / `WM_SIZE` / `WM_DESTROY` with
   `LRESULT 0` and normal process exit. The game's failure needs some other state.
2. **A rejected device creation is fatal, not reported.** Requesting fullscreen
   with `D3DFMT_UNKNOWN` makes the underlying layer return D3DERR_INVALIDCALL; the
   crosire control returns that HRESULT to the caller, but in the Remix chain the
   process dies instead. Recorded shape: unhandled `0xE06D7363`,
   `EXCEPTION_NONCONTINUABLE`, with a **NULL exception object and NULL ThrowInfo**
   on the *first-chance* notification. No `catch` clause can match that, and the
   whole stack of the terminating thread is the harness's own CRT
   (`__scrt_unhandled_exception_filter` -> `_abort` -> `_exit` -> `ExitProcess(3)`).

The NULL-payload, non-continuable record is the distinguishing signature: a
genuine catchable MSVC throw in the same harness (the deliberate `throw 42`) shows
a valid exception object and ThrowInfo and is caught normally.

## Correction to earlier reading

Rayman's own recorded request was `fmt=21` (A8R8G8B8) fullscreen, and its swap
chain was created successfully (`2880x1800`, `A8R8G8B8`, `Windowed: false`). The
`fmt=0` fullscreen case above is a harness parameter error, not a reproduction of
the game's failure. It is recorded because the fatal-vs-HRESULT behaviour it
exposes is a real property of this chain.

## Probe round 2 — real message pump, bridge channel, idle hold

All runs windowed and non-activating (`SW_SHOWNOACTIVATE`); no display mode change,
no fullscreen, no focus theft. Installed game DLL hashes unchanged.

| Case | Outcome |
|---|---|
| Real window + queue pump (PeekMessage/Dispatch), synthetic messages, 1 s | clean, exit 0 |
| Same, holding the post-bridge state for 8 s (`--idle`) | clean, exit 0 |

Evidence: `tests/remix_abi_nycf8b1v`, `tests/remix_abi_fxbuur5y`.

Both runs now reach the game's exact pre-fault state and survive it:
`No winproc detected, initiating bridge message channel`, then
`Message channel UWM_REMIX_BRIDGE_REGISTER_THREADPROC_MSG established` — the line
logged at 26.061 in the game's fatal run — followed by real queued message
dispatch (4 + 1 + 1 messages) and a real `SetWindowPos` resize through the OS path.

The game died ~7.9 s after that line; the harness held it 8 s and was clean. So
duration is not the trigger either.

## Dead code removed

`DEV_Reset = 14` is deleted from `src/d3d8proxy.cpp`. It was declared and never used,
never hooked, and appears in no probe log — its slot number was never confirmed.
Against the SDK ordering slot 14 is plausibly `GetBackBuffer`, whose last argument is
an out-pointer, so using it as `Reset` could write to a wild address. Do not
reintroduce it without confirming the slot from a header or by address comparison.
`python tests/run_abi.py` afterwards (`tests/abi_lsma6824`) returns exactly the same
matrix as before the edit: the three Remix/crosire paths pass direct and through the
proxy, and the same three known failures remain (dgVoodoo direct and proxy heap
corruption `0xC0000374`, BR3 through proxy access violation `0xC0000005`).
The copy under `backups/before_exit_trace_source_z35g7zer` is evidence and is untouched.

## Probe round 3 — repeats, wider message set, second device

Windowed and non-activating throughout; installed game DLL hashes unchanged.

| Case | Outcome |
|---|---|
| 8 activate/resize cycles, 80 messages incl. `WM_ACTIVATE`, `WM_NCACTIVATE`, `WM_SETFOCUS`/`WM_KILLFOCUS`, `WM_WINDOWPOSCHANGED`, `WM_DISPLAYCHANGE` | clean, 80/80 |
| Second `CreateDevice` on the same HWND (`--reuse-window`) | rejected with `0x8876086A`, no abort |

Evidence: `tests/remix_abi_ijq7afcq`, `tests/remix_abi_4mtl76je`.

Two caveats on the reuse-window case. The second device never got created, so window
setup may not have re-run at all — it is not a valid test of the PR #144 rehook shape.
And it broke the renderer's own swap chain:
`vkCreateSwapchainKHR failed again, VK_ERROR_NATIVE_WINDOW_IN_USE_KHR. Giving up.`
followed by `Failed to create swap chain` and `[80] common device objects were not
disposed of`. Those errors are an artifact of this probe, not a game finding.

## Hypotheses closed by rounds 1-3

Tested and clean: windowed device, fullscreen device at the game's parameters, the
three dispatched messages, real queue dispatch, a real OS resize, the bridge message
channel being established, an 8 s hold in that state, 8 activate/resize cycles across
80 messages, and a 320x240 draw/Present on every run. None reproduces the abort.

What is left needs one of: an activated foreground window or a display transition
(interrupts the session), a confirmed `Reset` slot, or the bridge source and symbols.

## Probe round 4 — recreation ruled out, re-entrancy tested

All windowed and non-activating; installed game DLL hashes unchanged.

**Recreation is not the differentiator — settled from logs alone.** The game's fatal
log has exactly one `D3D9DeviceEx::ResetSwapChain` block, same as every harness run,
and three `Presenter: Actual swap chain properties` dumps in both cases. The game's
buffer size moved `2880x1800` -> `2880x1800` -> `2892x1832`; mine stayed `304x201`.
So the game never performed a device Reset either; the size change came from the
renderer adjusting its own presenter. No `Reset` call was needed to establish this.

**Re-entrancy.** A WndProc that synchronously sends the paired message from inside
itself re-enters the shim's handler on the same thread — the classic way a
non-recursive lock gets locked twice. Bounded to 4 re-entries: clean, exit 0
(`tests/remix_abi_k66da2n4`). So nested dispatch through the application WndProc does
not re-enter the locked region; the shim appears to release that lock before calling
into the app proc.

Unbounded, the same probe livelocks through the message queue (my own fault, not a
shim finding): it ran to the 120 s timeout and left `d3d8abi.exe` spinning at ~82 s
CPU. That process was killed, and `tests/run_remix_abi.py` now uses `Popen` and does
`taskkill /F /T` on timeout so a hung run can never orphan a CPU-burning child again.

**Incidental renderer note.** These runs log
`[RTX-Compatibility-Info] Skipped drawcall, using pre-transformed vertices which isn't
currently supported.` — consistent with STATUS's point that the synthetic XYZRHW
triangle is not evidence about Rayman's geometry. Any future harness draw meant to
reach Remix must submit XYZ + camera, not XYZRHW.

**`Reset` remains untested and the slot remains unconfirmed.** No `d3d8.h` exists on
this machine (Windows Kits, Visual Studio, mingw, or the project); the header-derived
verification the project relies on cannot be reproduced here, so no call was made.

## Still not reproduced

The game aborted within about 8 seconds of its swap chain existing, with zero hooked
draw calls, and its window was foreground — a state this probe never enters.
Superseded next steps (1 and 2 of the earlier list are now done and clean; device
`Reset` is blocked on the unverified slot above):

1. Repeated activation/deactivation cycles, which is the shape of the
   "repeated WndProc setup" lead from dxvk-remix PR #144.
2. A wider message set (`WM_ACTIVATE`, `WM_SETFOCUS`/`WM_KILLFOCUS`,
   `WM_WINDOWPOSCHANGED`, `WM_DISPLAYCHANGE`, `WM_PAINT`), logging which handler
   takes the failing branch.
3. Confirm the device `Reset` slot from a header or by address comparison, then
   exercise reset/recreation windowed at a new size.
4. Only with approval: an activated foreground window, then a fullscreen
   transition. Both take the foreground or the display.

## Probe round 5 — approved fullscreen real-message-pump run fails

See `analysis/fullscreen-probe-findings.md` for the complete result and limits.
One approved `--lifecycle --fullscreen --stress` run (`tests/remix_abi_snxkrde5`)
created/drew/Presented successfully, then threw `0xE06D7363` during lifecycle queue
processing and exited `0xC000041D`. There was no completed lifecycle/stress verdict.
This is not yet a confirmed match to the game's mutex failure: stack capture yielded
zero frames and the type reader incorrectly used the unwind field at ThrowInfo+4.
That reader is now corrected to +12 and has a CPU-only red/green regression check
(`tests/exception_metadata_1k7xroe5`); no second fullscreen run used the correction.
Command, outcome and artifact hashes: `tests/fullscreen_probe_nm5zsdvr/`.
Installed game DLL hashes remained unchanged and no matching test processes remained.

