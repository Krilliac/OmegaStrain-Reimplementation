# Native persistence

## Direction

OpenOmega persists native game state. It does not emulate PS2 RAM, a PS2 memory-card device, or an
emulator savestate. PCSX2 remains a private behavioral oracle only. Compatibility with PS2 tools is
an import/export concern at the edge of the application, not the runtime storage model.

The bottom-level foundation is `omega::persistence::SaveDatabase`. `omega::profiles::ProfileCatalog`
adds the first typed native schema, and `omega::profiles::CharacterCatalog` adds a minimal
profile-owned character identity and metadata schema. The app-level `NativePersistence` service
composes all three into `OmegaApp`. These are non-hot-reloadable services. The database and catalog
layers remain independent of runtime, content, retail-format, simulation, gameplay, app, SDL, and
PCSX2 code; only the app composition root owns them. E-0096 adds app-session active-profile selection
without moving that ownership boundary. E-0109 makes the same explicit Profiles action confirm one
owned ID through a separate project-native durable pointer before session publication. E-0111 adds
one profile-bound project diagnostic marker behind that same explicit session confirmation. The
character-session slice adds an explicit profile/character confirmation pair and a separate
character-owned diagnostic-session marker. General mutation UI, retail character customization,
retail campaign/save payloads, and PS2 compatibility adapters remain separate slices.

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
  `ProfileCatalog` and one stateless `CharacterCatalog` that borrow it, validates at most the shared
  1,024-profile front-end marker budget and, for each admitted profile, the shared 1,024-character
  marker budget, validates every recognized checkpoint-shaped child record against those catalogs,
  and then moves the complete owner into `OmegaApp`.
- `OmegaApp` declares native persistence before its other services, so reverse member destruction
  destroys all app consumers before both catalogs and then destroys both catalogs before their
  database.
- Before SDL startup, `OmegaApp` copies at most the three displayed `ProfileId` values and fixed
  labels into `FrontEndStartupModel`. Frame-time confirmation resolves only a typed bounded model
  slot, asks its owned `NativePersistence` service to confirm the copied ID, and publishes the
  projected front-end state and optional app-session value only after that operation succeeds. It
  retains no catalog view. Failure retains the prior Profiles, session, database, and GPU state.
- Profile selection obtains an owned, bounded character summary list for that explicit profile and
  prepares a candidate Characters model/presentation before durable confirmation. Success publishes
  those owned values and clears the per-launch active character; failure releases the candidates and
  retains the previous published state. No character-catalog view reaches the frame loop.
- Bootstrap owns the optional decoded `profiles/active` value and its observed record revision. It
  validates that ID against the complete already-validated startup catalog but never copies it into
  the new app session. A same-ID explicit reconfirm validates the current durable revision and
  performs no write.
- Bootstrap also owns the optional decoded `profiles/active-character` profile/character pair and
  its observed record revision. The pair is accepted only when its profile equals the validated
  `profiles/active` ID and its character still resolves through that profile's catalog. Reopen
  exposes the validated durable IDs for diagnostics, but never copies either into the new app
  session.
- Confirming a different profile atomically replaces `profiles/active` and erases an existing
  `profiles/active-character` record in one commit. Only after that commit succeeds does the owner
  clear its durable character snapshot. The new profile therefore requires another explicit
  character confirmation before a character-owned session can start.
- Bootstrap and every subsequent catalog/database operation run on the externally serialized
  persistence/game thread. Neither layer creates a worker or performs hidden asynchronous I/O.
- Start Diagnostic is capability-closed until the current launch owns a confirmed profile and a
  confirmed character that still resolve in their bounded models. `OmegaApp` applies its typed
  session-start command before publishing DiagnosticPlay. A closed row is inert rather than an
  operational error.
- Capture retains only bounded front-end commands. Replay may reduce the same typed transitions from
  caller-supplied capabilities and identity-free mirrors, but performs no persistence, catalog
  lookup, profile/character-ID publication, or GPU-resource operation.

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

`CharacterCatalog` is a stateless typed facade over that same borrowed database. Every operation
takes an explicit `ProfileId`, validates that the parent profile exists and is well formed, and then
addresses a `CharacterId` that is likewise exactly 16 owned bytes rendered as 32 lowercase
hexadecimal characters. No ambient active profile is retained by the catalog. Its sole marker key is
`profiles/<profile-id>/characters/<character-id>/metadata`; database record schema 1 owns this
bounded project format:

```text
offset  bytes  meaning
0       8      ASCII OOCHARMD
8       2      little-endian payload version 1
10      2      little-endian zero flags
12      4      little-endian display-name byte count
16      8      little-endian supplied creation UTC milliseconds
24      8      little-endian supplied modification UTC milliseconds
32      N      1..64 bytes of control-free UTF-8 display name
```

The display name must be valid scalar UTF-8 without ASCII or Unicode C1 controls. Both timestamps
must fit the project-supported UTC millisecond range through year 9999, and modification cannot
precede creation. Updates preserve creation time, reject a backwards modification time, and require
the exact observed metadata revision. Profile-scoped listing is deterministic by `CharacterId`,
ignores nested child records, and spends its caller-supplied marker budget before parsing a direct
marker ID or payload. A malformed admitted direct marker therefore fails closed; a marker beneath a
different profile is outside that enumeration. The API never creates an orphan character, but this
schema does not claim a retail character slot, body, appearance, class, skill, inventory, equipment,
progression, network identity, or customization model.

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

The app-level repository owns a second optional confirmation record at
`profiles/active-character`. Its database record schema is 1 and its value is exactly 48 bytes:

```text
offset  bytes  meaning
0       8      ASCII OOACTCHR
8       2      little-endian payload version 1
10      2      little-endian zero flags
12      4      little-endian zero reserved word
16      16     raw parent ProfileId bytes
32      16     raw CharacterId bytes
```

Bootstrap rejects a malformed value, a missing validated active profile, a profile-ID mismatch with
`profiles/active`, or a character absent from that profile through the exact
`persisted-active-character` category. It retains the observed pointer revision for later
optimistic validation. A durable pointer is integrity-checked reopen state, not an automatic app
session selection and not evidence of any retail active-character policy.

The app-level repository also owns one optional project diagnostic marker per profile at
`profiles/<id>/campaigns/diagnostic/checkpoint`. Its database record schema is 1 and its value is
exactly 32 bytes:

```text
offset  bytes  meaning
0       8      ASCII OODIAGCP
8       2      little-endian payload version 1
10      2      little-endian zero flags
12      4      little-endian zero reserved word
16      16     raw ProfileId bytes
```

Bootstrap rejects a checkpoint-shaped record with a malformed key ID, unsupported schema, wrong
length or scalar, key/value ID mismatch, or an ID absent from the validated startup catalog through
the fixed `persisted-diagnostic-checkpoint` category. The marker records only that this native
diagnostic launch boundary was prepared. Retail campaign, mission, continuation, save, world,
checkpoint, inventory, unlock, equipment, and gameplay payload semantics remain unassigned.

The character-session slice owns a separate marker at
`profiles/<profile-id>/characters/<character-id>/sessions/diagnostic/checkpoint`. Its database
record schema is 1 and its value is exactly 48 bytes:

```text
offset  bytes  meaning
0       8      ASCII OOGAMECP
8       2      little-endian payload version 1
10      2      little-endian zero flags
12      4      little-endian zero reserved word
16      16     raw parent ProfileId bytes
32      16     raw CharacterId bytes
```

Bootstrap recognizes the exact character-owned suffix, requires one canonical profile/character
path pair, decodes the complete value, requires key/value identity equality, and revalidates the
character through its parent catalog. Malformed, mismatched, or orphan markers fail through the
exact `persisted-game-session-checkpoint` category. The marker contains no level code, spawn,
position, transform, inventory, loadout, mission, world, or continuation state. Its name records only
that the project-generated diagnostic session boundary was prepared; it is not a retail checkpoint
or save schema.

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
8 MiB, aggregate logical values to 48 MiB, and one snapshot to 64 MiB. E-0109 initially overrode only
the production record ceiling to 1,025, and E-0111 superseded that ceiling with 2,049 records. The
character-session slice now configures production for the database's hard 4,096-record ceiling and
255-byte key ceiling. The canonical character-session marker key is 116 bytes and cannot fit the
standalone 96-byte default. These production values remain shared ceilings rather than reserved
slots or per-profile namespace quotas: no configuration reserves capacity for every possible
profile/character combination, and confirmation or session preparation reports a typed capacity
failure when unrelated accepted records consumed the available budget.
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
`OmegaApp` resolves its copied startup-model slot, lists that profile's bounded characters, and
prepares the owned character model/presentation. `NativePersistence::ConfirmActiveProfile` then
verifies that the ID still names an existing catalog profile and commits `profiles/active` with
`MustBeAbsent` when no pointer was observed or `ExactRevision` when replacing one. Only after that
operation succeeds does `OmegaApp` publish the reducer's projected Characters state, prepared
presentation, and per-launch profile value; it clears any per-launch active character. A same-ID
reconfirmation re-reads the durable pointer, requires the same decoded ID and observed revision, and
returns without a commit.

Missing-profile, revision-conflict, capacity, storage, and resource-exhaustion failures are typed and
projected to bounded path/ID-free operational diagnostics. Because database commit is transactional
and reducer/session publication follows it, failure stays on the prior Profiles selection and
preserves published database generation, records and logical bytes, the prior session active ID, and
the prior published presentation. Any candidate character presentation is released on failure.
`NativePersistence::ConfirmActiveProfile` itself allocates no GPU resource. Commit/recovery's
documented indeterminate durable-I/O rule remains unchanged.

A generated one-profile fixture owns exactly one 41-byte logical metadata value at generation 1.
First confirmation commits the 32-byte pointer as a second record at generation 2, for exactly 73
logical value bytes. Reopen validates and exposes that pointer to persistence diagnostics, but a new
`OmegaApp` still starts the project-owned front end at `Profiles / Profiles / First` with session
activation unset. The user must explicitly reconfirm; the same-ID no-write path leaves generation 2,
two records, and 73 logical bytes unchanged before session publication.

Capture and replay carry bounded logical commands only. Replay reduces them without accessing either
catalog, the database, filesystem, clock, or GPU. Durable confirmation is deliberately a live
composition-root responsibility rather than replay state.

## Project-owned active-character confirmation

After an explicit profile confirmation, the Characters Primary edge may publish a bounded
`SetActiveCharacter` command. `OmegaApp` resolves that slot against the selected profile's owned
startup model. `NativePersistence::ConfirmActiveCharacter` first requires the same durable active
profile, re-reads and decodes `profiles/active`, requires its exact observed revision, revalidates the
profile, and then revalidates the selected character through `CharacterCatalog`.

An absent character pointer is committed with `MustBeAbsent`; replacing a previously observed
pointer uses `ExactRevision`. Confirming the same profile/character pair re-reads the current pointer,
requires the same decoded IDs and observed revision, and succeeds without a write. Only after
confirmation succeeds does `OmegaApp` publish its per-launch active-character value. Creating a
character does not select it, and reopening validated durable confirmations does not activate either
identity in a new app session.

Confirming a different profile replaces `profiles/active` and erases the old character pointer in one
transaction. Missing active profile, missing parent profile, missing character, stale revision,
capacity, storage, and resource failures are typed and do not publish the projected app state. This
is a project-owned authorization and integrity boundary only. It assigns no retail character menu,
slot count, customization, save, account, roster, equipment, progression, or online semantics.

## Retained project-owned profile diagnostic checkpoint

The earlier profile-only diagnostic boundary remains a validated native schema and explicit
`PrepareDiagnosticCampaignStart` API. It is retained for its generated fixtures and compatibility;
the current production Start Diagnostic path uses the profile/character session boundary documented
below. The retained marker does not authorize or activate a session by its presence.

`PrepareDiagnosticCampaignStart` requires its ID to equal the owned confirmed ID, re-reads and
decodes `profiles/active`, requires the exact revision observed by confirmation/bootstrap, and
revalidates current profile existence. It then reads the profile-bound checkpoint. An existing
schema-1 marker with the same ID succeeds without a write even after unrelated database generations.
An absent marker is committed once with `MustBeAbsent`; a competing or malformed marker is a typed
revision conflict. Active-profile-required, missing-profile, revision, capacity, storage, and
resource failures map to fixed path-, key-, ID-, and byte-free categories.

Definite validation, precondition, and configured-limit failures commit nothing. The database's
existing indeterminate publication rule remains authoritative: an I/O failure after atomic
replacement may have published the marker, poisons the database instance, and requires
destroy/reopen reconciliation. Replay never creates or validates this marker. Its generated
profile-only fixture reaches generation 3 with exactly three records and 105 logical value bytes:
41-byte metadata, 32-byte active pointer, and 32-byte diagnostic marker. Repeating the same valid
preparation preserves those totals.

## Project-owned character session checkpoint

The current production mission-activation gate is exposed from the project-owned BriefingRoom after
explicit character confirmation. It requires both per-launch identities to resolve against their
bounded models. Its accepted command calls `PrepareGameSessionStart` with the owned profile and
character IDs before `OmegaApp` publishes DiagnosticPlay or permits simulation. Raw optional presence
does not authorize the transition. The briefing surface selects only the one content value already
bound at startup; it neither discovers nor persists a mission identity.

Preparation requires the same durable confirmed pair, re-reads and decodes both confirmation
pointers at their observed revisions, and revalidates both catalog records. It then reads the
character-owned marker. An existing schema-1 value with the same key/value IDs is a no-write success;
an absent marker is committed once with `MustBeAbsent`. A malformed or competing marker is a typed
revision conflict. Active-profile-required, active-character-required, missing-profile,
missing-character, revision, capacity, storage, and resource failures are fixed categories that
prevent app-state publication.

The generated lifecycle fixture commits one profile, one character, both explicit confirmation
pointers, and one session marker in five commits and five records. Repeating preparation preserves
the database generation. Reopen validates the profile, character, both durable pointers, and the
marker before exposing them; repeated preparation after reopen is likewise a no-write success. This
proves only the native transactional/session boundary. It does not establish retail save,
character-customization, campaign, checkpoint, mission/level catalog or selection, spawn,
simulation, world-state, continuation, networking, or behavioral-parity semantics.

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
then resolves the native directory, opens the database, validates at most 1,024 direct profile
markers, and validates at most 1,024 direct character markers beneath each admitted profile before
platform creation. Each catalog spends its budget before parsing a direct marker; checkpoint and
other nested child records do not consume those catalog budgets. Bootstrap next validates
`profiles/active`, then requires any `profiles/active-character` pair to match that profile and an
existing character. It scans recognized profile- and character-owned checkpoint records, requires
canonical key identities, complete typed values, key/value equality, and an existing owning catalog
record. Bootstrap retains validated durable identities but does not activate the new app session.
Consequently `--frames=0` intentionally creates a fresh pair of 64-byte generation-zero snapshots
plus the empty lock file, reports the deterministic profile count, and returns only after this
bootstrap. Reopening the same zero-frame run must preserve the complete native-save manifest. No
default profile or character is created.

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
corrupt/future markers, resource limits, and database-error mapping. Character-catalog tests cover
exact identifier parsing, parent ownership, cross-profile isolation, control-free UTF-8 and timestamp
validation, sorted bounded listing, budget-before-parse behavior, optimistic update, reopen,
corrupt/future character and parent markers, storage limits, and typed error mapping. App integration
tests cover fresh bootstrap without an implicit profile or character, stable borrowed ownership
across moves, live-lock rejection, reopen summaries, path failure, typed future-schema failure, the
exact generated confirmation encodings, bootstrap rejection of malformed/unsupported/stale pointers
and checkpoints, revision-checked no-write confirmation/preparation, explicit per-launch
reconfirmation, profile-switch invalidation of the character pointer, and failure preservation of
database/session/GPU state. Process,
portable-package, and fresh-runner contracts cover exact genesis, second-open stability, probe-only
non-mutation, zero-frame output, and unavailable-root diagnostics. E-0109 adds a non-installed
build-tree writer that prepares one deterministic profile and confirmation through this same typed
owner. The direct-process and Windows portable-package contracts launch the real shell twice at zero
frames, require `profiles=1` and empty stderr, preserve the exact generated native-save manifest and
neighboring working/package/extracted trees, and retain exact install/archive allowlists that exclude
the helper and all save state. These fixtures use no owner data, retail executable, disc image,
memory card, savestate, emulator, PCSX2 input, or D-drive input.

The integrated character-session tree passed a warning-free full MSVC Debug build and all 67/67
CTest cases locally. Static validation passed 361/361 tooling tests, Python compile-all, the 267-file
native-dependency gate, both 112-record ledger gates, and the staged public-tree gate over 445
indexed text blobs. These are local validation results for the
current worktree. Only the cited local Debug and static results are claimed here; no Release or
publication-CI result is claimed for this slice. All new catalog, confirmation, session, corruption,
and reopen fixtures are generated and synthetic. They use no owner data, retail executable, disc
image, memory card, savestate, emulator state, or PCSX2 input. The character and session schemas
remain independent of PS2 save representation and do not reinterpret any pointer or marker as retail
customization, campaign, save, gameplay, continuation, world-state, checkpoint, or behavioral-parity
semantics.

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
