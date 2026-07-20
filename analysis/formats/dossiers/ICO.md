# .ico — whole-disc disposition dossier

## 1. Identity

The suffix `.ico` is observed only in the tracked whole-disc inventory. It has no occurrence
in either HOG inventory. This placement is an occurrence fact, not proof of a platform role, title
role, shared grammar, or out-of-scope decision. The authoritative decoder matrix therefore keeps
this family in the separate whole-disc disposition rather than assigning a decoder status.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive HOG inventory | 0 | `analysis/formats/asset-fingerprints.json` |
| Top-level HOG inventory | 0 | `analysis/formats/hog-validation.json` |
| Whole-disc inventory | 2 | `analysis/manifests/disc-summary.json` |

## 3. Confirmed facts

- The three tracked occurrence inventories produce the counts above.
- `analysis/formats/dossiers/catalog.json` records this suffix in
  `whole_disc_disposition`, outside the authoritative 31-family decoder matrix.
- No format-specific aggregate block for this suffix exists in
  `analysis/formats/asset-fingerprints.json`.

## 4. Aggregate-only facts

The only family-wide public aggregate established here is an occurrence count of 2 at
whole-disc scope and zero at both archive scopes. Existing tracked metadata may describe individual
files, but this dossier does not derive a new per-file relation or reproduce raw payload material.

## 5. Hypotheses

- **H1 — one structural family.** Files sharing `.ico` may implement one grammar, or the
  suffix may group unrelated content. A bounded aggregate structural experiment could distinguish
  those explanations without publishing identity-bearing rows.
- **H2 — runtime disposition.** The files may be platform support, title data, publishing metadata,
  or a mixture. Only a tracked consumer trace or independently documented compatibility contract
  can decide that question; directory placement and suffix convention cannot.

## 6. Missing observations

- No tracked family-wide header or envelope aggregate establishes a falsifiable grammar.
- No tracked consumer observation establishes whether the native runtime needs this family.
- No synthetic malformed-boundary test can be specified honestly until a grammar boundary is
  supported by evidence.

## 7. Decoder/tooling status

**Whole-disc disposition: `whole_disc_only_unclassified`.**

This label is intentionally not a decoder status. It makes no
`canonical_decoder`, `structural_envelope_only`, `passive_descriptor_only`,
`aggregate_scanner_only`, `unknown`, or system-file claim.

## 8. Codex work order

1. Keep this family outside the decoder matrix until a concrete runtime need or tracked structural
   observation exists.
2. If investigation becomes necessary, design the narrowest bounded aggregate that can falsify a
   stated hypothesis; do not publish paths, names, hashes, payload bytes, or per-file rows from a new
   collection.
3. Do not predeclare a native descriptor from occurrence or size alone. Require a falsifiable
   grammar, malformed boundaries, and proportionate synthetic tests first.
