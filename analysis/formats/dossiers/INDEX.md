# Omega Strain format dossiers — master index

## Purpose and authority

This directory records one evidence-tiered dossier for each of the 47 suffix families present in
the tracked aggregate inventories. `catalog.json` is the deterministic family-to-file map.
`analysis/formats/DECODER-COVERAGE.md` is authoritative for native/tool coverage of the 31 families
in its matrix; this index mirrors that classification rather than defining a competing taxonomy.

The other 16 families occur only in the whole-disc inventory. They are listed separately because
disc placement alone does not establish a decoder class, system role, or out-of-scope decision.

## Clean-room evidence contract

- Confirmed, Inferred, Hypothesis, and Rejected retain the meanings in
  `docs/01-Clean-Room-Method.md`. Aggregate-only describes disclosure scope, not a replacement
  evidence state.
- A dossier may cite already-tracked, policy-allowed compatibility metadata such as hashes, sizes,
  addresses, offsets, names, and call-graph facts when necessary and non-reconstructive. New
  collector output must remain path-free, identity-free, aggregate-only, and contain no payload
  bytes or per-file rows.
- Claims cite Git-tracked sources. Private inputs, runtime logs, D-drive content, third-party trees,
  and untracked files are never evidence targets in these dossiers.
- Suffix spelling, directory placement, absence from a decoder registry, and an unrun collector do
  not establish semantics. Competing explanations remain Inferred or Hypothesis.
- A collector implementation plus synthetic tests proves only the collector contract. It does not
  prove an owner-corpus result, format grammar, native API, or retail behavior.

## Authoritative 31-family decoder matrix

### canonical_decoder (6)

| Suffix | Dossier |
| --- | --- |
| `.col` | [COL.md](COL.md) |
| `.hog` | [HOG.md](HOG.md) |
| `.pop` | [POP.md](POP.md) |
| `.tdx` | [TDX.md](TDX.md) |
| `.vag` | [VAG.md](VAG.md) |
| `.vum` | [VUM.md](VUM.md) |

### structural_envelope_only (4)

| Suffix | Dossier |
| --- | --- |
| `.lpd` | [LPD.md](LPD.md) |
| `.par` | [PAR.md](PAR.md) |
| `.skas` | [SKAS.md](SKAS.md) |
| `.vpk` | [VPK.md](VPK.md) |

### passive_descriptor_only (5)

| Suffix | Dossier |
| --- | --- |
| `.ska` | [SKA.md](SKA.md) |
| `.skl` | [SKL.md](SKL.md) |
| `.skm` | [SKM.md](SKM.md) |
| `.so` | [SO.md](SO.md) |
| `.tbl` | [TBL.md](TBL.md) |

### aggregate_scanner_only (14)

| Suffix | Dossier |
| --- | --- |
| `.bin` | [BIN.md](BIN.md) |
| `.bnk` | [BNK.md](BNK.md) |
| `.bon` | [BON.md](BON.md) |
| `.fnt` | [FNT.md](FNT.md) |
| `.gui` | [GUI.md](GUI.md) |
| `.gun` | [GUN.md](GUN.md) |
| `.ie` | [IE.md](IE.md) |
| `.prn` | [PRN_.md](PRN_.md) |
| `.pss` | [PSS.md](PSS.md) |
| `.scc` | [SCC.md](SCC.md) |
| `.skel` | [SKEL.md](SKEL.md) |
| `.skf` | [SKF.md](SKF.md) |
| `.sub` | [SUB.md](SUB.md) |
| `.txt` | [TXT.md](TXT.md) |

### unknown (2)

| Suffix | Dossier |
| --- | --- |
| `.pf` | [PF.md](PF.md) |
| `.tm2` | [TM2.md](TM2.md) |

`.pf` and `.tm2` remain Unknown by the campaign's explicit hard rule. Whole-disc placement and
suggestive names cannot promote either family.

## Whole-disc disposition (16)

These dossiers preserve occurrence and existing compatibility evidence without assigning a decoder
status. A specific known file may have native compatibility support—for example `SYSTEM.CNF`—without
proving that every file sharing its suffix is one format family.

### whole_disc_disposition (16)

| Suffix | Dossier |
| --- | --- |
| `.64` | [64.md](64.md) |
| `.bd` | [BD.md](BD.md) |
| `.cnf` | [CNF.md](CNF.md) |
| `.dat` | [DAT.md](DAT.md) |
| `.elf` | [ELF.md](ELF.md) |
| `.hd` | [HD.md](HD.md) |
| `.icn` | [ICN.md](ICN.md) |
| `.ico` | [ICO.md](ICO.md) |
| `.img` | [IMG.md](IMG.md) |
| `.ini` | [INI.md](INI.md) |
| `.irx` | [IRX.md](IRX.md) |
| `.log` | [LOG.md](LOG.md) |
| `.map` | [MAP.md](MAP.md) |
| `.rgb` | [RGB.md](RGB.md) |
| `.sys` | [SYS.md](SYS.md) |
| `.wdb` | [WDB.md](WDB.md) |

## Ranked next work

1. Run the hardened size-only HOG-member collector documented in
   `analysis/formats/MEMBER-STRUCTURAL-FINGERPRINT.md` privately for the default
   `.gui/.fnt/.ie` set. `.bnk/.gun` are optional allowlisted measurements. Commit only a sanitized
   aggregate after independent review.
2. Keep the front-end envelope gate closed. Size regularity alone does not establish a falsifiable
   grammar or justify `GuiEnvelopeIR`; a native descriptor requires stable structure plus malformed
   boundaries and independent consumer evidence.
3. Run metadata-only owner-corpus validation for existing synthetic-only LPD, PAR, VPK, SKAS, SKL,
   TBL, and VAG boundaries where their authoritative format notes still leave that validation unclaimed.
   Their publication on main is already established and must not be re-opened as a work item.
4. Treat `.pf` and `.tm2` as Unknown until a designed experiment produces tracked evidence.
   Do not promote the 16 whole-disc-only families from placement or public suffix convention alone.
5. Use `docs/native-scaffolds/README.md` before proposing a native descriptor. Occurrence and size
   ranges are evidence inputs, not accept/reject grammars or API designs.

## Remaining nonclaims

The dossier set does not add an owner-corpus measurement, decode any previously opaque payload,
identify a retail menu consumer, or establish layout, lookup, rendering, audio, timing, gameplay, or
PCSX2 equivalence. Composite formats retain the narrower scope stated by their authoritative native
interfaces—for example VUM's canonical material catalog and separate passive render-payload view.

## Provenance

The initial 47-dossier corpus and collector draft came from Claude commit `98b5096`, integrated in
PR #64 as `38b7c0fc`. Codex corrected the integration against current-main evidence and the hardened
collector contract. The evidence-first promotion checklist distills Claude commit `a2fdf3d`,
integrated in PR #64 as `2421adf`, without retaining its premature C++ declarations.
