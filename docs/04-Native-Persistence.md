# Native persistence

## Direction

OpenOmega persists native game state. It does not emulate PS2 RAM, a PS2 memory-card device, or an
emulator savestate. PCSX2 remains a private behavioral oracle only. Compatibility with PS2 tools is
an import/export concern at the edge of the application, not the runtime storage model.

The implemented foundation is `omega::persistence::SaveDatabase`. It is a non-hot-reloadable
service intended for sole ownership by `OmegaApp`. The first slice is deliberately independent of
the runtime, content, retail-format, simulation, gameplay, app, SDL, and PCSX2 layers. App ownership,
typed profile records, menu enumeration, and PS2 compatibility adapters follow as separate slices.

## Ownership and thread contract

- `SaveDatabase::Open` takes one absolute native directory and acquires an operating-system-held
  exclusive lock for the complete service lifetime.
- A second live owner receives the typed `busy` result. The database never attempts competing
  last-writer-wins commits across processes.
- One persistence/game thread externally serializes `Read`, `List`, and `Commit`. The service does
  not create a worker, dispatch callbacks, or hide blocking filesystem work.
- Returned records and list entries are owned copies. No internal map node, byte span, file handle,
  or path view escapes.
- Move construction transfers the lock and all state. A moved-from object fails closed as
  `invalid-state`.

## Logical model

The database is a bounded, ordered key/value store. Keys use lowercase ASCII letters, digits,
`_`, `-`, `.`, and `/`; leading or trailing separators, empty segments, `.` segments, and `..`
segments are rejected. Each record owns:

- a canonical key;
- a nonzero schema version;
- a nonzero monotonically increasing record revision; and
- opaque bytes bounded independently and in aggregate.

A commit contains one or more unique-key mutations and is validated as a whole against a private
copy. Conditions support unconditional write, must be absent, must exist, and exact nonzero
revision. Invalid keys, malformed conditions, duplicate keys, failed preconditions, exhausted
counters, or configured-limit failures publish nothing.

This layer intentionally does not assign campaign, profile, checkpoint, inventory, unlock, or
retail-payload semantics. Typed repositories above it will own those schemas.

## On-disk format version 1

The database directory contains an ordinary lock leaf and two complete snapshot slots:

```text
openomega-save.lock
openomega-save-a.oodb
openomega-save-b.oodb
```

Every snapshot is little-endian and begins with a fixed 64-byte header. The header contains the
eight-byte `OOSDB001` magic, format version, header size, endian marker, generation, record count,
payload size, payload CRC-32, header CRC-32, and zeroed reserved fields. The checksum calculation
zeroes the stored header-CRC field. Records are key-sorted and each begins with a fixed 32-byte
header containing key length, zero flags, schema version, revision, value length, value CRC-32, and
a zero reserved field, followed immediately by key bytes and value bytes.

The implementation checks every arithmetic edge before allocation or subspan creation. Defaults
bound records to 1,024, mutations per commit to 256, a key to 96 bytes, a value to 8 MiB, aggregate
logical values to 48 MiB, and one snapshot to 64 MiB. Separate hard ceilings prevent configuration
from exceeding 4,096 records/mutations, 255 key bytes, 16 MiB per value, 128 MiB logical bytes, or
256 MiB per file. These are project engineering limits, not retail or PS2 limits.

## Commit and recovery

At open, both slots are decoded independently. An unsupported version fails closed even when the
other slot is readable, preventing an older executable from silently rolling a newer database back.
Equal-generation slots must contain identical records. Otherwise the newest complete valid
generation wins; one corrupt or torn newer slot falls back to the prior complete generation. If no
slot is valid, open fails instead of manufacturing an empty database. A new directory receives two
checksummed generation-zero snapshots.

For a commit, the service:

1. copies the current ordered record map;
2. validates and applies the complete mutation batch to that copy;
3. encodes the next generation;
4. writes and flushes the inactive slot;
5. reopens, decodes, and compares that slot with the candidate; and
6. only then publishes the new active slot and in-memory generation.

The previously active complete slot is never modified by that commit. A torn inactive write
therefore cannot expose a partial new generation.

## PS2 compatibility boundary

PS2 compatibility will be implemented as stateless, bounded codecs plus a typed mapping layer:

```text
PS2 save container/image bytes
        |
        v
bounded standard-format codec
        |
        v
owned compatibility directory/files
        |
        v
typed OpenOmega import transaction -> SaveDatabase
```

The first compatibility target is an archival logical round trip:

- import one explicitly selected top-level save directory from an 8 MiB PS2 card image;
- accept either 512-byte logical pages or 528-byte raw pages;
- preserve the directory's ordered child names, full mode/attribute values, timestamps, and exact
  opaque file payloads;
- export only to a new 8 MiB raw `.ps2` image with 528-byte pages; and
- regenerate superblock, allocation tables/chains, physical placement, spare/ECC bytes, `.`/`..`,
  and unused entry tails rather than persisting emulator layout.

PS2SDK is the primary structural reference: its public headers define the 512-byte directory entry,
eight-byte timestamp, mode bits, 1,024-byte clusters, 256 FAT entries per FAT cluster, and superblock
geometry ([directory entry and timestamp](https://github.com/ps2dev/ps2sdk/blob/426de26cc7fe6d3d0e33d5704fa6815061ee9b27/common/include/libmc-common.h#L22-L67),
[mode bits](https://github.com/ps2dev/ps2sdk/blob/426de26cc7fe6d3d0e33d5704fa6815061ee9b27/common/include/libmc-common.h#L196-L215),
[filesystem geometry](https://github.com/ps2dev/ps2sdk/blob/426de26cc7fe6d3d0e33d5704fa6815061ee9b27/iop/memorycard/mcman/src/mcman-internal.h#L100-L115),
[superblock fields](https://github.com/ps2dev/ps2sdk/blob/426de26cc7fe6d3d0e33d5704fa6815061ee9b27/iop/memorycard/mcman/src/mcman-internal.h#L295-L313)).
PCSX2 is used only as the final compatibility oracle for its documented file-card convention: 512
data bytes plus 16 spare/ECC bytes per raw page, producing 8,650,752 raw bytes from an 8,388,608-byte
logical card ([layout](https://github.com/PCSX2/pcsx2/blob/30962c8d088793d847a5287d4ab5691a90187fce/pcsx2/SIO/Memcard/MemoryCardFile.cpp#L29-L33),
[conversion boundary](https://github.com/PCSX2/pcsx2/blob/30962c8d088793d847a5287d4ab5691a90187fce/pcsx2/SIO/Memcard/MemoryCardFile.cpp#L84-L150)).

Patching an existing card, folder cards, larger cards, and `.psu`, `.max`, `.cbs`, or `.psv`
packages are outside v1. More importantly, archival import/export does not turn arbitrary native
records into an Omega Strain retail save. That requires an independently evidenced retail-payload
codec for slot layout, checksums, and any compression or encryption.

Export follows the reverse path from a stable database snapshot. The codecs will not become
database backends, expose emulator state, execute retail code, or retain borrowed input. Standard
container/filesystem support and Omega Strain payload interpretation require separate evidence and
tests. Only synthetic fixtures may enter version control; owner saves and exported card images stay
outside it.

## Current validation boundary

The standalone synthetic suite covers fresh creation, deterministic sorted listing, transactional
put/update/delete, optimistic conditions, malformed conditions and keys, limits, move ownership,
exclusive live ownership, reopen persistence, torn-newest recovery, post-recovery commit, both-slot
corruption, and unsupported-future-version rejection. It uses no owner data, retail executable,
disc image, memory card, savestate, emulator, PCSX2 input, or D-drive input.

Serialized local validation passed warning-free runtime-disabled Debug, runtime-enabled Debug, and
runtime-enabled Release builds. Their complete CTest suites passed 29/29, 33/33, and 34/34,
respectively, including the Release portable-package contract. The focused database test also passed
20 consecutive Debug runs and 20 consecutive Release runs. The dependency gate checked 171 native
files, all 212 tooling tests passed, Python compile-all passed, and the staged public-tree gate
checked 262 indexed text blobs.
