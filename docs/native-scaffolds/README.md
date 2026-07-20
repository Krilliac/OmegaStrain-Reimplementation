# Native format-descriptor scaffolds (evidence-pending)

These are **skeletons, not decoders**. Each is a header-only placeholder for a member family
that is observed inside HOG archives but has **no tracked structural evidence** and **no native
decoder** yet. They exist so the Codex workstream has the file structure and entry point to start
from once evidence is collected.

## Rules they obey

- No invented semantics: each descriptor holds only an opaque `ObservedExtent` (the whole input).
  There are no named/typed structural fields, because none are proven. A plausible invented
  decoder is a regression per the clean-room brief.
- Not wired: none are in `CMakeLists.txt`, the runtime, or any test, so they claim no coverage.
- Located under `docs/native-scaffolds/` (not `native/`) on purpose: the native-dependency gate
  rejects any unclassified `.h` under `native/`, and placing them in `native/include/omega/retail/`
  would falsely imply an active decoder.

## Turning one into a real descriptor (per header's own checklist)

1. Collect aggregate structural evidence (run `tools/measure_member_structural_fingerprint.py`, or
   add a `fingerprint_<suffix>` handler to `tools/fingerprint_assets.py`, against the owner corpus).
2. Populate the struct with `ObservedByteRange`/counts ONLY for aggregate-proven structure; add a
   manual `operator==` (see `SkmContainerDescriptor`).
3. Move the header to `native/include/omega/retail/`; add `native/src/retail/<suffix>_container_descriptor.cpp`
   implementing a bounded, fail-closed, `DecodeLimits`-honoring reader.
4. Register in `CMakeLists.txt`; add `native/tests/<suffix>_container_descriptor_tests.cpp` with
   exact/malformed/truncated/limit/determinism adversarial coverage BEFORE claiming coverage.
5. Append an evidence-ledger entry that states exactly what was validated and what remains unproven.

## Scaffolds in this directory

| Format | Recursive-in-HOG | Top-level-HOG | Descriptor type | Entry point | File |
| --- | ---: | ---: | --- | --- | --- |
| .bnk | 77 | 77 | `BnkContainerDescriptor` | `InspectBnkContainer` | [bnk_container_descriptor.h](bnk_container_descriptor.h) |
| .bon | 156 | 156 | `BonContainerDescriptor` | `InspectBonContainer` | [bon_container_descriptor.h](bon_container_descriptor.h) |
| .fnt | 3 | 3 | `FntContainerDescriptor` | `InspectFntContainer` | [fnt_container_descriptor.h](fnt_container_descriptor.h) |
| .gui | 77 | 21 | `GuiContainerDescriptor` | `InspectGuiContainer` | [gui_container_descriptor.h](gui_container_descriptor.h) |
| .gun | 624 | 0 | `GunContainerDescriptor` | `InspectGunContainer` | [gun_container_descriptor.h](gun_container_descriptor.h) |
| .ie | 79 | 23 | `IeContainerDescriptor` | `InspectIeContainer` | [ie_container_descriptor.h](ie_container_descriptor.h) |
| .prn | 1 | 1 | `PrnContainerDescriptor` | `InspectPrnContainer` | [prn_container_descriptor.h](prn_container_descriptor.h) |
| .pss | 54 | 54 | `PssContainerDescriptor` | `InspectPssContainer` | [pss_container_descriptor.h](pss_container_descriptor.h) |
| .scc | 1 | 1 | `SccContainerDescriptor` | `InspectSccContainer` | [scc_container_descriptor.h](scc_container_descriptor.h) |
| .skel | 4 | 4 | `SkelContainerDescriptor` | `InspectSkelContainer` | [skel_container_descriptor.h](skel_container_descriptor.h) |
| .skf | 26 | 26 | `SkfContainerDescriptor` | `InspectSkfContainer` | [skf_container_descriptor.h](skf_container_descriptor.h) |
| .sub | 42 | 42 | `SubContainerDescriptor` | `InspectSubContainer` | [sub_container_descriptor.h](sub_container_descriptor.h) |
| .txt | 3 | 0 | `TxtContainerDescriptor` | `InspectTxtContainer` | [txt_container_descriptor.h](txt_container_descriptor.h) |

> Counts are tracked aggregates from `analysis/formats/asset-fingerprints.json` and
> `analysis/formats/hog-validation.json`. They establish only that the family exists and how
> often; they say nothing about internal structure.
