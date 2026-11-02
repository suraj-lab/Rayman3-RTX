# Offline termination-path findings — 2026-11-01

Target: installed GOG Rayman3.exe, SHA256
`64deefd3a7b14a32124aa9b31ebba84e1a7c9a4a54ee40d9b65b4043ff1be15c`.
Image base: 0x00400000.
The game, launcher, and bridge were confirmed stopped after the user terminated the failed run.

## Evidence source and limits

MSVC dumpbin `/imports` and `/disasm:nobytes` on the installed executable.
These are decoded instructions, unlike the earlier raw-byte geometry guesses.
REA's configured native disassembly providers are unavailable on this host; no REA decompilation was performed.
No application or system configuration was changed to work around that limitation.

## Runtime message implementation

The executable itself contains `Runtime Error!`, `Microsoft Visual C++ Runtime Library`,
and `abnormal program termination` strings.
The message writer at 0x00404B69 searches a code/message table at 0x005B6D50.
Its instructions at 0x00404C3C and 0x00404C82 reference the first two strings.
The abnormal-termination text occurs at 0x00597F7A (this may be inside a longer message).
The evidence supports an embedded CRT reporting implementation, not the prior debug-MFC assertion theory.

## Abort-like routine (static inference)

Decoded instructions:

```
00551A7A: push 0Ah
00551A7C: call 00404B69
00551A81: push 16h
00551A83: call 005523F1
00551A88: pop ecx
00551A89: pop ecx
00551A8A: push 3
00551A8C: call 004027FC
```

This matches the traditional CRT abort shape (runtime message 10, signal 22, exit 3).
The only decoded direct incoming branch found in this disassembly was:

```
0054DE93: call 0054DE98
0054DE98: jmp 00551A7A
```

Indirect callers are not covered by this direct-branch search.
These addresses identify a diagnostic target, NOT the cause of the game's observed abort.
The live-dialog minidump contains ARM64 contexts and does not establish that the observed execution took this chain.

## Exception-tracing run

A tested host RaiseException import hook was used in one authorized game launch at approximately
22:45 local time on 2026-11-01. Evidence: `tests/trace_game_6syzwgke`.
The hook installed, CreateDevice succeeded, and a detach summary recorded zero hooked draws.
No RaiseException record was logged. The user observed the same runtime error.
The process had exited before capture, so this evidence contains no new live dump or process exit status.
This narrows the diagnostic, not the exception universe: dependency imports and hardware exceptions are outside the hook.

## Decoded reporting and shutdown paths

Further dumpbin decoding plus PE import/string parsing on the same hash-verified executable:

- Reporter call at 0x00404C88 goes to helper 0x00407E3F.
- Helper loads `user32.dll` (string 0x005980E0), obtains `MessageBoxA`
  (string 0x005980D4) through GetProcAddress (host IAT VA 0x0059718C),
  caches it at 0x005CFB3C, and calls that pointer at 0x00407EBA.
- The separately named MessageBoxA import is at 0x005972B0. Hooking that named import alone
  would not intercept this CRT helper's dynamically resolved call.
- Abort-shaped routine 0x00551A7A calls 0x004027FC with exit code 3.
- 0x004027FC calls common shutdown routine 0x0040280D.
- The normal branch calls the named ExitProcess import (IAT VA 0x005970D0)
  at 0x004028AA; its return site would be 0x004028B0.
- A conditional alternative at 0x00402829 calls the named TerminateProcess import
  (IAT VA 0x005970D4). The new candidate does not hook this alternative.
- Routine 0x0054DE3C invokes an optional thread-local handler then calls the abort stub;
  its decoded incoming branches include exception-handling/metadata paths. This is
  consistent with CRT terminate machinery, not proof of the actual runtime caller.

These are static paths, NOT an observed abort stack or a diagnosis of the trigger.

## Next bounded diagnostic: host ExitProcess

A separate candidate now logs ExitProcess's unchanged exit code, caller, and up to 16
Windows CaptureStackBackTrace frames with module-relative offsets, then forwards the call.
It reuses the existing bounded host-import scanner for RaiseException and ExitProcess.
No exception is swallowed, abort bypassed, or device capability altered.

The first candidate captured only its own frame under x86 /O2, so the test requiring a host frame failed
(`tests/abi_511d_by5`). Keeping a frame pointer in the exit hook fixed that harness case.
Latest matrix: `tests/abi_6jd4pkuk`; the two d3d8to9 backends pass direct/proxy graphics,
exception-semantics checks, and explicit ExitProcess(73) tests with exit code 73 preserved.
The complete matrix still fails the known dgVoodoo direct/proxy and renamed DXWrapper cases.
BR3 direct also failed the first development run after Present but passed the next; consistency is unresolved.

Full isolated Remix chain: `tests/remix_abi_npvuorj_`, launcher exit 0, Present S_OK,
unchanged SEH/C++ checks, ExitProcess code 0, and ten captured module-resolved frames.
Actual Rayman unwind depth is untested; frame-pointer omission/corruption may truncate it.

Candidate: `src/d3d8proxy.exit-trace.dll`, SHA256
`ba32d48ccba9f40e05c1c7b02f07255ded8c955c228968d81658ba1da06125ab`.
Adjacent manifest binds source hashes and passing-path evidence at candidate creation time.
It was subsequently installed for one authorized run at approximately 23:15 local time.
That run captured ExitProcess code 3, the CRT abort/signal path and a std::system_error throw path
inside the Remix shim's window-message lock handling. Evidence: `tests/exit_trace_game_2am85wxs`.
See `analysis/exit-trace-findings.md` for the decoded metadata, stack offsets and next steps.
The installed DLL is now this exit tracer, not the prior exception-only build.
Obtain specific approval before another installed-DLL replacement, settings change or game launch.
