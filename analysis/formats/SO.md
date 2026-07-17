# Omega Strain `.SO` script modules

## Result

The `.SO` files inside `GAMEDATA/*/SCRIPTS.HOG` are custom little-endian script-VM modules. They are not ELF shared objects: every tested file begins with the 32-bit value `8`, which points to its first bytecode cell, rather than the ELF magic `7F 45 4C 46`.

`tools/inspect_so.py` parses modules directly from `SCRIPTS.HOG` and emits only structural metadata. The recovered grammar consumed all 118 modules from all 19 level archives exactly to EOF:

- 9,275,184 serialized bytes
- 402,694 32-bit code cells
- 6,909 literal-pool entries
- 9,250 type records with 3,681 members
- 4,359 enum records with 174 locally declared values
- 24,928 global-symbol records, including 4,074 local definitions
- 42,804 inline global-initializer cells
- 79,845 callable records, including 4,950 local definitions
- 140,322 callable parameter-type references
- 0 parse errors and 0 trailing bytes

The deterministic evidence file is `analysis/formats/so-validation.json`. Re-running the inspector produces byte-identical JSON.

These cells are executable only in the retail game's custom VM. They are an
**offline research input**, not a runtime format: the shipping reimplementation
will never interpret, translate, recompile, or dispatch retail `.SO` cells.
Observed script behavior must be independently rewritten as native project code
or project-owned declarative mission data.

## Serialization grammar

All integers and VM cells are unsigned 32-bit little-endian values unless noted otherwise.

`LPString` is serialized as:

```text
u32 byte_length_including_NUL
u8  bytes[byte_length_including_NUL]
u8  zero_padding[to 4-byte boundary]
```

The top-level module layout is:

```text
u32 code_offset                       // confirmed 8 in all 118 modules
u32 code_cell_count
u32 code_cells[code_cell_count]

u32 literal_count
LPString literals[literal_count]      // payload may contain control bytes

u32 type_count
TypeRecord types[type_count]

u32 enum_count
EnumRecord enums[enum_count]

u32 globals_reserved                 // confirmed 0 in all modules
u32 global_count
GlobalRecord globals[global_count]

u32 callable_count
CallableRecord callables[callable_count]
EOF
```

The type record is:

```text
LPString name
u32 external_flag                    // observed values: 0 or 1
LPString base_type_name              // zero-length when absent
u32 member_count
repeat member_count:
    LPString member_name
    u32 member_type_id
```

The enum record is:

```text
LPString name
u32 external_flag                    // observed values: 0 or 1
if external_flag == 0:
    u32 value_count
    repeat value_count:
        LPString value_name
        u32 value
```

The global record is:

```text
LPString name
u32 field_0                          // likely type ID; not yet proven
u32 external_flag                    // observed values: 0 or 1
u32 ordinal                          // exactly equals table index
u32 initializer_cell_count
u32 initializer_cells[initializer_cell_count]
```

The callable record is:

```text
LPString name
u32 field_0                          // likely return-type ID
u32 external_flag                    // observed values: 0 or 1
u32 ordinal                          // exactly equals table index
u32 label_id
u32 entry_cell
u32 unknown_5
u32 unknown_6
u32 unknown_7
u32 unknown_8
u32 unknown_9
u32 parameter_count
u32 parameter_type_ids[parameter_count]
```

## Imports, definitions, and code entry points

The `external_flag` interpretation is strongly supported by independent invariants across every module:

- Flag `1` globals never carry initializer cells; flag `0` globals may carry them.
- Flag `1` callables always have `entry_cell == 0`.
- Flag `0` callables always have a nonzero entry and are strictly ordered by that entry.
- All 4,950 flag-`0` callable entries point inside the module's leading code array.

For every one of those 4,950 local callables, the cell immediately before `entry_cell` has this exact form:

```text
high16 == callable.label_id
low16  == 0x003B
```

This confirms that `entry_cell` is the executable entry after a `0x003B` label/prologue marker, and that callable `field_3` is the marker's identifier. It does not yet prove a human-readable name for opcode `0x003B`.

`field_0` occupies the same numeric ID domain used by member and parameter types. For local callables its only observed values are:

- `0x00000004`: 4,746 records
- `0x00000002`: 163 records
- `0x00000029`: 41 records

That makes a return-type tag the best current interpretation, but linking each numeric ID to a primitive or reflected class still requires tracing the runtime's type registry.

The five trailing callable fields are preserved but not named. Across all 79,845 records, their non-default (`!= 0` and `!= FFFFFFFF`) counts are `0, 1,624, 18, 956, 381`. Values in fields 6, 8, and 9 cluster on event-style handler records, so dispatch/binding metadata is a plausible interpretation; it remains unconfirmed.

## Shared versus module-local schema

Every module uses the same container grammar, but the reflected schema is a family rather than one byte-identical table:

- type count ranges from 78 to 80
- enum count ranges from 35 to 40
- at most one locally declared type appears in a module
- at most two locally declared enums appear in a module
- external-only type tables have 8 distinct hashes
- external-only enum tables have 14 distinct hashes

The variants are therefore real content/schema differences, not parser drift.
Full and external-only schema hashes are recorded per module so an offline
importer can group compatible evidence sets without exporting the tables
themselves.

## Offline research/import contract

An analysis-only importer can now be implemented without guessing offsets:

1. Read the leading code array and retain cell indices exactly.
2. Read the literal pool as indexed length-prefixed byte strings.
3. Catalog type and enum declarations by name and compare flag-`1` records with
   flag-`0` declarations across modules. Do not register them with the shipping
   engine as executable retail types.
4. Catalog globals in ordinal order. Retain initializer-cell offsets and counts
   as evidence only; never execute those cells.
5. Catalog callable signatures in ordinal order. Treat flag-`0`
   `entry_cell` values and their preceding `0x003B` markers as provenance for
   offline control-flow research, not as native function entry points.
6. Preserve callable fields 5 through 9 in research metadata until their
   behavioral meaning is established.
7. Recover opcode widths, stack effects, type IDs, imports, and observable
   behavior only as far as needed to write independent native gameplay code or
   project-owned declarative mission data.

Steps 1 through 6 are structurally determined. Step 7 is the remaining offline
semantic boundary: the cells mix instruction words and raw immediates
(including IEEE-754 values), so a naive low-16-bit histogram is not a valid
disassembler. None of these steps authorizes a retail VM in the shipping
runtime.

## Confidence ledger

Confirmed:

- non-ELF custom format
- exact section order and all record boundaries
- length-prefix/NUL/padding rule
- 32-bit little-endian code and initializer cells
- ordinal fields for globals and callables
- external/local correlation of flag `1`/`0`
- local function entry-cell behavior
- `0x003B` marker and label-ID relationship
- exact parsing of all 118 modules

Inferred with strong evidence:

- global `field_0` is a type ID
- callable `field_0` is a return-type ID
- flag `1` means imported/external and flag `0` means locally defined; whether every local definition is externally exported is not yet proven

Unknown:

- VM opcode names, widths, and stack effects beyond the entry-marker invariant
- numeric type-ID meanings
- callable fields 5 through 9
- import-resolution precedence across simultaneously loaded modules
- whether global initializer cells use precisely the same execution entry convention as callable code

## Reproduction

From the repository root:

```powershell
python -B .\tools\inspect_so.py .\private\extracted-disc `
  --json .\analysis\formats\so-validation.json
```

The command reads proprietary files in place and writes metadata only. It never extracts or copies a `.SO` payload.
