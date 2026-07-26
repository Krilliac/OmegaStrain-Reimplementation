# Level-name pointer table

## Scope

This note records a clean-room, metadata-only extraction of one data table from
the private reference executable `SCUS_972.64`. It contains no instruction
bytes, copied routines, or decompiled source. PS2 virtual addresses are
provenance for observed data layout only; they are not implementation addresses
and do not enter the native x86-64 runtime.

The only reference content reproduced here is the eighteen level directory
names. Those are structural filesystem identifiers that already appear in this
repository's own `analysis/manifests/disc-files.jsonl`. Nothing else from the
image is reproduced.

This note extends `analysis/elf/argument-loader.md`, which established the
table's existence, its base, and one worked entry. It corrects that note's entry
count. See "Relationship to argument-loader.md" below.

Reference image metadata (unchanged from `argument-loader.md`):

- ELF32, little-endian MIPS, entry point `0x00100008`
- SHA-256 `9924da91767c8145411f37fa6c14c9d77208264c17f1ce9ee157d51abdd31dc6`
- single `PT_LOAD` maps file offset `0x00000100` to VA `0x00100000`, file size
  `0x3E1D80`

## Method

Deterministic pointer chasing, not inference. Three steps, each independently
repeatable:

1. Parse the ELF32 program headers to build the virtual-address to file-offset
   map. Only the single `PT_LOAD` segment is needed.
2. Read consecutive little-endian 32-bit words starting at VA `0x0048C160` with
   stride four. Continue past the previously assumed end of the table so that
   the terminator is observed rather than assumed.
3. Treat each non-zero word as a virtual address, map it back to a file offset,
   and read the NUL-terminated ASCII string stored there.

The scan was deliberately extended to indices `-8` through `25` so that the
table's boundaries were observed on both sides instead of being taken from the
earlier note.

Consumer bounds were then read directly. For each base-construction site
recorded in `argument-loader.md`, the surrounding instructions were decoded to
field level and the compare that guards the index register was identified. Only
decoded instruction fields were used; no instruction bytes were extracted.

Reproduce with the existing scanner for the base constructions:

```powershell
python -B tools\mips_static_refs.py `
  private\extracted-disc\SCUS_972.64 `
  --range 0x0048c100:0x0048c240 `
  --gp 0x004e9b70
```

The pointer chase itself is a direct read of the mapped segment: map
`0x0048C160 + 4*i` through the `PT_LOAD` translation, dereference, and read the
C string. No project tool is required beyond the ELF program-header parse that
`tools/inspect_ps2_elf.py` already performs.

## Result: eighteen entries, not seventeen

| Index | Entry VA | Name |
| --- | --- | --- |
| 0 | `0x0048C160` | `TORONTO1` |
| 1 | `0x0048C164` | `TORONTO2` |
| 2 | `0x0048C168` | `TORONTO3` |
| 3 | `0x0048C16C` | `ITALY` |
| 4 | `0x0048C170` | `BELARUS1` |
| 5 | `0x0048C174` | `BELARUS2` |
| 6 | `0x0048C178` | `KYRGSTAN` |
| 7 | `0x0048C17C` | `YEMEN1` |
| 8 | `0x0048C180` | `YEMEN2` |
| 9 | `0x0048C184` | `MINSK` |
| 10 | `0x0048C188` | `CHECHNYA` |
| 11 | `0x0048C18C` | `LORELEI` |
| 12 | `0x0048C190` | `TOKYO` |
| 13 | `0x0048C194` | `MYANMAR` |
| 14 | `0x0048C198` | `ZURICH` |
| 15 | `0x0048C19C` | `MNTNEGR1` |
| 16 | `0x0048C1A0` | `UKRAINE` |
| 17 | `0x0048C1A4` | `TRAINING` |

Index 9 is `MINSK` at `0x0048C184`, exactly reproducing the worked example in
`argument-loader.md`. That is the independent check that the base, the stride,
and the pointer interpretation are all correct.

Boundaries were observed, not assumed:

- The words at `0x0048C158` and `0x0048C15C`, immediately below the base, are
  zero. The nearest populated neighbor below is the separate table at
  `0x0048C148`, whose entries are unrelated theme names.
- The words at `0x0048C1A8` and `0x0048C1AC`, immediately above index 17, are
  zero. The next populated neighbor above is the separate table at
  `0x0048C1B0`, already flagged in `argument-loader.md` as not to be conflated
  with this one.

So the table's storage extent is exactly eighteen consecutive non-null pointers,
terminated by zero words on both sides.

## Validation against the owned disc

The owned disc's `GAMEDATA` directory contains twenty-one subdirectories. Three
are not level directories by name and content role: `COMMON`, `FRONTEND`, and
`NETWORK`. `COMMON` is already established in `argument-loader.md` as the
loader's fallback location rather than a level.

That leaves eighteen level directories. The correspondence with the decoded
table is exact and total:

- every one of the eighteen decoded names has a `GAMEDATA/<NAME>` directory;
- every one of the eighteen level directories appears in the table;
- the set difference in both directions is empty.

This correspondence is independently reproducible from the public tree alone,
without the disc: reducing the `path` fields of
`analysis/manifests/disc-files.jsonl` to their first two components yields
exactly twenty-one `GAMEDATA` subdirectories, and removing the three non-level
roles leaves a set equal to the eighteen names above.

All eighteen names therefore already appear in this repository's tracked
manifest, so recording them here introduces no reference content that the public
tree did not already carry.

## The seventeen-versus-eighteen discrepancy

This is the point the prior note got wrong, and it is worth stating precisely
because the resolution is the opposite of what a naive reconciliation would
produce.

`argument-loader.md` describes a "17-entry level-name pointer table". A
seventeen-entry table set against eighteen level directories would imply that
one on-disc directory is absent from the table. **That is not what is happening.
No directory is absent.** The table holds eighteen entries, and the eighteenth
is `TRAINING`.

The number seventeen is real, but it is a *consumer bound*, not the table's
size. The consumers disagree with each other. Decoding the compare that guards
the index register at each base-construction site recorded in
`argument-loader.md` gives:

| Consumer base construction | Guarding compare | Admitted indices |
| --- | --- | --- |
| `0x00294D04` | `0x00294CEC` compare against 17 | `[0, 17)` |
| `0x0034F964` | `0x0034F958` compare against 17 | `[0, 17)` |
| `0x00354BBC` | `0x00354BB0` compare against 17 | `[0, 17)` |
| `0x00354FFC` | `0x00354FEC` compare against 17 | `[0, 17)` |
| `0x002CC84C` | `0x002CC840` compare against 18 | `[0, 18)` |
| `0x002CC904` | `0x002CC93C` compare against 18 | `[0, 18)` |

In every row the compared register is the same register that is then shifted
left by two, added to the base, and used as the load address, so the bound and
the index are provably the same value. In every row the base is constructed as
the same `LUI 0x0049` / `ADDIU -16032` pair, and `0x00490000 - 16032` is
`0x0048C160`, the table base. The site at `0x002CC904` is a loop that increments
its index and continues while it remains below 18, stepping a companion pointer
by four per iteration; it therefore walks all eighteen entries.

The prior note's seventeen came from `0x00294D04`, which it explicitly cites as
the sanity check that "bounds the index below 17". That reading of that one
consumer was correct. Generalizing it to the table's size was not.

So the discrepancy is fully resolved, and resolved without discarding anything:

- eighteen level directories on disc,
- eighteen entries in the table,
- `TRAINING` is the entry that four of the six inspected consumers cannot
  reach, and the two that can do reach it.

### Why some consumers stop at seventeen: unresolved

`TRAINING` being both last in the table and excluded by the narrower bound is
strongly suggestive — a training level plausibly sits outside whatever
enumeration those four consumers perform. **That is an inference and is not
claimed here as fact.** Nothing decoded assigns `TRAINING`, or any other entry,
a category such as campaign, bonus, training, or multiplayer. The functions
containing these consumers were not identified, and the image carries no symbols
that would name them. What is established is only that two different bounds
exist over one table and which entry the difference selects.

## Ordering is not presentation order

The table's order is a fact about this table. It is **not** established to be
the mission-presentation order used by the Command Center or any other menu. The
argument loader's use of the table is not the menu's use, and no menu consumer
was traced here.

There is positive reason for caution rather than merely absent evidence. Widely
circulated external descriptions of the game's mission list place the training
level *first*. This table places `TRAINING` *last*, with the other seventeen
names in an order that is otherwise consistent with those external lists shifted
by one position. Those external descriptions are not disc-derived evidence and
are deliberately not reproduced or relied upon here; they are noted only because
they point the same way the internal evidence does — namely that table index and
presentation position are different things and must not be equated.

Anything that needs a presentation order must derive it from a traced menu
consumer, not from this table.

## What remains unproven

- That table order equals mission-presentation order, unlock order, or any
  chapter grouping. Explicitly unproven, per the section above.
- Why four consumers bound at 17 and two at 18.
- Any per-entry classification (campaign, bonus, training, multiplayer).
- The identity or purpose of the functions containing the six consumers.
- That this is the only level enumeration in the image. The neighboring tables
  at `0x0048C100`, `0x0048C148`, `0x0048C1B0`, and `0x0048C200` were not
  decoded beyond confirming that they are separate from this one.
- How the reference title's own runtime reacts to a level name that is not in
  the table. Nothing here was executed, so the validation rule described below
  is this project's boundary policy, not an observed retail behavior.
- Any behavioral result. No emulator was launched for this analysis.

## Native consumption

The decoded table is carried into the native tree as a constant in
`native/include/omega/content/retail_level_table.h` and
`native/src/content/retail_level_table.cpp`. It exposes all eighteen names in
table order, index/name lookups in both directions, the count, and the narrower
consumer bound as a separately named constant so the two numbers cannot be
confused again.

`omega::runtime::IsValidContentLevelCode` now validates `content.level_code`
and `--level` against that table rather than accepting any 1-to-32-byte ASCII
alphanumeric string. A mistyped level therefore fails at the argument or
configuration boundary with a fixed diagnostic that echoes no input, instead of
failing late inside content I/O as a missing directory.

## Provenance of this note

The pointer chase, the boundary scan, the consumer-bound decoding, and the
`GAMEDATA` directory listing were performed against the private reference image
and the owned disc in the originating analysis session. This note and the native
table are a hand-port of that recorded result; the port did not reopen either
private input. The disc-correspondence claim above was independently re-checked
in the port from the tracked public manifest alone, as described in that
section. The remaining reference-image facts — entry addresses, zero-word
boundaries, and the six consumer bounds — carry the provenance of the original
extraction and are reproducible with the method above by anyone holding the
image.
