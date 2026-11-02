# Authorized fullscreen lifecycle probe — 2026-11-02

## Outcome

One user-approved isolated launch ran:

```text
python tests/run_remix_abi.py --lifecycle --fullscreen --stress
```

The runtime **failed**, after successful device creation/drawing/Present, during
lifecycle-message processing. This is a new failing standalone case, **not yet a
confirmed reproduction of Rayman's gSwapChainMapMutex failure**.

- Summary/command/pre/post hashes: `tests/fullscreen_probe_nm5zsdvr/`.
- Staged runtime and original logs: `tests/remix_abi_snxkrde5/`.
- Duration: approximately 16 seconds; no timeout or forced termination was needed.
- Launcher/runtime exit: **3221226525 = 0xC000041D**, an unhandled user-callback exception.
- Python runner exit: **0**, because lifecycle mode collects evidence rather than
  enforcing a success gate. Do not describe this as a passing runtime test.

Rayman was not launched. All installed game DLLs matched their pre-run hashes.
Post-run process checks found no matching harness/launcher/bridge processes and
no Rayman3 process. No installed DLLs or settings were replaced or changed by us.
The test used the existing staged harness; no second fullscreen run occurred.

## What the logs establish

`abi.log` records:

1. CreateDevice S_OK, `2880x1800`, fullscreen, format 21, behavior `0x44`.
2. SetTransform, FVF, BeginScene, DrawPrimitiveUP, EndScene and Present all S_OK.
3. Lifecycle began and two real message-pump intervals completed (5 messages,
   then 1 message).
4. Explicit `WM_ACTIVATEAPP(TRUE)`, `WM_SIZE(512x384)` and
   `WM_ACTIVATEAPP(FALSE)` sends returned LRESULT 0.
5. The following queued-message phase encountered first-chance C++ exception
   `0xE06D7363` on thread 31824, the harness's main thread.
6. Its nonzero object/ThrowInfo pointers were `0x007BEB10` and `0x70918018`.
7. The exception became unhandled with `EXCEPTION_NONCONTINUABLE`, followed by
   process exit `0xC000041D`. There is no lifecycle verdict or completed stress
   summary. Do not claim that the 80-message stress loop completed or even began.

`bridge32.log` records a fullscreen window-position reset at 21:49:56.810.
`bridge64.log` records the client unexpectedly exiting and server shutdown.
The draw is still the harness's XYZRHW triangle: API success is not proof of
ray-traced output or any Rayman frame.

## Why attribution remains incomplete

The vectored handler captured **zero throw-stack frames**. Its type reader also
printed an invalid count, `-956022387`, rather than a reliable type name.

Source inspection found a concrete diagnostic bug: `DumpTypeName` read
`ThrowInfo + 4`, which is `pmfnUnwind`, as though it were `pCatchableTypeArray`.
The installed MSVC `ehdata_forceinclude.h` defines four x86 fields:

```text
+0  attributes
+4  pmfnUnwind
+8  pForwardCompat
+12 pCatchableTypeArray
```

The raw recorded ThrowInfo address alone is not enough to bind it to a shim RVA:
its owning module/base was not logged. Do not infer the exception type or claim a
nested windowMsg/SetWindowPos stack from this incomplete record. Similarly, the
runner's empty host-import exception list does not mean no shim throw happened;
the first-chance harness log explicitly records a C++ throw.

## Minimal diagnostic correction and CPU-only regression

`src/d3d8abi.cpp` now reads the type-array pointer at **+12**, with the layout
comment corrected. Its only caller is `FirstChance` in that same file. No hook,
rendering code, installed binary, or staged executable was replaced.

`tests/exception_metadata.cpp` includes the actual harness implementation with
its main renamed and never called. A synthetic x86 ThrowInfo deliberately puts
negative data behind the unwind field and a valid type list behind +12. It checks
both type-name decoding and a NULL type-array pointer, without D3D initialization.

Evidence: `tests/exception_metadata_1k7xroe5/`:

- Before correction: compiled successfully, regression **failed** (exit 1),
  printing the fake destructor bytes as a negative catchable-type count.
- After correction: compiled successfully, regression **passed** (exit 0).
- The original and corrected test executables/logs are retained separately;
  `manifest.json` binds source and evidence hashes.

The metadata correction cannot retroactively recover this probe's type or stack.
The full D3D backend matrix was not rerun; the CPU-only check does not claim it passes.

## Next

Before asking for another fullscreen launch, stage a freshly built harness with
the corrected metadata reader and improve throw-stack/module-RVA capture. The
current runner selects an existing `abi_*` build; editing source alone does not
update that executable. Bind the next staged executable to its source hashes.
Then request a separately authorized single isolated fullscreen replay. The needed
acceptance evidence remains: the exact throw type/handler, and, if reentrancy is
the cause, the outer locked SetWindowPos plus nested windowMsg acquisition.
Do not change mutex type, swallow exceptions, alter TnL, or deploy a game fix yet.
