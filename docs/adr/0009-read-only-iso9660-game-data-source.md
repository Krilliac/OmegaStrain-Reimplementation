# ADR 0009: Read-only ISO9660 game-data source

Status: accepted

## Context

OpenOmega previously required an owner to extract the retail disc filesystem before native startup.
The desktop source picker also needs to accept the owner's regular `.iso` directly without copying,
modifying, publishing, or placing that proprietary input in the repository.

## Decision

`VirtualFileSystem::MountIso9660(path)` indexes the primary ISO9660 volume using fixed 2048-byte
logical blocks. It supports nested directories, ASCII case-insensitive game paths, and strips only
the exact canonical `;1` suffix from file identifiers. The frozen mount owns an immutable map of
normalized paths to checked image ranges. Newer VFS mounts retain the existing deterministic
override policy.

Descriptor, record, extent, arithmetic, record-count, directory-count, depth, identifier-byte,
directory-byte, and estimated-index-memory bounds are enforced before publication. Directory
cycles, directory-start-sector aliases, malformed `.`/`..` hierarchy records, interleaving,
multi-extent files, and duplicate normalized paths fail the whole mount. File payload extents may
legitimately overlap and are not treated as aliases. Payload size is checked against the caller's
limit before allocating its independently owned result.

All diagnostics at the `GameDataService` boundary remain categorical and omit the owner-supplied
host path. The adapter never writes to the image and never interprets or recompiles executable code.

## Consistency and format limits

The immutable object is the published index, not the external image bytes. Indexing parses owned,
bounded descriptor and directory-block copies. A payload read acquires one complete owned bounded
buffer before publishing it. File size and modification time are checked around indexing and each
payload acquisition to catch ordinary concurrent replacement, mutation, or truncation.

Those metadata checks are best-effort consistency detection, not authentication: an adversary that
mutates bytes and restores both data and metadata can evade them. OpenOmega does not hash an entire
multi-gigabyte disc or snapshot it into memory. The owner should treat a mounted image as read-only
for the process lifetime.

Joliet, Rock Ridge, extended-attribute records, interleaved files, multi-extent files, non-2048-byte
logical blocks, and multi-volume sets are deliberately unsupported. Adding any of them requires a
separate bounded clean-room contract and synthetic fixtures.
