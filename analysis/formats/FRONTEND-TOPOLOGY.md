# Front-end HOG topology measurement

## Scope

`tools/measure_frontend_hog_topology.py` is a bounded, read-only experiment for
mapping the aggregate shape of one supplied HOG or a recursively discovered
directory of HOGs. It reuses the established span-aware HOG parser and follows
only members whose normalized suffix is `.hog`.

The JSON report never contains an input path, member name, hash, offset, payload
byte, raw unapproved suffix, identifier, per-file row, or exception message.
The output vocabulary is frozen in public source. Unknown suffixes and members
without suffixes collapse into one `other` count.

This measurement establishes only container topology. It does not assign a
front-end asset role, lookup rule, menu state, layout, render binding, audio
event, timing, control flow, or retail behavior.

## Aggregate contract

The schema-version-2 document contains:

- totals for root and nested archive occurrences, member occurrences, approved
  and other members, and same-basename groups/pairs;
- fixed counts for the approved public suffixes `.col`, `.gui`, `.hog`, `.pop`,
  `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.txt`, `.vag`, and
  `.vum`;
- fixed category counts for animation, audio, collision, container, gui,
  material, mesh, scene, script, skeleton, table, text, and texture;
- archive-depth counts for depths zero through sixteen;
- exact-root, exact-nested, and zero-padded-nested HOG extent families;
- seven fixed member-size buckets, both overall and per approved category plus
  `other`; and
- counts for every unordered pair of approved suffixes that shares a normalized
  sibling basename. Basenames themselves are never emitted.

Top-level HOGs must end at their logical extent. Nested HOG members may either
end exactly or contain only zero padding after the logical extent. A nonzero
tail is malformed.

### Schema history

- Schema version 1: the initial frozen vocabulary. `.gui` members collapsed
  into the `other` count.
- Schema version 2: `.gui` was promoted out of `other` into the frozen public
  vocabulary as its own suffix count and its own `gui` category. This adds the
  `.gui` extension count, the `gui` category count, the `gui` per-category
  extent buckets, and every unordered pair key that combines `.gui` with an
  approved sibling suffix (for example `.gui+.tdx`). The `gui` label echoes the
  suffix only; it asserts no menu role, layout, lookup rule, render binding, or
  other retail semantics. No other suffix was promoted: menu-adjacent-sounding
  suffixes such as `.fnt` and `.ie` deliberately remain in the `other` bucket
  because the tracked evidence does not justify freezing them into the public
  vocabulary.

## Limits and failure behavior

The defaults cap root archive count, filesystem entries and depth, root and
nested archive bytes, directory bytes, entries per directory, total archive
occurrences, total HOG entries, cumulative name-table bytes, individual name
bytes, cumulative traversed span bytes, and nesting depth. A second hard ceiling
prevents callers from replacing those defaults with unbounded values.

Filesystem links/reparse points, non-regular leaves, case-folded filesystem
collisions, unsafe archive names, normalized archive-name collisions, malformed
tables/extents, and every exhausted budget fail closed. The command-line entry
point prints only the same fixed aggregate schema with one typed error-category
count; it does not print the path or parser exception.

## Synthetic verification

Public tests construct exact and zero-padded nested HOGs in memory. They cover
recursive filesystem discovery, approved and other classification, depth and
extent distributions, sibling-basename extension pairs, malformed offsets,
invalid and colliding names, top-level padding, nesting, entry/name/size limits,
fixed bucket boundaries, deterministic JSON, and identity-free error output.
No owner input or proprietary byte is required by the tests.

## Related evidence collection

`MEMBER-STRUCTURAL-FINGERPRINT.md` defines a separate size-only collector for default
`.gui/.fnt/.ie` measurements and optional `.bnk/.gun`. It reuses the proven HOG boundary but does
not change this topology schema, assign a front-end role, read target payload bytes, or justify an
envelope decoder. A synthetically verified collector is not an owner-corpus result.

## Reproduce

Run the synthetic contract:

```powershell
python -B -m unittest tools.tests.test_measure_frontend_hog_topology -v
```

Run the scanner against an ignored owner-supplied directory and redirect only
the aggregate document to ignored analysis output:

```powershell
python -B .\tools\measure_frontend_hog_topology.py `
  .\private\frontend-research `
  > .\analysis\output\frontend-hog-topology.json
```

Do not commit the supplied input. This public implementation and its tests were
developed only from documented HOG structure and synthetic fixtures; no owner
input was read for E-0086.
