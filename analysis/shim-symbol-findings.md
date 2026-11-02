# Matching shim symbols and the remaining lock hypothesis — 2026-11-02

## Scope and result

Offline analysis only: no Rayman launch, installed-DLL replacement, settings change,
or new GPU probe. The exact release PDB is now available and matches the shim's
CodeView identity. This resolves the previously unnamed handler and global mutex;
it does **not** yet establish why the mutex state is invalid at runtime.

Evidence is preserved under `analysis/shim-symbols-1.6.1/`:

- `manifest.json`: hashes for the DLL copy, PDB, 60 client source files, symbolizer
  output, and two bounded disassemblies.
- `exit-stack-symbols.jsonl`: LLVM symbolizer JSON output (one array-valued record).
- `window-lock.txt` and `mutex-lock.txt`: MSVC dumpbin output with matching PDB names.
- `source/bridge/src/client/`: upstream client `.cpp`/`.h` snapshot at the release tag.

These are ordinary LLVM/MSVC/static-file observations, not REA Evidence IDs.
REA was connected but had no configured native analysis provider for this Windows
ARM64 host; no provider was installed and no target was opened or executed.

## Exact symbol identity

Downloaded NVIDIA's public x86 **release** symbols for `remix-1.6.1`:

https://github.com/NVIDIAGameWorks/rtx-remix/releases/download/remix-1.6.1/rtx-remix-for-x86-games-release-remix-1.6.1-symbols.zip

Archive SHA256, checked against GitHub's release asset digest:
`246494b51106950153a4d1994da9eb13c5800edd9f6011273a8a4cc2272b69b5`.
Only the root `d3d9.pdb` was extracted; the `.trex` and `.trex_arm64` PDBs target
other binaries and were not substituted for it.

The PDB info stream was decoded and asserted:

- GUID `DCD49AD3-3444-40C9-BC8D-E7550DE10F98`, age **1** — matches the shim.
- PDB SHA256 `19965a96ac829d9f6c2333f4215315a17cb81d9fa643b1e200d1a1cd73e1870b`.
- Local shim copy SHA256 `a2cbebedf6cc495fa47feb79d197756242e547938784011a83f99b86458dc750`.

The annotated upstream `remix-1.6.1` tag resolves to commit
`0a6cd5b1ee814ec390c358e0c9c32ece8895ddc0`. Its relevant source lines align with
the PDB's file/line records and the binary's disassembly. This is a targeted source
match, not a claim that the entire public tree reproduces NVIDIA's build byte-for-byte.

## Captured stack, now symbolized

The five captured shim return addresses were adjusted by **-1** to attribute the
calling instruction rather than the instruction after the call. Function entry
addresses were queried without that adjustment.

| Captured return RVA | Lookup RVA | Matching PDB attribution |
| --- | --- | --- |
| `0x7FE2A` | `0x7FE29` | `_CxxThrowException`, `throw.cpp:74` |
| `0x7D324` | `0x7D323` | `std::_Throw_Cpp_error(int)`, `thread0.cpp:35` |
| `0x7D2E8` | `0x7D2E7` | `std::_Throw_C_error(int)`, `thread0.cpp:53` |
| `0x5FF04` | `0x5FF03` | inlined `_Check_C_return` -> `_Mutex_base::lock` -> `scoped_lock<mutex>` -> `WndProc::windowMsg`, `window.cpp:155` |
| `0x5E0AA` | `0x5E0A9` | `WndProc::RemixWndProc`, `window.cpp:294` |

Additional entries: `0x5FB30` is `WndProc::windowMsg` at line 154;
`0x7D23A` is `_Mtx_lock`; `0x7D014` is `mtx_do_lock`.

Reproduce with the installed Visual Studio LLVM symbolizer (no package installation):

```powershell
$llvm = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\bin\Hostarm64\arm64\llvm-symbolizer.exe'
& $llvm '--obj=analysis/shim-symbols-1.6.1/d3d9.dll' --relative-address --output-style=JSON 0x7fe29 0x7d323 0x7d2e7 0x5ff03 0x5e0a9 0x5fb30 0x7d23a 0x7d014
```

## Which mutex, and where it is used

The failing statement is exactly:

```cpp
// bridge/src/client/window.cpp:155
std::scoped_lock lock(gSwapChainMapMutex);
```

The PDB-assisted disassembly independently names `gSwapChainMapMutex` as the
argument at VA `0x1005FB60`; the prior decoding placed that object at VA
`0x100D10A0`. The failed `_Mtx_lock` return branches to `_Throw_C_error` at
`0x1005FEFE`.

A search of all **60** client `.cpp`/`.h` files at the pinned release found three
lock sites, plus their declarations/definition:

- `d3d9_lss.cpp:128`: global `std::mutex` definition.
- `d3d9_device.cpp:3813–3815`: lock/update/unlock the HWND-to-swapchain map.
- `d3d9_swapchain.cpp:69–75`: lock/conditionally erase/unlock a map entry.
- `window.cpp:155–191`: scoped lock for lifecycle-message processing.

This search boundary is the client source directory, not every file in the repository.
PR #144 still reports open/unmerged at this lookup; its WndProc-setup guard is not
a demonstrated repair of this lock site.

## Narrower, testable hypothesis

Inside `windowMsg`, the mutex remains locked while the fullscreen
`WM_ACTIVATEAPP(TRUE)` branch calls `SetWindowPos` (`window.cpp:172–174`).
Its flags include `SWP_ASYNCWINDOWPOS`, but that flag only posts the request when
the caller and window owner are attached to **different input queues**. It does
not make a same-thread call inherently asynchronous. See:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos

A synchronous resize can deliver a nested `WM_SIZE`, which `RemixWndProc` routes
back into `windowMsg` **before** invoking the application's WndProc. That would
attempt to acquire this non-recursive mutex twice on the same thread.

The matching binary's `mtx_do_lock` supports this hypothesis:

- `_Mtx_lock` supplies a NULL timeout.
- The owner-thread comparison is at `0x1007D080`.
- The recursion count is incremented at `0x1007D167–16D`.
- For count > 1 without recursive flag `0x100`, it restores the count and returns
  the nonzero status through `0x1007D180–1AC` (status 3 for NULL timeout).

This makes recursive acquisition a concrete explanation for the observed error,
not an unspecified OS lock failure. It is **not yet proven for Rayman**: corrupted
mutex/count state could also reach that path. Neither the exact triggering message
nor owner/count fields were captured in the game run.

The earlier application-WndProc nesting probe does not rule this out: that probe
re-entered from the game's callback after the shim's handler returned, whereas
this candidate nesting occurs inside the shim's own locked `SetWindowPos` call.
The original fullscreen probe predates the later real-message-pump/stress changes;
no evidence yet combines all of those conditions.

## Proposed bounded test — subsequently authorized and executed once

Update: see `analysis/fullscreen-probe-findings.md`. The combined fullscreen/pump
run failed with a C++ callback exception, but incomplete stack/type diagnostics
prevent attributing it to this mutex yet. The plan below records the original
acceptance criteria; another fullscreen launch needs separate approval.

Prefer the isolated harness, not another unchanged Rayman launch:

1. Exercise the fullscreen lifecycle path with the current real message pump and
   resize stress, explicitly ensuring the window size changes on its owning thread.
2. Capture the first throw's type/stack and distinguish nested `windowMsg` frames
   from unrelated startup or invalid-presentation errors.
3. A strong reproduction needs both a failing stack at this exact mutex and evidence
   of the outer fullscreen `SetWindowPos` call. A clean run only bounds the tested
   state; it does not refute all reentrancy or lifetime faults.

This test can change display state or take focus, so obtain explicit approval.
Do not replace a game DLL, suppress the throw, substitute a recursive mutex, or
change TnL based on the hypothesis alone. If the isolated case stays clean, the
next game diagnostic should record the incoming message plus mutex owner/count
before acquisition, rather than repeat an uninstrumented launch.
