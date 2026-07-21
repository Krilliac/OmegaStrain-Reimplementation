# Full-Spectrum Campaign Ledger — 2026-07-20

Coordinator: Claude Code (Opus 4.8) background session. Base: origin/main `1f476d7`.
Campaign branch: `claude/full-spectrum-campaign-20260720`
(worktree checkout `OmegaStrain-claude-campaign`). Publication is Codex's; nothing here is pushed.

## Operating envelope (binding)
- RAM preflight: started **STOP** (1.4G free), recovered to **CAUTION** (2.5G). => No native MSVC
  build/CTest run this session. Native slices are **designed-only** (verification blocked). The
  **promotable surface** is build-independent: Python tooling tests, static gates, docs, DCO.
- Clean-room: public repo + project fixtures + lawful docs only. No D:, no OmegaPrivateEvidence, no
  ISO/BIOS/ELF/savestate/memcard/disc/PSS payload, no proprietary asset copied anywhere.
- No push / no PR / no main mutation. DCO-sign retained commits. All ~20 pre-existing worktrees
  preserved; no `git add -A`, no reset --hard, no history rewrite.

## Verifiable gates used this session (promotion surface)
- `python -m unittest discover -s tools/tests` (~298 tests)
- `python tools/check_public_tree.py` ; `python tools/check_native_dependencies.py` ; `python tools/check_dco.py <range>`
- `python -m compileall tools`
- JSON/JSONL parse of `analysis/evidence/ledger.jsonl`

## Frontier (verified from docs)
- Opening movie: E-0102 — Windows Media Foundation decode path only. No cross-platform decoder seam.
- Front end: E-0089 bounded card UI + E-0096 session-only profile selection. Project-owned bootstrap
  shell; NOT an evidence-backed retail menu. A project-owned fixed 3x5 glyph table renders fixed
  and projected profile labels, but no retail font decoder or retail-asset-backed renderer exists (H).
- FNT/GUI/IE: E-0101 passive prefix HYPOTHESES; retail provenance of constants UNRECORDED (audit C6).
- Mission loop: only synthetic Position3 locomotion (E-0060). AI absent (grep-confirmed zero hits).
- Engine/SDK: roadmap SDK-0..SDK-8; maturity ladder 0-6 + Exportable. Compatibility-first (ADR 0004).

## Discovery wave — 14 read-only cloud scouts, all domains A-T owned, non-overlapping
Each applied 4 lenses (capability gap / correctness-security / smallest testable slice / evidence
challenge). ~90 candidate microtasks surfaced. Highlights (file:line verified by the scout):

- **A boot/menu:** critical path (movie->menu) is a project-owned scaffold with NO tracked evidence
  tying either endpoint to retail boot. Risk: `omega_app.cpp:1413-1430` escalates a post-render
  movie-audio fault delta to a fatal Run error, contrary to ADR 0003's fail-open transition contract.
  [native-blocked]
- **B front-end formats:** `kFntObserved*`/GUI/IE header constants + "aggregate-observed" comments
  overclaim vs audit C6 ("no tracked source records retail provenance") and A5/C4 (no tracked
  aggregate even contains those per-suffix header fields). [BUILD-INDEPENDENT]
- **B2 menu interactivity:** no Back/Cancel edge distinct from confirm; Profiles confirm ALWAYS
  commits SetActiveProfile so a browser cannot leave without activating. The initially reported
  replay "bypass" was rejected on audit: null front-end state is the documented legacy nonmodal
  mode, and the production capture/replay path supplies front-end state. [native]
- **C media:** only Windows MF decode; off-Windows hard-fails with no decoder seam. Watchdog risk:
  repeated greater-than-half-clock backward PTS discontinuities can synthesize an impractically long
  SafetyDurationTicks despite saturating arithmetic, defeating the practical fail-open bound.
  SShd/48k-stereo PCM constants have untracked provenance (like E-0101). [native policy + tests]
- **D+E vfs/texture:** item-budget divergence `game_data_service.cpp:129` (material path omits
  terrain_cells from initial_items -> can overrun shared budget). The initially reported VUM OOB
  was rejected on audit: the outer layout validator restricts observed middle spans before the
  cited accesses, making the alleged underflow path unreachable. Texture->material binding exists
  in docs only, absent in code. [native for fixes]
- **F+G geometry/anim:** the initial SKA extent-formula concern was rejected on audit. Tracked
  evidence records 213/213 accepted spans satisfying the relation; it is not a proven P0 defect.
- **H rendering:** project-owned textured debug blits and a fixed glyph table render fixed/projected
  labels, but no retail font decoder or evidence-backed retail menu renderer exists. Risk:
  `sdl_gpu_host.cpp:1287` submits an acquired-but-unrecorded swapchain image on a plan failure —
  D3D12-tolerated, Vulkan-risky. Backend-neutrality overclaimed (all golden pixels D3D12-only). [native]
- **I+J sim/AI:** AI entirely absent. Gap: no per-step system hook turning position into an
  observable outcome (trigger/objective). `component_store.h` EntityId carries no registry token ->
  cross-world handle reuse silently corrupts. O(capacity) Snapshot() in spawn = quadratic. [native]
- **K+L input/audio:** audio "presentation clock" ticks on SDL buffer appetite (requested frames),
  counting silence/underrun as elapsed A/V time -> drift presented as a sync timebase
  (`opening_movie_audio_clock.h` vs `sdl_audio_service.cpp:116-119`). Realtime callback re-zeroes a
  4KB array up to ~256x/invocation. No remap/analog path. [native for fixes]
- **M persistence:** active-profile selection is session-only (E-0096) — lost every launch; no
  persistent pointer. POSIX group/other-write root rejection has NO Windows-ACL equivalent
  (`save_database.cpp:548`, inside `#if !defined(_WIN32)`). A retail checkpoint schema now = fabrication
  (no save-slot evidence). [native for pointer]
- **N+O net/SDK:** no networking code (target concept only). `omega_tool` is verify-only; no
  `omega_assetc`/cooked-format/hot-reload. REAL bug: `generate_manifest.py:34` sorts by `.lower()`
  (not a total order) while docstring claims "deterministic". ADR-0004 boundary
  `omega_content->omega_retail_formats` is the sole retail edge — unguarded by any linter.
  [BUILD-INDEPENDENT: manifest fix; dep-edge guard]
- **P+Q test/CI:** fresh-VM consumer + package retention are push-to-main-only (`native-ci.yml:130`)
  -> a PR breaking ZIP layout/launcher contract passes all PR checks, fails only post-merge. Capture/
  replay CLI smoke never runs in CI (OMEGA_RUN_GPU_SMOKE_TEST default OFF). No fuzz harness. [mixed]
- **R security/privacy:** the public-tree gate validates FORM not CONTENT. Blind spots:
  `check_public_tree.py` BLOCKED_ROOTS lists only `analysis/output`; `PS2_EXECUTABLE_NAME.fullmatch`
  bypassed by extra suffix; no owner-home-path (drive:\Users\<name>, /home/<name>) content scan; no
  hex/base64 retail-byte-reencoding heuristic. Native parsers audited clean (CheckedAdd/Multiply,
  span-bounded). [BUILD-INDEPENDENT gate hardening — scope carefully, do NOT auto-scrub tracked evidence]
- **S+T docs/reuse:** ledger `E-0095` uses spaced JSON (101 others compact); E-0012 gap unexplained.
  The M10 "allowlist of promoted engine/SDK targets" is cited in 3 places (roadmap:176-177,
  milestones:1199, adr0004:82) but **does not exist**. ADR-0004 asserts present-tense "reusable"
  ahead of the project's own second-consumer bar (roadmap:84). [BUILD-INDEPENDENT]

## Synthesis — ranking dimensions
menu-criticality x evidence-confidence x independence x testability(this session) x reusable-engine value.
At CAUTION/STOP, "testability this session" gates promotion: native slices cannot be verified, so
they are recorded as designed contenders, not accepted.

## Selected contenders — build-independent, to implement + verify + DCO-commit this session
1. **C1 — `generate_manifest.py` canonical total-order determinism fix + self-test** [N+O, SDK-0/Q].
   Real reproducibility bug; docstring-vs-behavior drift. Low risk, isolated.
2. **C2 — `check_public_tree.py` safe hardening + tests** [R, clean-room core]. Fix fullmatch-suffix
   bypass; add owner-path content scan scoped to NOT false-flag accepted game-identity strings.
   Verify the gate still passes the existing tree. Do NOT delete/scrub tracked evidence (Codex/owner call).
3. **C3 — evidence-ledger format lint + `E-0095` normalization + `E-0012`-gap note** [S, SDK-0].
   Re-serialize E-0095 compactly (identical semantics); add a build-independent ledger-format checker+test.
4. **C4 — FNT/GUI/IE (+ media SShd) provenance-honesty comment reconciliation** [B/C, SDK-0, menu path].
   Comment-only edits aligning headers to audit C6 ("project-defined hypothesis; retail provenance
   untracked"). No symbol renames (would need a build). Verify via public-tree + native-dep gates.

## Designed-but-blocked contenders (native-build; verification blocked on RAM this session)
Ranked for a next session that can build+CTest. Strongest first:
- **N3 [P0 correctness]** item-budget divergence `game_data_service.cpp:129` (material path).
- **N4 [P0 reliability]** practical watchdog bound under repeated PTS discontinuities; define the
  fail-open cap in the ADR and test normal, wrap, discontinuity, and saturation cases.
- **N5 [P0 contract]** route post-render movie-audio fault deltas through ADR 0003's fail-open
  transition while proving device/sample containment; do not blindly ignore control failures.
- **N6 [P1 interactivity, menu path]** Back/Cancel reducer edge in `front_end.h` ReduceFrontEnd + oracle/sweep.
- **N7 [P1 safety]** EntityId registry token in `component_store.h` (fail-closed cross-world reuse).
- **N8 [P1 gameplay seam]** pure AABB/proximity-trigger evaluator (independent, synthetic, mirrors debug_locomotion).
- **N9 [P1 media portability]** abstract `IH262Decoder` seam behind the MF impl.
- **N10 [P1 persistence]** persistent `profiles/active` pointer record via existing atomic commit.

Rejected after independent Codex source/evidence audit (stable IDs retained to avoid ambiguity):
- **N1 REJECTED:** VUM access is protected by the validated observed-span/layout envelope.
- **N2 REJECTED:** tracked SKA evidence supports the current relation for all 213 accepted spans.
- **N11 REJECTED:** null front-end state is an intentional, tested legacy nonmodal replay mode.

## Campaign commits (dependency order)
All four are build-independent, self-verified, and DCO-signed. A subsequent independent Codex
integration audit approved C1/C4 and required follow-up hardening for C2/C3 before publication.

| SHA | Slice | Reviewer verdict |
|-----|-------|------------------|
| `5eebbd8` | C1 tools: file-manifest deterministic total order + self-test | ACCEPT (genuine total order; order-only; docstring now truthful) |
| `a280cfb` | C2 tools: public-tree gate — close fullmatch suffix bypass + case-insensitive owner-home-path scan + tests | CONDITIONAL (PS2 suffix fix sound; escaped/Unicode home paths and URL/dot-segment cases required follow-up) |
| `ff9263a` | C3 tools: evidence-ledger format gate + E-0095 normalization | CONDITIONAL (E-0095 is provably whitespace-only; strict JSON, type, key-order, empty-file, and byte-format checks required follow-up) |
| `4e7b0ff` | C4 retail: FNT/GUI/IE prefix constants labeled project-defined hypotheses (cites audit C6) | ACCEPT (near-verbatim to audit C6/C8/A5/C4; comment-only; consistent) |

Gates run green on the assembled tree: 315 tooling tests; public-tree (420 blobs); native-dependency
(247 files); ledger-format (102 records); Python compile-all; `git diff --check`; DCO (4/4 commits).

### Reviewer-noted non-blocking follow-ups (applied or deferred)
- APPLIED: C2 added `re.IGNORECASE` (catches `C:\\users`, `/HOME/` variants; placeholders still spared).
- APPLIED in Codex integration: scan decoded UTF-8, catch escaped separators and Unicode usernames,
  exclude URLs/dot segments, and add adversarial regression tests.
- APPLIED: C3 error string corrected to `[A-Z]-####` (matches the accepting regex).
- APPLIED in Codex integration: strict UTF-8/LF/final-newline handling, strict finite JSON,
  canonical key order, required-field type checks, and empty/blank-ledger rejection.
- DEFERRED (needs a build): rename `kFntObserved*`/`observed_word_*` symbols to shed the residual
  "Observed"-vs-"hypothesis" wording ambiguity; C4 intentionally changed comments only this pass.

Post-integration corrective gates passed locally: 323 tooling tests; public-tree (420 blobs);
native-dependency (247 files); ledger-format (102 records); Python compile-all; `git diff --check`.

## Rejected / deferred contenders + reasons
- C5 M10 promoted-target allowlist artifact [S+T T] — high strategic value, deferred: accurate
  classification of ~15 CMake targets deserves its own reviewed pass; recorded as top next doc slice.
- Any retail-shaped checkpoint/menu-graph/AI/mission schema — BLOCKED on absent evidence (would be
  fabrication). Honest blocker, not a failure.
- Historical owner-home paths in ledger check commands — owner/Codex policy decision applied during
  integration by replacing only the username segment with the generic `<user>` placeholder.

## Remaining cutscene/menu blockers (honest)
- Cutscene playback is Windows-only and selected through an external `--opening-movie` path; retail-
  intro discovery from game data is absent. Transition reliability also requires a defined practical
  watchdog bound for greater-than-half-clock backward PTS discontinuities (N4) and an ADR-compliant
  fail-open transition with proven containment for post-render movie-audio faults (N5).
- The menu is a synthetic card shell with project-owned glyph rendering. A retail-shaped menu still
  requires privacy-safe retail format/consumer evidence, bounded FNT/GUI/IE or other proven decoders
  and owned IR, an evidence-backed retail font/layout/asset-rendering and binding path, and a tested
  Back/Cancel primitive. Owner-derived evidence remains outside the public tree.

## Resume handle
Campaign branch worktree: checkout `OmegaStrain-claude-campaign` @ `claude/full-spectrum-campaign-20260720`.
Scratch working ledger: the background job's scratch `tmp/campaign-ledger.md`.
