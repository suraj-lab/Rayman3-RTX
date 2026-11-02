# Mod inspection hints — 2026-11-01

Read-only inspection of extracted Better Rayman 3 installer payload. No mod was installed or executed.

## Strongest lead: Transform and Lighting warning

BR3_Config.exe SHA256:
`65145dccdcf2a4f8941018f649af1bd4a06de533cf966003370d35f0bbd6ca6a`.

REA classified this as a .NET Framework 4.0 x86 CIL application without executing it.
Artifact evidence: `ev_482e058960672232fbcb0d7da07ee8a75b503c0387dddf231df8dad34eb4cc25`.
Resource-page evidence: `ev_d284cd99dee882f9cba89312dc83aa3889164e3e5520b28dc5110c438b9acd12`.

Embedded `Rayman3ConfigurationForm.Properties.Resources.resources` is 646 bytes,
SHA256 `d9b75818a14dab00c6d60f7a79135d32ce31628472e0e85ec7aff0e41ce9a756`.
Its raw resource bytes contain the text:

> Enabling Transform and Lighting is known to cause major graphical issues, including missing textures. Are you sure you want to enable it?

Associated warning title:

> Enable Transform and Lighting?

The configurator also contains the UI label `Hardware Vertex Processing` and the identifiers
`checkBoxTnL` and `HardwareVertexProcessing`.
Current read-only game-config observation is `TnL=1`; the corrected proxy recorded CreateDevice flags `0x44` (hardware vertex processing plus FPU preserve).
This corroborates an existing documented compatibility concern. It does NOT establish that TnL caused the observed abort.
A future one-variable TnL-off comparison must also check resulting geometry/transforms: startup stability is not proof of Remix camera compatibility.
No Ubi.ini settings were changed.

## Native fix and DXWrapper

- BetterRayman3.dll is native x86, SHA256 `44e79c3bebbee0a9ef435296be6bc549d214472ec3f1de6343628c8c5eb2dbbc`.
- It matches the already-preserved BetterRayman3.asi byte-for-byte; reinstalling it introduces no new fix binary.
- Its import table includes VirtualProtect, consistent with runtime patching. That alone does not identify any patch's semantics.
- Decoded disassembly contains pattern/address-handling code; a bounded direct-address scan did not establish a D3D8-specific workaround. No absence claim about all code is made.
- Bundled d3d8.dll is DXWrapper 1.0.990.18 (DDrawCompat 0.2.1 / DxWnd 2.03.60), SHA256 `5c4a808db809e05889a83977fd0ca984e534c2c7f4fcbbf0b312957776d87c47`.
- Packaged d3d8.ini enables loading BetterRayman3.dll but sets `D3d8to9=0`. Default mod installation is therefore not evidence that its D3D8-to-9 path is known-good.
- Prior standalone tests established DXWrapper conversion works under its original d3d8.dll name, but fails when renamed behind the proxy; the renamed-case log shows generic API hooking. Do not stack it behind the proxy without resolving that behavior.
- Public author fork exists at https://github.com/RibShark/dxwrapper, but the inspected current revision `81ce55f6486fcf361f82a4f229ec69b8ac648ba6` was NOT matched to the packaged 1.0.990.18 binary. No source/binary equivalence is claimed.

## Trace checkpoint

Exception tracing was implemented and standalone-validated, then used in one specifically authorized game launch.
Evidence: `tests/trace_game_6syzwgke`. The host RaiseException hook installed, CreateDevice succeeded,
but no host-import exception was logged before the user-observed runtime error and process exit.
This does not rule out exceptions raised elsewhere or establish TnL as the cause. Ubi.ini was unchanged.

A separate ExitProcess tracing candidate was subsequently built and tested offline.
Matrix: `tests/abi_6jd4pkuk`; full Remix chain: `tests/remix_abi_npvuorj_`.
The exit tracer `src/d3d8proxy.exit-trace.dll` was subsequently installed for one authorized Rayman run.
It captured code-3 CRT termination with a shim std::system_error throw path after a failed lock in
window-message handling. Evidence: `tests/exit_trace_game_2am85wxs`; details: `analysis/exit-trace-findings.md`.
The lock's failure reason is unresolved. This is a stronger current lead than the general TnL warning.
Another installed-DLL replacement/launch or TnL setting change requires specific approval.
Do not conflate the mod warning with an abort diagnosis.
