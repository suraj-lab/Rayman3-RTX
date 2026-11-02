# Abort root cause and fix — 2026-11-02

## Confirmed causal chain

The fullscreen abort is caused by the Remix bridge's window management running
inside `gSwapChainMapMutex`. Removing that work removes the abort, and restoring it
restores the abort — demonstrated in both directions on the isolated harness.

```text
RemixWndProc (window.cpp:294)
  -> windowMsg  (window.cpp:154)
       std::scoped_lock lock(gSwapChainMapMutex)      // window.cpp:155, non-recursive
       WM_ACTIVATEAPP && !Windowed && !NOWINDOWCHANGES:
           SetWindowPos(HWND_TOP, ..., SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS)
       -> nested resize/lifecycle message re-enters windowMsg on the same thread
       -> _Mtx_lock returns nonzero (CRT mtx_do_lock, recursion without _Mtx_recursive)
       -> std::_Throw_C_error -> std::system_error
       -> Rayman CRT abort path -> ExitProcess(3) / harness 0xC000041D
```

Supporting facts already established in this project: exact release PDB match
(`window.cpp:155`), the shim's `mtx_do_lock` recursion-count check, and the
`test dword ptr [ebp-74h], 800h` guard at `0x1005FCB4` in the installed shim.

## The decisive evidence: A/B and its inversion

Isolated harness, `python tests/run_remix_abi.py --lifecycle --fullscreen --stress`.
Device created fullscreen 2880x1800, fmt 21, exactly like the game.

| Run | Behaviour flags | Launcher exit | First-chance type | Lifecycle verdict |
|---|---|---|---|---|
| Fix off | `0x00000044` | **3221226525** (`0xC000041D`) | `.?AVsystem_error@std@@` (4 catchable types) | dies mid-sequence |
| Fix on | `0x00000844` | **0** | `.H` only (harness's deliberate `throw 42`) | **clean 3/3**, stress **clean 80/80** |

Repeated as an inversion test after making the fix the default: default → exit 0 and
clean; `R3_NO_WINDOW_CHANGES=0` → exit `0xC000041D` with `std::system_error` again.
Cause and mitigation therefore track each other in both directions.

Evidence directories: `tests/remix_abi_ctdn9gq8` (fatal, fix off),
`tests/remix_abi_8hb8owdf` (clean, fix on), plus the inversion pair recorded in
`analysis/research-pass-2026-11-02/reentrancy-ab.json`.

**Why this now connects to the game.** The earlier probe could not name its throw:
the diagnostic read `ThrowInfo+4` (the unwind function) instead of `+12` (the
catchable-type array). With that parser corrected, the failing harness run reports
`std::system_error` — the same type decoded from the game's exit-trace. The isolated
reproduction and the game failure are the same exception, not merely similar ones.

## The fix

In `src/d3d8proxy.cpp`, `hCreateDevice` ORs `0x800`
(`D3DCREATE_NOWINDOWCHANGES`) into `BehaviourFlags` before forwarding:

```c
if (NoWindowChanges() && !(flags & 0x800u)) {
    L("  R3_NO_WINDOW_CHANGES: behaviour 0x%08lX -> 0x%08lX", flags, flags | 0x800u);
    flags |= 0x800u;
}
```

- **Default on**; `R3_NO_WINDOW_CHANGES=0` opts out (used as the test control).
- The value `0x800` was verified against the installed shim's own guard, not memory.
- The flag was documented (Microsoft D3DCREATE page) but its numeric value is absent
  from current Wine headers, which is why the binary was used as the source of truth.
- The proxy's standalone gate (`run_abi.py`) still passes, and the matrix's three known
  failures (dgVoodoo direct/proxy, BR3 through proxy) are unchanged from before the edit.

## Risk that must be checked in the real game

`D3DCREATE_NOWINDOWCHANGES` tells D3D not to alter the focus window. Microsoft's own
note: *"If this flag is set, the application must fully support all focus management
events, such as ALT+TAB and mouse click events."* Remix's fullscreen branch exists
precisely to reposition/restore the window, so with the flag the window is left to
the game.

Expected possible symptoms, none yet observed because the game has not run with this
build: window not positioned/sized as intended on entering fullscreen, ALT+TAB or
minimise/restore behaving differently, or the window appearing at the wrong offsets.
Windowed mode may be unaffected.

## What is NOT claimed

- Not yet run in Rayman 3. This is proven on the isolated chain, not in the game.
- The exact nested message was still not captured; the mechanism is established by
  intervention, not by an observed nested stack.
- No claim that all future aborts are gone — only this reproducible one.
- No mutex type was changed, no exception swallowed, no TnL change made.
