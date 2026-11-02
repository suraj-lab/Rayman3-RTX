# Desktop native-analysis prerequisites — read-only SSH check

User proposed using their desktop over SSH. Connection to `suraj@arch` succeeded
using the existing host-key trust and noninteractive authentication. No credentials,
SSH configuration or host-key files were changed.

Verified:

- Host: Linux x86_64 (CachyOS/Arch).
- Installed package: `ghidra 12.1.2-2.1`.
- Ghidra root: `/opt/ghidra`.
- Headless launcher: `/opt/ghidra/support/analyzeHeadless`.
- Native decompiler: `/opt/ghidra/Ghidra/Features/Decompiler/os/linux_x86_64/decompile`.
- Ghidra application properties: version 12.1.2, Java minimum/compiler 21.
- Default installed JVM: OpenJDK 21.0.12.1; meets that package's declared minimum.
- Node: v22.23.4.
- `GHIDRA_INSTALL_DIR` is unset in the inspected SSH environment.
- No `rea` executable found on PATH or the checked common user/global launcher paths;
  no global rea-agents npm package was listed.
- Existing clean source checkout: `/home/suraj/Projects/rea`, package `rea-agents`
  version 6.5.0, public launcher `scripts/rea.mjs`. `node_modules` and `dist` absent;
  it is not a verified ready-to-run CLI installation.

One initial inspection exited early because the remote login shell was zsh and an
unmatched glob caused `no matches found`. The follow-up explicitly used `sh -lc`
and completed. This was not an SSH connectivity failure.

No installation, build, persistent environment change, tool registration, binary
transfer, game launch, analysis project creation, service or remote file mutation
was performed. Ghidra's presence is prerequisite evidence, not a completed REA
readiness/decompilation test.

## Correction: existing desktop MCP registration and cached package verified

The user pointed out that desktop REA MCP was already configured. Inspecting only
its non-secret launcher/analysis fields in `~/.pi/agent/mcp.json` found:

- Registration: `npx -y rea-agents@6.4.0 mcp`.
- Registered analysis environment: `GHIDRA_INSTALL_DIR=/opt/ghidra`,
  `JAVA_HOME=/usr/lib/jvm/java-21-openjdk`.
- Two npm-cache installations of version 6.4.0; their package manifests and public
  launcher scripts had identical respective SHA256 values.
- Reused public launcher:
  `/home/suraj/.npm/_npx/af752b90495482d7/node_modules/rea-agents/scripts/rea.mjs`.
- `node <launcher> --version` returned 6.4.0. No acquisition/build was needed.

An owned stdio MCP process was started over SSH using that existing public launcher,
initialized through JSON-RPC, and asked for its advertised `binary_session` tool.
The resulting session had no open target. The Ghidra provider reported **available**;
configuration, platform, x64 architecture, installation, compatible Ghidra 12.1.2,
headless launcher, ELF x86_64 native decompiler, and full 64-bit JDK 21 all passed.
The owned readiness process then exited normally (0). No unrelated MCP process
was stopped and neither machine's registration was changed.

**Readiness conclusion:** desktop REA/Ghidra can be used. The earlier PATH/global
package inspection was insufficient to establish MCP availability. The existing
6.5.0 source checkout does not need to be built or modified to use registered 6.4.0.
No Rayman workspace was found among the narrowly filtered desktop Projects names;
no mod binary has yet been transferred or opened there.

Next scope: transfer selected hashed artifact copies into a new isolated desktop
analysis directory and perform static REA analysis. No software installation,
global setting, game execution or laptop MCP/auth change is needed.
