# Native persistence

## Direction

OpenOmega persists native game state. It does not emulate PS2 RAM, a PS2 memory-card device, or an
emulator savestate. PCSX2 remains a private behavioral oracle only. Compatibility with PS2 tools is
an import/export concern at the edge of the application, not the runtime storage model.

The bottom-level foundation is `omega::persistence::SaveDatabase`. `omega::profiles::ProfileCatalog`
adds the first typed native schema, and the app-level `NativePersistence` service composes both into
`OmegaApp`. These are non-hot-reloadable services. The database and profile layers remain
independent of runtime, content, retail-format, simulation, gameplay, app, SDL, and PCSX2 code;
only the app composition root owns them. E-0096 adds app-session active-profile selection without
moving that ownership boundary. E-0109 makes the same explicit Profiles action confirm one owned ID
through a separate project-native durable pointer before session publication. Campaign records,
profile mutation UI, and PS2 compatibility adapters remain separate slices.

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
- `NativePersistence::Bootstrap` heap-owns the database at a stable address, constructs one
  `ProfileCatalog` that borrows it, validates all profile markers through one deterministic list,
  and then moves the complete owner into `OmegaApp`.
- `OmegaApp` declares native persistence before its other services, so reverse member destruction
  destroys all app consumers before the catalog and then destroys the catalog before its database.
- Before SDL startup, `OmegaApp` copies at most the three displayed `ProfileId` values and fixed
  labels into `FrontEndStartupModel`. Frame-time confirmation resolves only a typed bounded model
  slot, asks its owned `NativePersistence` service to confirm the copied ID, and publishes the
  projected front-end state and optional app-session value only after that operation succeeds. It
  retains no catalog view. Failure retains the prior Profiles, session, database, and GPU state.
- Bootstrap owns the optional decoded `profiles/active` value and its observed record revision. It
  validates that ID against the complete already-validated startup catalog but never copies it into
  the new app session. A same-ID explicit reconfirm validates the current durable revision and
  performs no write.
- Bootstrap and every subsequent catalog/database operation run on the externally serialized
  persistence/game thread. Neither layer creates a worker or performs hidden asynchronous I/O.
- Capture retains only the existing bounded front-end command. Replay performs no persistence,
  catalog lookup, profile-ID publication, or GPU-resource operation.

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

The database layer intentionally assigns no campaign, profile, checkpoint, inventory, unlock, or
retail-payload semantics. The first repository above it is `ProfileCatalog`. A profile identifier is
exactly 16 owned bytes rendered as 32 lowercase hexadecimal characters. Its sole marker key is
`profiles/<id>/metadata`; metadata format version 1 owns a bounded UTF-8 display name plus supplied
creation and modification timestamps. Listing is deterministic by identifier, ignores unrelated
records below `profiles/`, and never creates or selects an implicit default profile. Updates require
the caller's expected metadata revision, preserve creation time, and reject a backwards modification
time. A summary's metadata revision is the database generation that wrote that marker, so it is an
opaque optimistic-concurrency token rather than a profile-local consecutive counter.

The app-level repository also owns one optional confirmation record at `profiles/active`. Its
database record schema is 1 and its value is exactly 32 bytes:

```text
offset  bytes  meaning
0       8      ASCII OOACTPRF
8       2      little-endian payload version 1
10      2      little-endian zero flags
12      4      little-endian zero reserved word
16      16     raw ProfileId bytes
```

Bootstrap rejects a database read failure, unsupported record schema, wrong value length,
wrong magic, unsupported payload version, nonzero flags/reserved fields, or an otherwise well-formed
ID missing from the validated catalog through the typed `persisted-active-profile` startup category.
The pointer is therefore neither an ambient default nor permission to activate a session.

Campaign, checkpoint, inventory, unlock, equipment, and retail-payload records remain unassigned.

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

The implementation checks every arithmetic edge before allocation or subspan creation. Standalone
database defaults bound records to 1,024, mutations per commit to 256, a key to 96 bytes, a value to
8 MiB, aggregate logical values to 48 MiB, and one snapshot to 64 MiB. The production app overrides
only the record ceiling to 1,025. That capacity is sufficient for its bounded 1,024-profile startup
model plus `profiles/active` when no other record consumes the budget; it is not a reserved slot or
namespace quota, and confirmation reports a typed capacity failure if the database is already full.
Separate hard ceilings prevent configuration from exceeding 4,096 records/mutations, 255 key bytes,
16 MiB per value, 128 MiB logical bytes, or 256 MiB per file. These are project engineering limits,
not retail or PS2 limits.

## Commit and recovery

At open, both slots are decoded independently through no-follow handles anchored to the owned
database directory. An integrity-valid unsupported version fails closed even when the other slot is
readable, preventing an older executable from silently rolling a newer database back. Transient I/O,
limit, unexpected-file-type, and hard-link identity failures also fail closed; only a definitely torn
or checksum-corrupt slot may fall back to its complete sibling. Equal-generation slots must contain
identical records. Otherwise the newest complete valid generation wins. If no slot is valid, open
fails instead of manufacturing an empty database. A missing slot is accepted only beside an empty
generation-zero survivor, where it is rebuilt before open succeeds; a missing slot in any established
database fails closed instead of risking rollback. A new directory synchronizes every newly created
path component through its previously existing ancestor, then publishes each generation-zero slot
through a flushed private same-directory temporary and atomic replacement. A crash before the first
replacement leaves both official slots missing and initialization safely retryable; a crash afterward
leaves at least one complete slot.

For a commit, the service:

1. copies the current ordered record map;
2. validates and applies the complete mutation batch to that copy;
3. encodes the next generation;
4. creates, writes, and flushes a private same-directory temporary;
5. atomically replaces the inactive slot and synchronizes the directory entry; and
6. performs only non-allocating swaps to publish the new in-memory generation.

The previously active complete slot is never modified by that commit. A torn inactive write
therefore cannot expose a partial new generation, and a hard-linked inactive name cannot truncate
another file. If the OS reports failure after replacement but before directory synchronization can
be confirmed—or if the replacement call itself has an indeterminate outcome—the service returns an
I/O error and poisons that instance; the caller must destroy and reopen it to reconcile the only
authoritative state from disk.

The database directory is private application state and must not be concurrently mutated by hostile
code running as the same OS account. Newly created POSIX roots are owner-only, and existing POSIX
roots with group/other write permission are rejected. No-follow handles, single-link validation,
private unpredictable temporary names, and atomic replacement protect against accidental or stale
namespace hazards; they are not a same-account sandbox boundary.

## Project-owned active-profile confirmation

An existing Profiles Primary edge publishes the already-established `SetActiveProfile` command.
`OmegaApp` resolves its copied startup-model slot, and `NativePersistence::ConfirmActiveProfile`
first verifies that the ID still names an existing catalog profile. It then commits `profiles/active`
with `MustBeAbsent` when no pointer was observed or `ExactRevision` when replacing one. Only after
that operation succeeds does `OmegaApp` publish the reducer's projected Main/Profiles-row state and
copy the ID into its app-session active value. A same-ID reconfirmation re-reads the durable pointer,
requires the same decoded ID and observed revision, and returns without a commit.

Missing-profile, revision-conflict, capacity, storage, and resource-exhaustion failures are typed and
projected to bounded path/ID-free operational diagnostics. Because database commit is transactional
and reducer/session publication follows it, failure stays on the prior Profiles selection and
preserves published database generation, records and logical bytes, the prior session active ID, and
the exact GPU snapshot. The confirmation path allocates no texture, draw list, upload, or other GPU
resource. Commit/recovery's documented indeterminate durable-I/O rule remains unchanged.

A generated one-profile fixture owns exactly one 41-byte logical metadata value at generation 1.
First confirmation commits the 32-byte pointer as a second record at generation 2, for exactly 73
logical value bytes. Reopen validates and exposes that pointer to persistence diagnostics, but a new
`OmegaApp` still starts the project-owned front end at `Profiles / Profiles / First` with session
activation unset. The user must explicitly reconfirm; the same-ID no-write path leaves generation 2,
two records, and 73 logical bytes unchanged before session publication.

Capture and replay retain their existing schema. Capture observes the same bounded command; replay
reduces it without accessing the profile catalog, database, filesystem, clock, or GPU. Persistent
confirmation is deliberately a live composition-root responsibility rather than replay state.

## Native directory and startup contract

The executable captures environment roots once at the application boundary and passes them to the
pure `ResolveDefaultNativeSavePath` function. Empty and relative roots are ignored. The selected
directory is:

- `%LOCALAPPDATA%/OpenOmega/native-save` on Windows;
- `$HOME/Library/Application Support/OpenOmega/native-save` on macOS;
- `$XDG_DATA_HOME/openomega/native-save` when `XDG_DATA_HOME` is absolute on XDG hosts; or
- `$HOME/.local/share/openomega/native-save` as the XDG fallback.

The resolver performs no environment or filesystem access, canonicalization, normalization,
directory creation, or token expansion. When no absolute platform data root is available, startup
fails with the typed `native persistence [path-unavailable]` stage rather than falling back to the
working directory.

Startup validates command-line/configuration and content first. `--probe-only` returns after its
content probe and never resolves, creates, locks, or reads native persistence. Every non-probe run
then resolves the native directory, opens the database, and validates the profile catalog before
platform creation. When `profiles/active` exists, the same bootstrap validates its complete typed
value and requires its ID to exist in that catalog. It does not activate the new app session.
Consequently `--frames=0` intentionally creates a fresh pair of 64-byte
generation-zero snapshots plus the empty lock file, reports the deterministic profile count, and
returns only after this bootstrap. Reopening the same zero-frame run must preserve the complete
native-save manifest. No default profile is created.

## PS2 compatibility boundary

PS2 compatibility is split into implemented stateless bounded standard-format codecs and a future
typed Omega Strain payload mapping layer:

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

The implemented standard-container boundary provides an archival logical round trip:

- import one explicitly selected top-level save directory from an 8 MiB PS2 card image;
- accept either 512-byte logical pages or 528-byte raw pages;
- preserve the directory's ordered child names, full mode/attribute values, timestamps, and exact
  opaque file payloads;
- export only to a new deterministic 8 MiB logical image or raw `.ps2` image with 528-byte pages;
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

Export follows the reverse path from an owned compatibility-directory value. The codecs are not
database backends, expose no emulator state, execute no retail code, and retain no borrowed input.
Standard container/filesystem support is implemented; adapting a stable database snapshot to an
Omega Strain payload still requires separate evidence and tests. Only synthetic fixtures may enter
version control; owner saves and exported card images stay outside it.

## Current validation boundary

The standalone database synthetic suite covers fresh creation, deterministic sorted listing,
transactional put/update/delete, optimistic conditions, malformed conditions and keys, limits, move
ownership, exclusive live ownership, reopen persistence, torn-newest recovery, post-recovery commit,
both-slot corruption, and unsupported-future-version rejection. Profile tests cover identifier and
metadata validation, deterministic listing, unrelated-record filtering, optimistic updates, reopen,
corrupt/future markers, resource limits, and database-error mapping. App integration tests cover
fresh bootstrap without an implicit profile, stable borrowed ownership across moves, live-lock
rejection, reopen summaries, path failure, typed future-profile-schema failure, the exact generated
confirmation encoding and totals, bootstrap rejection of malformed/unsupported/stale pointers,
revision-checked same-ID no-write confirmation, explicit session reconfirmation after reopen, and
failure preservation of database/session/GPU state. Process,
portable-package, and fresh-runner contracts cover exact genesis, second-open stability, probe-only
non-mutation, zero-frame output, and unavailable-root diagnostics. E-0109 adds a non-installed
build-tree writer that prepares one deterministic profile and confirmation through this same typed
owner. The direct-process and Windows portable-package contracts launch the real shell twice at zero
frames, require `profiles=1` and empty stderr, preserve the exact generated native-save manifest and
neighboring working/package/extracted trees, and retain exact install/archive allowlists that exclude
the helper and all save state. These fixtures use no owner data, retail executable, disc image,
memory card, savestate, emulator, PCSX2 input, or D-drive input.

The E-0109 implementation and generated fixtures are present. Static validation passed 340 tooling
tests, Python compile-all, the 262-file native dependency gate, both 109-record ledger gates, the
439-blob staged public-tree gate, diff checks, and independent core/package reviews. Local C++
compilation/execution and publication CI remain pending and unclaimed. The next persistence boundary
is a separate bounded project-owned campaign/checkpoint policy; it must not reinterpret this
confirmation pointer as retail or PS2 save behavior.

The database foundation's serialized local validation passed warning-free runtime-disabled Debug,
runtime-enabled Debug, and runtime-enabled Release builds. Their complete CTest suites passed 29/29,
33/33, and 34/34, respectively, including the then-current Release portable-package contract. The
focused database test also passed 20 consecutive Debug runs and 20 consecutive Release runs. Those
counts predate the profile and app-composition slices; their newer executable tests must be reported
only from a build that contains those slices.

The separate synthetic compatibility suites cover strict logical/raw envelope recognition,
geometry and superblock rejection, canonical ECC conversion, bounded IFC/FAT and directory-chain
walking, loops and cluster reuse within the traversed root, selected-directory, and selected-file
chains, ordered opaque file recovery, deterministic fresh-card creation,
input/limit/capacity rejection, and logical/raw read-after-write round trips. They do not decode an
Omega Strain payload or validate a generated image against owner data or a live PCSX2 session.
Focused warning-free MSVC Debug and Release builds passed with CTest 3/3 and direct execution of all
three compatibility tests. The complete warning-free Debug integration build and CTest 40/40 also
passed; a complete Release integration build was not run for this hardening slice.
