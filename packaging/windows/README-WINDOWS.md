# OpenOmega Windows portable preview

This archive is an unsigned native Windows x86-64 development preview. Extract the complete
`OpenOmega-0.1.0-windows-x86_64` directory to a writable location before running it. The package
imports a Windows synchronization API-set available on Windows 8/Server 2012 and later; that API
availability is not an OpenOmega package-support guarantee. A main-push-only artifact consumer on a
separate fresh GitHub-hosted `windows-2022` runner passed exact-main push run `29714679947` at commit
`4868e1118bcd32c6713a7f4be57dd243d40996ed`; consumer job `88266108572` validated retained artifact
ID `8450186290` and emitted `fresh-VM portable consumer: OK (artifact, checksum, tree, launch,
immutability)`. That historical result predates native persistence. The current bounded package
contract instead requires first zero-frame startup to create an exact native-save genesis under the
isolated profile and a second startup to reopen it without further mutation. This is not general
clean-machine, interactive, owner-data, GPU, audio, or broader Windows-version compatibility
validation. Optional opening-movie playback requires Windows Media Foundation and an available
synchronous MPEG-2 decoder MFT. If that decoder capability is unavailable, OpenOmega skips the
optional opening movie and continues to the native menu. The package has exactly this layout:

```text
OpenOmega-0.1.0-windows-x86_64/
  openomega.exe
  launch-openomega.cmd
  README-WINDOWS.md
  LICENSE
  NOTICE
  TRADEMARKS.md
  THIRD_PARTY_NOTICES.md
  LICENSES/
    SDL3.txt
```

Double-click `launch-openomega.cmd`, or run it from a terminal with the same command-line options
accepted by `openomega.exe`. The launcher changes to the extracted directory, forwards every
argument unchanged, and returns the executable's exit code. A shortcut may target the launcher;
keep its **Start in** field empty because the launcher selects its own directory.

No installation, administrator access, registry entry, bundled profile, or bundled game data is
required. An optional per-user configuration file is read from
`%LOCALAPPDATA%\OpenOmega\openomega.cfg`. Every non-probe launch opens project-owned native
persistence at `%LOCALAPPDATA%\OpenOmega\native-save`; a fresh launch creates two checksummed
database snapshots and an empty process-lock file, but no default game profile. `--probe-only`
validates content and returns without touching native persistence. Owner-supplied data remains
outside this archive and can be selected with the documented `--data-root` and optional `--level`
arguments or configuration.

Interactive startup opens the project-generated Profiles screen. Durable profile confirmation
opens Characters, durable character confirmation opens BriefingRoom, and its mission row enters
DiagnosticPlay. The native Main screen remains available through cancel navigation with Start
Diagnostic, Profiles, Controls, and Asset Topology rows. Keyboard and mouse provide the complete
default route: use
W/A/S/D or the arrow keys to navigate and move; F1, Enter, keypad Enter, Space, or LMB confirms;
Space or LMB fires in DiagnosticPlay; Escape, Backspace, or RMB returns from a modal screen; T or
held RMB targets in DiagnosticPlay; and F10 quits. Target and fire cues follow the latest valid
normalized mouse position and use target center while no pointer sample is available. The SDL
gamepad subsystem is not initialized unless `--set=input.gamepad_enabled=true` explicitly opts into
optional button and D-pad aliases. The empty Profiles and Characters screens can create the fixed
project-owned `PROFILE 1` and `DIAGNOSTIC CHARACTER` records, persist them transactionally, and
confirm them in separate steps before entering BriefingRoom. No arbitrary naming, editing,
importing, exporting, or retail slot semantics are implemented. The screens and controls are
development-shell policy, not reproduced retail artwork, mouse behavior, combat behavior, or a
retail-parity claim.

The adjacent `.zip.sha256` file records the archive's SHA-256 digest for integrity checks. This preview is not code-signed
and may trigger Windows reputation warnings. It is an early native clean-room runtime, not a claim
of retail behavior or PCSX2 equivalence.

The archive contains only project source-derived binaries, project documentation, and license
texts. It contains no disc image, retail executable, extracted proprietary asset, emulator,
PCSX2 component, private path, save, profile, credential, or other owner-only input.
