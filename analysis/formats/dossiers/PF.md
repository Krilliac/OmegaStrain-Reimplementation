# .pf — hard-rule Unknown dossier

## 1. Identity

The suffix `.pf` appears only in the whole-disc inventory: 3 occurrences and no
HOG-member occurrence. The campaign explicitly requires this family to remain Unknown. Suffix
convention, co-location, and absence from the current asset pipeline are not evidence of a system
role or permission to scope it out.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive HOG inventory | 0 | `analysis/formats/asset-fingerprints.json` |
| Top-level HOG inventory | 0 | `analysis/formats/hog-validation.json` |
| Whole-disc inventory | 3 | `analysis/manifests/disc-summary.json` |

## 3. Confirmed facts

- The three tracked occurrence inventories establish the counts above.
- The authoritative `analysis/formats/DECODER-COVERAGE.md` matrix classifies `.pf` as
  Unknown under the campaign hard rule.
- No native decoder, structural envelope, passive descriptor, or format-specific aggregate is
  claimed for this suffix.

## 4. Aggregate-only facts

Only the family-wide occurrence totals above are carried forward. They establish neither one shared
grammar nor any runtime or platform role.

## 5. Hypotheses

- **H1 — shared grammar.** The 3 files may share a structure, or the suffix may group
  unrelated content. A bounded, identity-free aggregate can test this.
- **H2 — consumer.** A title, platform component, or neither may consume the files. A tracked
  consumer observation is required; placement cannot decide it.

## 6. Missing observations

- No tracked header, envelope, field, or malformed-boundary evidence exists.
- No tracked consumer trace establishes whether this family participates in any native flow.
- No owner-corpus structural result has been published for this family.

## 7. Decoder/tooling status

**Classification: `unknown` (hard-rule mandated).**

Unknown is intentional. It must not be replaced with `system_file_out_of_scope` or a decoder class
without a new, independently reviewed evidence change.

## 8. Codex work order

1. Preserve Unknown status until a designed experiment supplies tracked evidence.
2. If work is authorized, begin with a bounded aggregate that emits no identity-bearing rows or raw
   values.
3. Do not create a native descriptor from occurrence or size alone. Promotion requires a falsifiable
   grammar and generated malformed-boundary tests.
