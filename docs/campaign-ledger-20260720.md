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
  shell; NOT an evidence-backed retail menu. No text/glyph render path (H).
- FNT/GUI/IE: E-0101 passive prefix HYPOTHESES; retail provenance of constants UNRECORDED (audit C6).
- Mission loop: only synthetic Position3 locomotion (E-0060). AI absent (grep-confirmed zero hits).
- Engine/SDK: roadmap SDK-0..SDK-8; maturity ladder 0-6 + Exportable. Compatibility-first (ADR 0004).

## Discovery wave — 14 read-only cloud scouts, all domains A-T owned, non-overlapping
Each applied 4 lenses (capability gap / correctness-security / smallest testable slice / evidence
challenge). ~90 candidate microtasks surfaced. Highlights (file:line verified by the scout):

- **A boot/menu:** critical path (movie->menu) is a project-owned scaffold with NO tracked evidence
  tying either endpoint to retail boot. Risk: `omega_app.cpp:1413-1430` escalates any audio-fault
  delta to a fatal error that ends Run — a transient SDL audio hiccup at the movie->menu transition
  can abort the app before the menu. [native-blocked]
- **B front-end formats:** `kFntObserved*`/GUI/IE header constants + "aggregate-observed" comments
  overclaim vs audit C6 ("no tracked source records retail provenance") and A5/C4 (no tracked
  aggregate even contains those per-suffix header fields). [BUILD-INDEPENDENT]
- **B2 menu interactivity:** no Back/Cancel edge distinct from confirm; Profiles confirm ALWAYS
  commits SetActiveProfile so a browser cannot leave without activating. Latent replay hole at
  `run_replay_session.cpp:218` (`!front_end_state_` disjunct bypasses the menu sim-gate). [native]
- **C media:** only Windows MF decode; off-Windows hard-fails with no decoder seam. Watchdog risk:
  `opening_movie_player.cpp:123-150` SafetyDurationTicks can inflate toward UINT64_MAX on malformed
  non-monotonic PTS, defeating fail-open. SShd/48k-stereo PCM constants untracked-provenance (like
  E-0101). [native for fix; provenance note build-independent]
- **D+E vfs/texture:** item-budget divergence `game_data_service.cpp:129` (material path omits
  terrain_cells from initial_items -> can overrun shared budget). VUM payload OOB read
  `vum_render_payload_descriptor.cpp:182-187` (`span_bytes-32U` unsigned underflow for 16<bytes<32).
  Texture->material binding exists in docs only, absent in code. [native for fixes]
- **F+G geometry/anim:** `ska_container_descriptor.cpp:129-165` embeds a hypothesis extent formula
  inside an ACCEPTING validator and rejects non-zero tails against it -> false-negative on unseen
  SKA + doctrine violation (hypothesis-as-fact, unlike scrupulously-quarantined POP strides). [native]
- **H rendering:** no shader/graphics-pipeline or text-glyph path at all (blocks real menu). Risk:
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
- **N1 [P0 correctness]** VUM payload OOB read `vum_render_payload_descriptor.cpp:182-187` — guard
  `span_bytes-32U` underflow + ContainsRange at 0x74/0xF4; add span_bytes 0..64 fixture sweep.
- **N2 [P0 correctness/doctrine]** SKA extent-formula-as-fact `ska_container_descriptor.cpp:129-165`
  — stop rejecting tails against a hypothesis extent; accept-with-relation.
- **N3 [P0 correctness]** item-budget divergence `game_data_service.cpp:129` (material path).
- **N4 [P0 reliability]** watchdog overflow `opening_movie_player.cpp:123-150` (clamp SafetyDurationTicks).
- **N5 [P1 reliability]** audio-fault fatal escalation `omega_app.cpp:1413-1430` (downgrade transient movie-audio faults).
- **N6 [P1 interactivity, menu path]** Back/Cancel reducer edge in `front_end.h` ReduceFrontEnd + oracle/sweep.
- **N7 [P1 safety]** EntityId registry token in `component_store.h` (fail-closed cross-world reuse).
- **N8 [P1 gameplay seam]** pure AABB/proximity-trigger evaluator (independent, synthetic, mirrors debug_locomotion).
- **N9 [P1 media portability]** abstract `IH262Decoder` seam behind the MF impl.
- **N10 [P1 persistence]** persistent `profiles/active` pointer record via existing atomic commit.
- **N11 [P1 replay-parity]** harden `run_replay_session.cpp:218` `!front_end_state_` sim-gate bypass.

## Accepted commits (dependency order)
All four are build-independent, self-verified, DCO-signed, and each passed an independent read-only
adversarial reviewer (promotion wave). Committed to the campaign branch; NOT pushed.

| SHA | Slice | Reviewer verdict |
|-----|-------|------------------|
| `5eebbd8` | C1 tools: file-manifest deterministic total order + self-test | ACCEPT (genuine total order; order-only; docstring now truthful) |
| `a280cfb` | C2 tools: public-tree gate — close fullmatch suffix bypass + case-insensitive owner-home-path scan + tests | ACCEPT (strictly strengthens; no false-positives on 420 blobs; IGNORECASE added per reviewer) |
| `ff9263a` | C3 tools: evidence-ledger format gate + E-0095 normalization | ACCEPT (E-0095 change provably whitespace-only; lint passes 102 records, rejects each malformed class) |
| `4e7b0ff` | C4 retail: FNT/GUI/IE prefix constants labeled project-defined hypotheses (cites audit C6) | ACCEPT (near-verbatim to audit C6/C8/A5/C4; comment-only; consistent) |

Gates run green on the assembled tree: 315 tooling tests; public-tree (420 blobs); native-dependency
(247 files); ledger-format (102 records); Python compile-all; `git diff --check`; DCO (4/4 commits).

### Reviewer-noted non-blocking follow-ups (applied or deferred)
- APPLIED: C2 added `re.IGNORECASE` (catches `C:\\users`, `/HOME/` variants; placeholders still spared).
- APPLIED: C3 error string corrected to `[A-Z]-####` (matches the accepting regex).
- DEFERRED (needs a build): rename `kFntObserved*`/`observed_word_*` symbols to shed the residual
  "Observed"-vs-"hypothesis" wording ambiguity; C4 intentionally changed comments only this pass.
- DEFERRED (edge, no live impact): ledger-format gate uses `read_text` universal-newlines, so a CRLF
  ledger would pass; tracked ledger is LF.

## Rejected / deferred contenders + reasons
- C5 M10 promoted-target allowlist artifact [S+T T] — high strategic value, deferred: accurate
  classification of ~15 CMake targets deserves its own reviewed pass; recorded as top next doc slice.
- Any retail-shaped checkpoint/menu-graph/AI/mission schema — BLOCKED on absent evidence (would be
  fabrication). Honest blocker, not a failure.
- Auto-scrubbing `hog-headers.jsonl` / ledger identity strings — a content-policy decision for the
  owner/Codex, not a coordinator auto-edit; surfaced as a finding instead.

## Remaining cutscene/menu blockers (honest)
- Cutscene is Windows-only (no decoder seam) and driven by an external `--opening-movie` file, not
  retail intro discovered from game data. => "owned opening cutscene" is currently "owned Windows cutscene".
- Menu is a synthetic card shell with no text/glyph render path (H) and no evidence-backed retail
  menu graph / FNT-GUI-IE decode (B). A genuinely interactive retail menu is gated on: (a) a text
  render path, (b) a Back/Cancel primitive, (c) evidence tying any menu graph to retail — the last
  requires a privacy-safe owner-corpus/consumer-trace pass done OUTSIDE this public tree.

## Resume handle
Campaign branch worktree: checkout `OmegaStrain-claude-campaign` @ `claude/full-spectrum-campaign-20260720`.
Scratch working ledger: the background job's scratch `tmp/campaign-ledger.md`.
