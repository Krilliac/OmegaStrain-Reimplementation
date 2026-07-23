# 08 — Retail Front-End Presentation: Implementation Scope

Status: scoping draft (2026-07-23). Feeds planning for closing the
`retail front-end presentation is unavailable` gate.

## 1. Symptom and current behaviour

Launched in the default `RetailRequired` presentation mode (what the launcher's
**PLAY** button uses), `openomega.exe`:

1. loads content from the ISO, boots runtime services, plays `INTRO.PSS`;
2. enters the native host loop;
3. fails the front-end gate and exits **code 1**:

```
ERROR presentation: front-end presentation [presentation-unavailable]: retail front-end presentation is unavailable
runtime loop: front-end presentation [presentation-unavailable]: retail front-end presentation is unavailable
```

This is a **controlled exit, not a crash**. `--developer-diagnostics` mode uses
the project-authored front-end and runs normally.

## 2. The seam

`native/apps/openomega/omega_app.cpp:2931` — `AuthorizeCurrentFrontEndPresentation()`:

```cpp
if (presentation_mode_ == DeveloperDiagnostics)
    return AuthorizeFrontEndPresentation(presentation_mode_, front_end_presentation_.provenance);
// retail path — always fails closed:
return AuthorizeFrontEndPresentation(presentation_mode_, std::nullopt);
```

The nullopt overload (`native/src/runtime/front_end_presentation_gate.cpp:80-84`)
always returns `PresentationUnavailable`. The retail overload
(`front_end_presentation_gate.cpp:46-58`) authorizes exactly when
`mode == RetailRequired && capability.valid()`. So the gate is ready; nothing
supplies it a valid capability yet.

> The in-code comment claiming the "FNT/GUI/IE and display-conversion decoders
> are intentionally not guessed here" is **stale**. See §3 — those decoders and
> the capability-minting path already exist and are tested.

## 3. What already exists (verified in-tree)

| Component | Status | Location |
|---|---|---|
| Presentation gate + retail overload | EXISTS | `runtime/front_end_presentation_gate.*` |
| `RetailFrontEndPresentationCapability` (move-only provenance token) | EXISTS | `content/retail_front_end_presentation_capability.h` |
| `FrontEndScreenBundle` (owned retail presentation data) | EXISTS | `content/front_end_screen_bundle.h` |
| FNT decoder | EXISTS | `retail/fnt_v3_decoder.*` |
| GUI decoder (`DecodeGuiFrontend`) | EXISTS | `retail/frontend_document_decoder.*` |
| IE decoder (`DecodeIeFrontend`) | EXISTS | same file |
| TDX / display-conversion decoder | EXISTS | `retail/frontend_tdx_decoder.*`, `retail/tdx_texture_storage_decoder.*` |
| Retail string table decoder | EXISTS | `retail/retail_string_table_decoder.*` |
| Capability **minting** | EXISTS | `content/game_data_service.cpp` — `LoadFrontEndScreen()` (~1338-2101), mint at 2096-2100 |
| ISO9660 / HOG reader | EXISTS | `vfs/virtual_file_system.*` (`MountIso9660`, `MountHog`) |
| CPU raster / texture sampler / triangle kernel / timeline | EXISTS (primitives) | `frontend_presentation/retail_frontend_cpu_raster.*`, `…texture_sampler.*`, `screen_space_triangle_kernel.*`, `retail_frontend_timeline.*` |
| Static-title compositor (fail-closed slice) | EXISTS (narrow) | `frontend_presentation/retail_title_compositor.*` |
| Root-visual-only layer | EXISTS (partial) | `frontend_presentation/retail_root_visual_layer.*` |
| Requirements inspector | EXISTS | `frontend_presentation/retail_presentation_requirements.*` |

`GameDataService::LoadFrontEndScreen(FrontEndScreenKey)` already mounts the ISO,
reads the retail HOGs (`GAMEDATA/FRONTEND/NTSC.HOG`, `GAMEDATA/COMMON/FONTS.HOG`,
`GAMEDATA/COMMON/STRINGS.DAT`), decodes GUI/IE/TDX/FNT/strings, validates text
references, and **mints the `RetailFrontEndPresentationCapability` inside a fully
owned `FrontEndScreenBundle`**. This is exercised end-to-end by
`native/tests/front_end_screen_bundle_tests.cpp` against format-real archives.
The live `GameDataService` is already inside the app
(`ContentStartupState::game_data`, dereferenced at `omega_app.cpp:389-398`) — the
same object that reads `INTRO.PSS` today.

## 4. What is missing

Two gaps, very different in size.

### Gap A — App wiring (small, mechanical)

Nothing in `apps/` ever calls `LoadFrontEndScreen`. To close the gate the app must:

1. For each needed `FrontEndScreenKey` (`Title`, `CreateAgent`, `LoadAgent`),
   call `content_owner->game_data->LoadFrontEndScreen(key)` during app
   construction (retail mode only) and retain the returned `FrontEndScreenBundle`(s).
2. Change `AuthorizeCurrentFrontEndPresentation()` (`omega_app.cpp:2931`) so the
   retail branch passes `bundle.presentation_capability()` to the retail overload
   instead of `std::nullopt`.
3. Decide failure policy: a missing/failed bundle load must produce a clear
   startup error (routed through the existing `StartupFailure*` path so the
   launcher — see §7 — can surface it), not the current unconditional gate fail.

On its own, Gap A makes the gate **pass** and the app reach steady-state — but
with no retail pixels drawn yet unless Gap B supplies them. Sequencing note:
do not authorize the gate on a bundle the compositor cannot yet render, or the
app will present an empty/incorrect screen. Gate A should land together with at
least the Phase-1 compositor slice (§6), or behind a temporary internal flag.

### Gap B — Full per-screen retail compositor (the real work)

Turn a whole `FrontEndScreenBundle` into the per-mode `RenderTextureHandle`s +
`RenderDrawList`s the host loop consumes — the retail equivalent of the
project-authored `build_front_end_presentation` lambda (`omega_app.cpp:835+`),
but sourced from decoded retail assets instead of `BuildProjectFrontEnd*Image`.

Required sub-steps:

- Upload each `FrontEndTextureBinding`'s decoded TDX pixels as GPU textures.
- Rasterize IE visual geometry (via the existing CPU raster / sampler / triangle
  kernel) into draw commands, honouring the decorator/DFS resolve rules the
  bundle already implements (`ResolveVisualBinding` / `ResolveTextureBinding` /
  `ResolveVisualTextureBinding`).
- Lay out text from FNT + string table (`ResolveFont`, `ResolveFontAtlas`,
  `strings_`).
- Apply the timeline/animation tracks.
- Resolve GUI widget → visual → texture bindings and per-node **draw order**.

The still-unresolved semantic decisions are already catalogued as
`RetailPresentationRequirement` values by the inspector
(`retail_presentation_requirements.h`): text encoding, text layout,
animation tick selection, opacity/UV application, candidate ordering, depth
policy, visual↔widget lane merge, action lifecycle. Current composition coverage
is only the **fail-closed static title** (untextured, opaque, single-resource,
constant-colour) and the **root-visual-own-geometry-only** layer — neither is
authorized to represent a real screen.

## 5. Recon/inventory work already landed (context)

Recent `frontend`/`recon` commits added **inventory + contracts + partial
primitives**, not new decoders:

- `retail_presentation_requirements.*` — enumerates unresolved blockers.
- `retail_root_visual_layer.*` — root node's own triangles only.
- `retail_title_compositor.*` — fail-closed static title slice.
- CPU raster / sampler / triangle kernel / timeline primitives.
- `analysis/formats/FRONTEND-PHASE.md` + `tools/assemble_frontend_phase.py`
  (docs + Python tooling), plus the `FRONTEND-*` analysis docs.

The FNT/GUI/IE/TDX decoders predate this window.

## 6. Suggested phasing

1. **Phase 0 — App wiring behind a flag.** Land Gap A but keep the retail gate
   gated by an internal opt-in (env/launch option) so `main` PLAY still exits
   cleanly with the (now clearer) startup error until pixels exist. Add a
   `LoadFrontEndScreen` smoke path in the app.
2. **Phase 1 — Root/title screen, own geometry.** Compose the `Title` bundle's
   textured root + immediate children using existing primitives; resolve the
   TextEncoding / TextLayout / draw-order requirements for that one screen;
   authorize the gate for `Title` only.
3. **Phase 2 — Text + fonts.** FNT atlas upload + string layout; render real
   menu labels.
4. **Phase 3 — Animation/timeline + interaction.** Timeline application and GUI
   action lifecycle; wire the reducer (`ReduceFrontEnd`, presentation-agnostic
   and reusable) to retail-sourced views.
5. **Phase 4 — CreateAgent / LoadAgent screens** and remaining modes.

Each phase closes a defined subset of `RetailPresentationRequirement` blockers;
track them explicitly.

## 7. Related launcher change (this session)

The launcher (`native/apps/openomega_launcher/launcher_window.cpp`) previously
spawned `openomega.exe` fully detached and closed its own window, so a retail
failure vanished with no feedback and the VS debugger (attached to the launcher)
just ended. It now:

- redirects the child's stdout+stderr to
  `%LOCALAPPDATA%\OpenOmega\last-run.log`;
- supervises the child (hides itself while the game runs) and, on a non-zero
  exit, re-shows with the last diagnostic line — so this gate failure is now
  visible in the launcher;
- adds a **DEV DIAGNOSTICS** button that launches with `--developer-diagnostics`.

This makes the retail gap observable during development but does not change the
gate itself.

## 8. Risks / unknowns

- **Real-asset conformance is unproven.** Decoders fail closed on unsupported
  layouts; only synthetic (format-real) fixtures are in-repo. Whether a real
  retail ISO's `NTSC.HOG` / `FONTS.HOG` / `STRINGS.DAT` members fall inside the
  decoders' proven families is not established in the tree — validate early
  against the actual disc.
- **Final draw-order / animation / text semantics are deliberately unresolved**
  pending independent evidence (that is what the requirements inspector encodes).
  Gap B is bounded by resolving those, not by writing new decoders.
- **No TIM/TIM2 decoder exists** — but the front-end uses TDX, not TIM, so this
  is not on the critical path.
