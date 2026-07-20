# HOG-member structural fingerprint

## Scope

`tools/measure_member_structural_fingerprint.py` is a bounded, read-only aggregate measurement of
member payload sizes inside one HOG or a recursively discovered directory of HOGs. It does not
inspect target payload bytes. Payload sizes come only from the already-proven HOG offset table
described in `HOG.md`. The tool composes the same bounded span adapter and shared `parse_hog_span`
implementation used by the front-end topology scanner; it does not infer a new container layout.

The default suffix set is `.gui`, `.fnt`, and `.ie`. Callers may explicitly select `.bnk` or `.gun`,
but only the frozen source-level allowlist `.bnk`/`.fnt`/`.gui`/`.gun`/`.ie` is accepted. Matching is
ASCII case-insensitive. Output keys are canonical lowercase suffixes sorted lexicographically.
Unknown suffixes are ignored and are never echoed.

This measurement supplies structural evidence only. It assigns no header, field, payload-address
alignment, lookup, menu, font, event, weapon, audio, render, state, timing, or retail-behavior
semantics.

## Schema version 1

The compact JSON document has four fixed top-level fields:

- `schema_version`, exactly `1`;
- one fixed public `scope` string;
- `measurements`, containing one record for each selected allowlisted suffix; and
- `error_categories`, containing every typed path-free failure category.

Each suffix record contains exactly:

- `count`;
- `payload_size_min`;
- `payload_size_max`;
- `distinct_payload_size_count`; and
- `size_gcd`.

With no matching member, all five values are zero. With one or more matching members, minimum and
maximum include zero-size members, distinct count includes zero, and `size_gcd` uses the
conventional integer GCD fold beginning at zero. Thus an all-zero set reports `size_gcd: 0`;
whenever at least one measured size is nonzero, `size_gcd` is positive and divides every measured
payload size. It is derived only from sizes. It is never a payload-address alignment claim, and
individual offsets are neither retained nor reported.

The report never contains an input path, archive or member name, hash, byte, individual offset,
per-file or per-member row, unknown raw suffix, proprietary identifier, or exception string. A CLI
failure prints the same zeroed schema with one fixed error count and writes nothing to stderr.

Schema version 1 owns an explicit local error-category tuple; it does not derive that public
vocabulary from the shared topology module. If a future shared HOG failure carries a category
outside this frozen tuple, the CLI maps it to the local `internal` category without echoing the
upstream category or chained exception text.

## Grammar and limits

The established HOG rules remain authoritative: the offset table starts at `0x14`; the name table
immediately follows `count + 1` little-endian offsets; the first payload offset is zero; offsets are
nondecreasing; payload spans stay inside the containing span; and the name table contains the
declared count of ASCII names. Top-level HOGs must end at their logical extent. Nested `.hog`
members may contain only the already-accepted zero tail after their logical extent.

Equal adjacent offsets are valid under the proven nondecreasing grammar and represent zero-size
members. "Duplicate" rejection therefore applies to two member names that normalize to the same
case-insensitive archive identity; the tool does not invent a strict-offset rule. Truncated tables,
nonmonotone offsets, out-of-range spans, malformed names, normalized duplicates, and nonzero nested
tails fail closed.

Defaults and separate hard ceilings bound root-file count, filesystem entries and depth, root and
nested container bytes, container directory bytes, entries per container, total container
occurrences and entries, cumulative name-table bytes, individual name bytes, cumulative traversed
spans, nesting depth, cumulative offset-table reads, distinct sizes per suffix and overall, checked
counters, and serialized output bytes. Caller `ScanLimits` may tighten these ceilings but cannot
raise any hard ceiling. The hard ceilings are local literal values held in an immutable mapping,
rather than aliases of mutable or upstream configuration. Focused tests assert the complete error
tuple and every hard ceiling value exactly so schema drift fails visibly.

## Synthetic verification

The focused tests cover recursive exact aggregation, optional allowlisted suffixes, empty and
zero-size sets, a GCD that divides sizes despite an unaligned payload address, malformed and
truncated headers/tables, normalized duplicates, nonmonotone and out-of-range offsets, nested
corruption, exact and one-below root and nested byte, file, filesystem-entry/depth,
archive-occurrence, entry, name, traversal, nesting, offset-table, distinct-size, and output limits;
hard-ceiling rejection before input access; real symlink rejection where the platform permits
creation with a narrow link-classification fallback otherwise; determinism; and CLI privacy for
malformed secret paths, member names, unknown suffixes, future upstream categories, and raw chained
exception text. They use only generated HOG bytes.

```powershell
python -B -m unittest tools.tests.test_measure_member_structural_fingerprint -v
```

An owner-corpus run, decoder schema, native adapter, and retail-fidelity claim remain deliberately
outside this synthetic implementation pass.
