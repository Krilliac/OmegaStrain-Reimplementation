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
validation. The package has exactly this layout:

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

The adjacent `.zip.sha256` file records the archive's SHA-256 digest for integrity checks. This preview is not code-signed
and may trigger Windows reputation warnings. It is an early native clean-room runtime, not a claim
of retail behavior or PCSX2 equivalence.

The archive contains only project source-derived binaries, project documentation, and license
texts. It contains no disc image, retail executable, extracted proprietary asset, emulator,
PCSX2 component, private path, save, profile, credential, or other owner-only input.
