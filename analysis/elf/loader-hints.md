# Executable loader hints

## Confirmed

- `SCUS_972.64` is an ELF32 little-endian MIPS executable, 4,071,592 bytes, entry point
  `0x00100008`, with five program headers and eleven sections.
- PCSX2 identifies the disc as serial `SCUS-97264`, CRC `D5605611`, and reaches VM
  initialization with the USA v1.60 BIOS.
- The ASCII token `MINSK` occurs in the executable at file offset `0x003ACBC8`, mapped through
  the main load segment to virtual address `0x004ACAC8`.
- The token sits with other level identifiers, while `GAMEDATA/MINSK` is present on disc.
  This makes a direct-level loader plausible, but does not establish its command-line syntax.

## Not yet confirmed

- Static string searches did not establish literal `-x` or `-lMINSK` options.
- The handoff claim that `-x -lMINSK` directly loads Minsk remains a hypothesis. The parser may
  consume individual option characters or construct the level argument dynamically.
- No loader function, file-open breakpoint, or call graph has yet been labeled.

## Next experiment

1. Launch PCSX2 with its debugger and break at `0x00100008`.
2. Compare a normal boot with `-gameargs "-x -lMINSK"` using
   `scripts/launch-omega.ps1 -Debugger -GameArgs '-x -lMINSK'`.
3. Trace reads of the argument vector and references to `0x004ACAC8`.
4. Break on the game's file-open path and record the first `GAMEDATA/MINSK` request.
5. Preserve screenshots/logs and convert observations into a repeatable test before treating
   the option as supported.
