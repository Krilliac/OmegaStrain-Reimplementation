# Executable loader hints

## Confirmed

- `SCUS_972.64` is an ELF32 little-endian MIPS executable, 4,071,592 bytes, entry point
  `0x00100008`, with five program headers and eleven sections.
- PCSX2 identifies the disc as serial `SCUS-97264`, CRC `D5605611`, and reaches VM
  initialization with the USA v1.60 BIOS.
- The ASCII token `MINSK` occurs in the executable at file offset `0x003ACBC8`, mapped through
  the main load segment to virtual address `0x004ACAC8`.
- The token sits with other level identifiers, while `GAMEDATA/MINSK` is present on disc.
  This earlier evidence made a direct-level loader plausible but, by itself, did not establish
  its command-line syntax.

## Follow-up static trace

The argument parser and file-resolution chain are now mapped in
[`argument-loader.md`](argument-loader.md). Static analysis confirms that the parser recognizes
`-x`, consumes an attached payload for `-l`, and carries `MINSK` into the level-specific
`LOADING.HOG` lookup. This establishes syntax and loader intent, but not the eventual gameplay
state.

## Optional reference-game experiment

1. Launch PCSX2 with its debugger and break at `0x00100008`.
2. Compare a normal boot with `-gameargs "-x -lMINSK"` using
   `scripts/launch-omega.ps1 -Debugger -GameArgs '-x -lMINSK'`.
3. Use the provenance-only observation points in `argument-loader.md` to trace the argument
   vector and level-specific file lookup.
4. Record the first `GAMEDATA/MINSK` request.
5. Preserve screenshots/logs and convert observations into a repeatable test before treating
   the option as supported.
