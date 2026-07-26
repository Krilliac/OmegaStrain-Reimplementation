# omega_debug — live native-process inspection

Tools for inspecting the running `openomega.exe` (MSVC Debug build) at the
symbol level. gdb (MinGW) attaches but cannot read MSVC PDBs; use **cdb** from
the WinDbg app package instead — it reads the PDBs and scripts cleanly.

## cdb (WinDbg app package)

`cdbX64.exe` is on PATH via the WinDbg app-execution alias
(`%LOCALAPPDATA%\Microsoft\WindowsApps\cdbX64.exe`; the engine lives in the
`Microsoft.WinDbg_*` package under `C:\Program Files\WindowsApps`).

### Front-end compositor code-path trace

`frontend_trace.cdb` sets pattern breakpoints (`bm`) on the retail front-end
compositor entry points, each printing a `[HIT]` tag and auto-continuing
(`gc`), so a single retail launch prints how many times each path fires.

Run (Git Bash), driving a bounded 3-frame retail launch against the real disc:

```
CFG=/tmp/omega.cfg
printf 'content.data_root = D:/OmegaStrain-Reimplementation/private/extracted-disc\n' > "$CFG"
OPENOMEGA_DISABLE_STARTUP_DIALOG=1 OPENOMEGA_ENABLE_RETAIL_FRONT_END=1 \
  cdbX64.exe -c "\$\$><$(cygpath -w tools/omega_debug/frontend_trace.cdb)" \
  "D:\OmegaStrain-Reimplementation\build\msvc\products\game\Debug\openomega.exe" \
  "--config=$(cygpath -w "$CFG")" --frames=3 --developer-diagnostics
```

Notes:
- `--developer-diagnostics` is **required**. The decoded-data preview is arranged
  by project presentation policy, so it runs only under developer provenance;
  `OPENOMEGA_ENABLE_RETAIL_FRONT_END` alone does nothing on a default
  RetailRequired launch, which stays fail-closed. See docs/08 section 10.
- The exe **must** be an absolute Windows path (cdb `Win32 error 0n2` otherwise).
- The script file must have CRLF line endings for `$$><` to split commands.
- Symbols resolve from the PDBs next to the exe + `build/msvc/Debug`; no manual
  `.sympath` needed.

Observed on the real Title screen (Gap B Phase 1): `AppendVisualNodeTriangles`
×68 (nodes), `IsRasterizableTriangle` ×132 (source triangles),
`RasterizeRetailFrontEndTriangles` ×1, `BuildRetailFrontEndPresentationIfPossible`
×1 — matching the compositor's own diagnostics.

### Attach to an already-running instance (read-only)

```
cdbX64.exe -pn openomega.exe -c ".reload /f; ~*k; qd"
```
`qd` = quit **and detach**, leaving the process running.

## Value-level tracing

For actual field values (not just hit counts), prefer the built-in env-gated
compositor trace over cdb locals (optimized-Debug locals are often in
registers). Set `OPENOMEGA_FRONTEND_TRACE=1` to have the compositor log node/
triangle/texture/skip counts and the composed draw-list shape to stderr.
