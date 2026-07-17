# Startup arguments and level-loader contract

## Scope

This note records a clean-room, metadata-only trace of the private reference
executable `SCUS_972.64`. It contains no instruction bytes, copied routines, or
decompiled source. All PS2 virtual addresses below are provenance for observed
behavior only. They are **not** implementation addresses and must not enter the
native x86-64 runtime.

Reference image metadata:

- ELF32, little-endian MIPS, entry point `0x00100008`
- SHA-256 `9924da91767c8145411f37fa6c14c9d77208264c17f1ce9ee157d51abdd31dc6`
- main load segment maps file offset `0x00000100` to VA `0x00100000`
- global-pointer value `0x004E9B70`
- analyzed with radare2 6.1.8 and a small metadata scanner

Ghidra 12.1.2 was also tested, but its stock MIPS language does not fully decode
the R5900 multimedia register-save instructions. The argument and loader
findings below were therefore checked against radare2's instruction boundaries
and the ELF bytes directly rather than relying on Ghidra decompilation.

## Result

The earlier `-x -lMINSK` claim is **statically confirmed as valid reference-game
syntax**:

- the executable recognizes both option letters;
- `-l` consumes the text attached to the same argument, so `-lMINSK` is the
  correct form and `-l MINSK` is not equivalent;
- the selected level name is copied into a 128-byte global buffer;
- `MINSK` is level-name table entry 9 (zero-based) in a 17-entry table; and
- the loader subsequently constructs the logical path
  `GAMEDATA/<selected-level>/LOADING.HOG`, with a common-data fallback.

The two options are independent. `-lMINSK` selects the level without consulting
the flag set by `-x`. The latter selects an alternate startup-presentation path:
its two splash-related readers choose a widget-backed splash implementation
instead of the simpler timed splash, while its remaining direct reader changes
one first-character construction decision during `DATA.HOG` parsing. This is
enough to reject "level-load enable" and "skip intro" as descriptions of `-x`,
but not enough to recover the developers' original name for the mode.

This confirms argument and loader behavior, not the eventual gameplay result.
No emulator was launched during this analysis, so reaching a playable Minsk
state remains a separate behavioral check.

## Native behavioral contract

The native reimplementation should preserve these observable rules without
carrying over any PS2 code or execution model:

1. Accept the compatibility form `-l<level-name>` with the payload attached.
2. Treat the compatibility option letter case-sensitively unless later runtime
   evidence proves otherwise.
3. Bound the selected level name to the reference buffer capacity of 128 bytes,
   including termination.
4. Recognize `-x` as a distinct hidden alternate-startup switch. A neutral name
   such as `alternate_startup_presentation` is appropriate; do not expose it as
   "skip intro" or "direct level" because the static evidence contradicts those
   stronger labels.
5. Resolve level assets through platform-native path APIs using the logical path
   `GAMEDATA/<level>/LOADING.HOG`, then try
   `GAMEDATA/COMMON/LOADING.HOG` when the level-specific file is unavailable.
6. Keep ISO9660 decoration (leading separator, uppercase normalization, and
   `;1`) confined to original-disc analysis. Native filesystems must not require
   or synthesize that suffix.

A modern long-form alias such as `--level MINSK` may be added for usability, but
it should be an alias rather than a replacement for the observed compatibility
syntax.

## Argument handoff and parser

| Evidence VA | Observation |
| --- | --- |
| `0x001001FC` | CRT startup begins loading the argument block at `0x00533400`. |
| `0x00100204` | `argc` is read from `0x00533400`. |
| `0x00100208` | Startup calls the argument-processing entry at `0x0013D080`. |
| `0x0010020C` | The call delay slot supplies `argv` at `0x00533404`. |
| `0x0013D168` | The loop fetches the current argument, beginning with `argv[1]`. |
| `0x0013D16C` | The first character is required to be `-`. |
| `0x0013D17C` | The parser reads the single option letter from character 1. |
| `0x0013D188` | The attached payload pointer is set to character 2. |

The switch recognizes `x`, `p`, `a`, `n`, `h`, `s`, `r`, `l`, `f`, and `g`.
Only the two options relevant to the startup hypothesis were characterized here.
The embedded user help documents `-l` as the level-name option; `-x` is accepted
by the parser but omitted from that help.

### `-l` handler

The handler begins at `0x0013D230`:

- `0x0013D23C` copies the attached payload into the 128-byte selected-level
  buffer at `0x004FFC90`;
- `0x0013D250` forwards the same name to the startup/game object; and
- the handler emits a level-name diagnostic before returning to the option loop.

The `arg + 2` payload source is what proves that the space-separated form is not
the same operation.

### `-x` handler

The handler begins at `0x0013D408`. It sets a group of startup flags, including
bytes at `0x004FFBA0`, `0x004FFBB0`, and `0x004FFBB2`. The last flag has three
identified readers at `0x00167EE8`, `0x001C1FB4`, and `0x002C7E00`.

The write to `0x004FFBB2` is deferred until the option loop has finished. An
adjacent `LUI` plus load/store scan over every file-backed instruction finds the
following direct accesses:

| Access VA | Operation | Established role |
| --- | --- | --- |
| `0x0013D484` | byte store | Commits the mode after argument parsing. |
| `0x00167EEC` | unsigned byte load | Selects the startup splash dispatch. |
| `0x001C1FB8` | unsigned byte load | Changes the first character-record construction path. |
| `0x002C7E04` | unsigned byte load | Guards the widget-backed splash implementation. |

This direct-reference inventory is exhaustive for adjacent absolute
load/store constructions in this executable. It does not claim that an
indirect access through the containing global object is impossible.

#### Splash behavior

At `0x00167EF0`, a clear mode byte selects a timed splash call. A set mode byte
first initializes the relevant GUI state and then enters the richer splash
controller. The controller repeats the guard at `0x002C7E08`: when asked to
enable the splash with the mode clear it falls back to the timed path; with the
mode set it constructs the named splash widget, attaches its wake behavior, and
uses the controller's normal enable/disable lifecycle.

This makes "alternate widget-backed splash mode" a high-confidence behavioral
description of the two presentation consumers. It does not establish why the
original option letter was `x`.

#### Character-loader behavior

The third reader is inside the character-data parser reached from the
`DATA.HOG` stage. It affects only the first record in that loop. When the mode
is clear, a selector is forced nonzero. When the mode is set, the selector is
preserved from a video/configuration byte at offset `0x250` of the startup game
object. Only the zero case takes the alternate construction path, which supplies
an existing fixed-size record to the character constructor; the other path
supplies no such record.

The record's original type and the developers' reason for coupling it to this
mode remain unresolved. Calling it player customization, multiplayer state, or
a platform mode would be speculation.

#### Confidence and remaining uncertainty

| Claim | Confidence | Basis |
| --- | --- | --- |
| `-x` is active and commits a persistent mode byte. | Confirmed | Handler store plus three direct readers. |
| It selects widget-backed rather than timed splash behavior. | High | Both presentation branches, widget construction, and lifecycle calls agree. |
| It is not required for `-l` level selection. | Confirmed | `-l` writes the level independently; the path-selection chain has no direct mode-byte read. |
| It changes one first-character construction branch. | Confirmed | Direct control-flow trace in the `DATA.HOG` character parser. |
| The original semantic name or intended end-user purpose. | Unresolved | No help entry, symbol, or diagnostic names the option. |

## `MINSK` reference enumeration

The token occurs at file offset `0x003ACBC8`, mapped to VA `0x004ACAC8`. The only
direct stored pointer to those characters is at `0x0048C184`. That pointer is
entry 9 of the 17-entry level-name pointer table beginning at `0x0048C160`:

`(0x0048C184 - 0x0048C160) / 4 = 9`

No direct code reference to the characters is expected because consumers index
the pointer table. Simple address-construction scanning found these table-base
consumers:

| Base construction | Base construction | Base construction |
| --- | --- | --- |
| `0x00294D04` / `0x00294D10` | `0x002952D8` / `0x002952E0` | `0x00295CE8` / `0x00295CF0` |
| `0x002970D8` / `0x002970DC` | `0x002AC5E8` / `0x002AC5F0` | `0x002CA8D4` / `0x002CA8D8` |
| `0x002CC84C` / `0x002CC854` | `0x002CC904` / `0x002CC908` | `0x002CC998` / `0x002CC9A8` |
| `0x0033E6A0` / `0x0033E6A8` | `0x0034F438` / `0x0034F43C` | `0x0034F964` / `0x0034F96C` |
| `0x00354BBC` / `0x00354BC0` | `0x00354FFC` / `0x00355000` | |

The consumer at `0x00294D04` is a useful sanity check: it bounds the index below
17, scales it by four, adds the `0x0048C160` base, and loads the selected pointer.
That establishes the table interpretation independently of string adjacency.

Nearby constructions of related data tables occur at `0x002C9B44`
(`0x0048C148`), `0x002C90A4` (`0x0048C200`), `0x002C9124`
(`0x0048C1B0`), and `0x002CA9E0` (`0x0048C100`). They are recorded to prevent
future scans from conflating neighboring tables with the level-name table.

## Level-loader and file-resolution chain

The selected-level buffer is consumed by the loader beginning at `0x00135420`:

| Evidence VA | Reference behavior |
| --- | --- |
| `0x00135420` | Begins selected-level loading with the `GAMEDATA` root and selected-level buffer. |
| `0x0013542C` | Establishes the selected game-data location. |
| `0x0013544C` | Formats the level-specific `LOADING.HOG` path. |
| `0x00135460` | Constructs the reference file abstraction for that path. |
| `0x00135468` | Resolves/opens the constructed path; failure selects the common fallback. |
| `0x0036DEF0` | Path-backed reference file resolver. |
| `0x0036D680` | Creates the disc-file backend. |
| `0x003696B0` | Ensures that the backend has an open descriptor. |
| `0x00369050` | Converts a logical path to the reference disc lookup form. |
| `0x003690FC` | Calls the low-level disc lookup with the final ISO9660 path. |
| `0x003A6DA0` | Low-level CD filesystem search/metadata boundary. |

At the last two stages, the reference backend has uppercased the path, added a
leading separator, and appended the ISO9660 version suffix. That transformation
describes the original disc backend only and is deliberately excluded from the
native contract above.

### Localization precedes the loading archive

The call at `0x0013542C` is more specific than a general game-data location
change. It builds and loads the level's localized string database before probing
`LOADING.HOG`. Logically, it resolves:

`GAMEDATA/<selected-level>/STRINGS<locale-suffix>.DAT`

The suffix comes from a seven-entry locale table selected by a global locale
index. The loader releases the preceding string database when present, creates
the localization object on first use, and then loads the newly constructed
path. Native code should model this as an explicit localization-resource change,
not as a process working-directory mutation.

### Loading-archive probe, fallback, and registration

The file-status routine at `0x0036DEF0` uses zero as success. At
`0x00135474`, any nonzero status replaces the level-specific candidate with
`GAMEDATA/COMMON/LOADING.HOG`; the common path is therefore a status-based
fallback, not merely a string default.

The selected path is handed to `0x00166430`, which probes it again. Only a zero
status proceeds to archive registration at `0x003F96E0`, registry lookup at
`0x003F9600`, and construction of the loading-screen resource at `0x002721B0`.
The registry de-duplicates archives by case-insensitive logical path and returns
the existing node when already mounted. A later stage removes matching archive
nodes through the corresponding registry-removal path.

### Main level-data archive

The level-data stage beginning at `0x001C4190` constructs and registers:

`GAMEDATA/<selected-level>/DATA.HOG`

It then creates a reader over the registered archive, hands that reader through
thirteen ordered parser calls, and destroys the reader after the final pass.
The calls are distinct subsystem boundaries; their order is observable and must
be retained in future native import tests even though the native implementation
will not execute any retail code.

The `-x` character-reader described above is reached inside one of these parser
passes. It does not choose or mount `DATA.HOG` itself.

### Weapon and character-asset archive order

When the parsed level declares at least one weapon dependency, the subsequent
weapon stage registers archives in this order:

1. `GAMEDATA/<selected-level>/WPN.HOG`
2. `GAMEDATA/COMMON/WPNHOG.HOG`
3. `GAMEDATA/<selected-level>/NPCTEX.HOG`
4. `GAMEDATA/<selected-level>/NPCSKM.HOG`

The per-level archives are checked for a usable archive payload before being
forwarded to their subsystem consumers. This is registration order, not VFS
override policy; precedence still needs an independent behavioral test before
the native VFS assigns duplicate-name priority.

Other useful stage markers are `0x001C41B0`, which begins the level-data loading
status stage, and `0x001457F8`, which begins level-specific weapon-data loading.

## Optional original-game observation points

These are debugger locations for validating the reference behavior in an
emulator. They are not native-runtime breakpoints or implementation guidance.
No such session was started for this report.

| Breakpoint | Inspect | Expected observation |
| --- | --- | --- |
| `0x0013D168` | `a1` / current argument pointer | Parser visits `-x` and `-lMINSK`. |
| `0x0013D230` | `s1` | Attached level payload points to `MINSK`. |
| `0x0013D408` | startup flags | Hidden startup mode is enabled. |
| `0x00135420` | memory at `0x004FFC90` | Selected level is `MINSK`. |
| `0x0013544C` | format arguments / result buffer | Level-specific loading archive path is constructed. |
| `0x00135468` | file object and path | Level-specific open/resolve is attempted. |
| `0x003690FC` | `a1` | Final ISO9660 lookup path is visible. |
| `0x003A6DA0` | `a1` | Same path enters the CD search boundary. |

## Reproducing the metadata scan

From the repository root:

```powershell
python -B tools\mips_static_refs.py `
  private\extracted-disc\SCUS_972.64 `
  --range 0x0048c100:0x0048c240 `
  --target 0x004acac8 `
  --gp 0x004e9b70 `
  --call-target 0x003a6da0
```

The helper reports virtual addresses and decoded instruction fields only. Its
LUI-pair scan is intentionally simple, so every address used as evidence above
was manually checked at the corresponding instruction boundary.

The bounded startup/loader verifier reproduces the newer findings and fails
closed unless the executable hash, direct mode-byte accesses, call targets,
branch targets, parser order, and path-template fingerprints all match:

```powershell
python -B tools\trace_startup_loader.py `
  private\extracted-disc\SCUS_972.64
```

Its JSON output uses only the input basename. It emits no executable bytes,
decompiled source, retail string contents, or private parent path. The report
was also independently checked against radare2 6.1.8 disassembly with instruction
bytes hidden; PCSX2 was not launched.
