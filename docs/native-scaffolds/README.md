# Evidence-first native format promotion

This is a language-neutral checklist for promoting an observed retail member family into a native
descriptor or decoder. It deliberately contains no provisional C++ declarations, type names, entry
points, headers, or source skeletons. Code parked under `docs/` is not compile-checked and would rot.
An opaque whole-input `logical_extent` would also imply that a boundary was validated when no
evidence establishes one, while predeclared types and entry points freeze ownership and API choices
before the evidence establishes what the implementation should represent.

The checklist distills the useful part of the Claude-authored scaffold submission retained in PR
#64 at commit `2421adfbe2c710a8fb64d85046075b461e2b8690`. Its thirteen evidence-pending header
declarations were intentionally removed during integration because they duplicated the dossiers and
predeclared unproven APIs.

## Promotion gate

Before writing native format-specific code:

1. Confirm that the suffix and occurrence scope are recorded in
   `analysis/formats/dossiers/catalog.json` and `analysis/formats/DECODER-COVERAGE.md`.
2. Collect the narrowest useful aggregate. For `.gui`, `.fnt`, and `.ie`, use the bounded size-only
   `tools/measure_member_structural_fingerprint.py`; `.bnk` and `.gun` are optional allowlisted
   suffixes. Do not add byte positions, magic values, or fields until an aggregate result supports
   them.
3. Record the result in the evidence ledger with its reproducible check. An implemented collector
   with synthetic tests is not an owner-corpus result.
4. State the evidence level independently from implementation coverage: Confirmed, Inferred,
   Hypothesis, or Rejected per `docs/01-Clean-Room-Method.md`.
5. Stop if the evidence supports only occurrence or whole-input size. A size-only fingerprint does
   not justify an accept/reject parser. Promotion requires a falsifiable grammar with synthetic
   malformed boundaries that the implementation can reject for evidence-backed reasons.

## Native implementation checklist

Once a stable structural boundary is confirmed:

1. Choose the smallest output that preserves only proven structure. Keep unknown regions opaque and
   avoid role-oriented names.
2. Specify ownership, thread affinity, caller-tightenable `DecodeLimits`, immutable hard ceilings,
   checked arithmetic, allocation behavior, and path-free typed failures before freezing the API.
3. Implement a bounded, fail-closed reader in `native/src/retail/` with its public contract under
   `native/include/omega/retail/` only after review.
4. Add generated exact, malformed, truncated, hostile-count, exact/one-below-limit, deterministic,
   ownership, and allocation-failure tests. Never use owner bytes as fixtures.
5. Register the implementation and focused test in CMake, then run the dependency, tooling,
   compile-all, public-tree, formatting, and DCO gates plus proportionate C++ validation.
6. Run a metadata-only owner-corpus verification before claiming corpus coverage. Claim semantics
   only after an independent behavioral oracle supports them.

## Current occurrence-only candidates

The following archive-member families remain occurrence-only in the authoritative coverage matrix:

| Suffix | Recursive-in-HOG | Top-level-HOG |
| --- | ---: | ---: |
| `.bnk` | 77 | 77 |
| `.bon` | 156 | 156 |
| `.fnt` | 3 | 3 |
| `.gui` | 77 | 21 |
| `.gun` | 624 | 0 |
| `.ie` | 79 | 23 |
| `.prn` | 1 | 1 |
| `.pss` | 54 | 54 |
| `.scc` | 1 | 1 |
| `.skel` | 4 | 4 |
| `.skf` | 26 | 26 |
| `.sub` | 42 | 42 |
| `.txt` | 3 | 0 |

These counts come from the tracked aggregate inventories. They prove occurrence only and freeze no
header, field, alignment, role, API, or native implementation.
