# Lead static review while online researchers run

## New result: two matching fingerprints for the fullscreen throw

Recorded first-chance ThrowInfo: `0x70918018`.
Recorded invalid type count from the old +4 parser: `-956022387`.

A format-aware PE scan checked the four actual staged x86 images from
`tests/remix_abi_snxkrde5`: `d3d9.dll`, `d3d8_real.dll`, `d3d8.dll`, and
`d3d8abi.exe`. Assuming a 64-KiB-aligned module load, it enumerated candidate RVAs
with the pointer's low 16 bits (`0x8018`), then validated each ThrowInfo's pointer
graph through CatchableTypeArray/CatchableType/TypeDescriptor to MSVC type names.

Exactly one structurally valid candidate was found within that search boundary:

- Image: Remix shim `d3d9.dll`.
- ThrowInfo RVA: `0xC8018`.
- Catchable types: `std::system_error`, `std::_System_error`,
  `std::runtime_error`, `std::exception`.
- This candidate would imply live module base `0x70850000`.

An independent fingerprint strengthens the match: that candidate's unwind/destructor
pointer refers to shim RVA `0xFBD0`, whose first four bytes are `8D 41 04 C7`.
Decoded as a signed little-endian 32-bit integer, they are **-956022387**, exactly
the value our broken type reader logged after following ThrowInfo+4.

The parser self-check asserted that the independently decoded known shim ThrowInfo
at RVA `0xC8018` resolves to the expected four types. The fingerprint equality was
also asserted. Results and image SHA256 values are preserved in
`throwinfo-candidates.json` and `throwinfo-fingerprint.json`.

**Conclusion:** strong static corroboration that the new fullscreen throw uses the
shim's std::system_error metadata, like the earlier game trace. This is not a
captured dynamic module map, call stack, mutex owner state or proof of reentrancy.
Other loaded/system modules were not scanned. Do not upgrade this to a confirmed
same-callsite reproduction.

## Source review: what is actually held across callbacks

Pinned source: release commit `0a6cd5b1ee814ec390c358e0c9c32ece8895ddc0` under
`analysis/shim-symbols-1.6.1/source/bridge/src/client/`.

- `d3d9_lss.cpp:128–129`: global mutex precedes the global map definition.
  No runtime construction/destruction evidence was collected for these globals.
- `d3d9_device.cpp:3804–3815`: swapchain reset is called BEFORE the map-update
  lock. Do not attribute that reset's SetWindowPos to this locked region.
- `d3d9_swapchain.cpp:64–75`: monitor restoration is BEFORE the destructor's
  map-removal lock, not inside it.
- `window.cpp:155–191`: lifecycle processing holds the scoped map lock through
  the fullscreen activation SetWindowPos call (172–174), activation flag update
  (175), and deactivation's ShowWindowAsync request (178–179).
- `window.cpp:293–294`: WM_SIZE, WM_ACTIVATEAPP and WM_DESTROY enter windowMsg
  before dispatch to the application's WndProc. The previous app-callback nesting
  probe therefore tested a different lock lifetime.

This makes the internal activation/resize route more specific than a broad claim
that every fullscreen/window API call occurs under the mutex.

## Competing hypotheses and distinguishing evidence

1. **Same-thread nested acquisition inside fullscreen window management.**
   A queued activation after resize/deactivation could reach SetWindowPos while
   the lock is held, generating a nested lifecycle callback. The latest log's
   last successful explicit send was WM_ACTIVATEAPP(FALSE), not a throw inside
   that send; the failing queue callback's actual message is unrecorded.
   Needed: outer/nested message sequence, thread ID, and owner/count snapshot
   or a stack covering the locked caller and nested acquisition.
2. **A different std::system_error callsite in the same shim.**
   Matching exception metadata identifies a type, not the site selecting it.
   Needed: original exception context/stack or a diagnostic at the relevant
   lock/error helper, without swallowing or suppressing the exception.
3. **Mutex lifetime/state corruption rather than ordinary recursion.**
   The static lock routine's count/owner checks can also fail with corrupted
   state; no live state was captured. Needed: matched-image global address,
   current thread versus owner, count/type, and evidence of initialization/
   teardown timing. An unexpected owner/count is evidence to investigate,
   not permission to replace the mutex type.

No source patch, game launch, GPU test, installed-file change, or system change
was performed for this review. These are ordinary static PE/source observations,
not REA Evidence IDs; no compatible configured native REA provider was available
at the earlier check, and no additional provider setup was attempted.
