# ADR 0011: Retail-derived player presentation

- Status: Accepted
- Date: 2026-07-22

## Context

ADR 0010 separates the native desktop launcher from `openomega.exe`. The launcher is intentionally
project-authored application UI, but the game process still contains diagnostic cards, contact-sheet
geometry, synthetic actors, and project-authored menu renderers used while native systems were being
bootstrapped. Those tools are useful for development, but they are not the canonical game and must
not be presented as though they were reconstructed retail screens or gameplay.

The owner-provided NTSC-U directory or ISO contains the opening media, front-end screen bundles,
fonts, textures, models, levels, audio, and other presentation inputs. The native runtime must decode
those inputs without embedding proprietary payloads or executing translated retail code.

## Decision

`openomega_launcher.exe` is the only normal project-authored product UI. After it starts
`openomega.exe`, every player-visible or player-audible game presentation must be derived from
validated assets in the selected retail data source. This includes opening media, title and agent
screens, text, fonts, icons, animation, models, levels, materials, HUD, audio, and briefing or mission
presentation.

Native code owns bounded file access, format decoding, canonical intermediate representations,
rendering, audio playback, input mapping, persistence, and clean-room behavior. Menu transitions and
other behavior that resides in the retail executable are reimplemented from static or behavioral
evidence; executable instructions are never interpreted, recompiled, translated, or shipped.

Normal presentation is fail-closed:

- a player-facing frame or audio submission must carry retail-decoded provenance from the current
  immutable `GameDataService` source;
- missing, malformed, unsupported, or incomplete required assets produce a sanitized OpenOmega
  decode/startup error before any substitute presentation is submitted;
- opening-media absence may skip to a validated retail title bundle, but it may not fall through to a
  synthetic menu; and
- no filename guess, generated card, diagnostic texture, contact-sheet scene, synthetic actor, or
  project-authored tactical renderer is an automatic fallback.

Project diagnostics may remain available only through an explicit developer-only launch mode or a
separate Visual Studio debug target that the normal launcher does not expose. Developer presentation
must be visibly identified as diagnostic and its render resources must not satisfy retail-provenance
checks.

Retail format code remains under `omega::retail`. It converts bounded, independently owned source
bytes into project-owned canonical IR. Retail paths, archive offsets, borrowed spans, and proprietary
payloads do not cross the decode boundary or enter version control. The initial front-end pipeline is
non-hot-reloadable: `GameDataService` owns the frozen source, worker-safe decoders produce owned IR,
and the main/render thread owns GPU resources and draw submission.

## Consequences

- The current synthetic front end and diagnostic gameplay are development tools, not product
  progress toward visual parity. They must be gated before a normal game build is published.
- The first playable post-movie milestone requires evidence-backed screen-bundle, GUI/IE, font, and
  display-texture decoding; changing only the renderer or copying the visual style is insufficient.
- Generated fixtures prove parser bounds and behavior. Owner-corpus checks remain private and publish
  only reviewed, sanitized evidence; proprietary bytes and host paths remain outside the repository.
- Tests must prove directory/ISO parity, immutable source identity, decode limits, owned output
  lifetime, path-free failures, and rejection of diagnostic resources in normal presentation.
- Platform window chrome and clearly branded startup/error dialogs are application surfaces rather
  than canonical game content and may remain project-authored.

## Non-goals

- Recreating retail menus from screenshots, approximating them with project art, or presenting a
  fallback as canonical.
- Shipping PCSX2, a MIPS interpreter/recompiler, translated retail code, or proprietary game data.
- Assigning semantics to FNT, GUI, IE, TDX, or other fields before independent consumer evidence
  supports those meanings.
