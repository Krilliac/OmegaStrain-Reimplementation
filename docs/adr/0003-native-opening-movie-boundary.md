# ADR 0003: Native opening-movie boundary

- Status: accepted
- Date: 2026-07-20

## Decision

Accept an opening movie only through one of two mutually exclusive launch options:
`--opening-movie=PATH` for an external file, or `--opening-movie-member=NAME` for one exact member
of the fixed `ZMEDIA/ZMOVIES.HOG` game-data archive. The archive-backed route uses the frozen
`GameDataService` and a one-component `SourceLocator`; it indexes only the top-level HOG directory
and reads only the explicitly named payload. It does not enumerate candidates, search other
archives, apply a filename fallback, or infer an automatic intro member. Neither selector is
persisted or included in diagnostics.

Both inputs become the same identity-free, move-only `OpeningMovieSource` before media inspection.
It owns at most 512 MiB and retains no host path, archive path, or member name. The app consumes that
owner exactly once. Missing, malformed, oversized, unavailable, or decoder-rejected archive-backed
input logs only a fixed category and fails open to the persistence-derived Profiles surface.

Inspect the selected MPEG-2 Program Stream through bounded, renderer-neutral descriptors. Borrowed
video payload ranges are fed incrementally to a narrow Windows Media Foundation H.262 decoder. The
decoder publishes owned NV12 frames; the app converts due frames to opaque RGBA8 and updates one
stable SDL GPU texture in place. Non-Windows builds retain the same public boundary through a
fail-closed unsupported decoder stub, and the app fails open to the existing front end.

Accept opening-stream audio only through the project-defined provisional PSS `private_stream_1` PCM
compatibility hypothesis currently implemented by this path: complete `SShd`/`SSbd` framing,
encoding tag 1, signed 16-bit little-endian samples, 48,000 Hz stereo, and complete
channel-interleave rounds. The media layer retains an
offset-only plan into the bounded owned source and deinterleaves exact frame intervals into
caller-owned PCM16 without allocation. The main thread refills a fixed-capacity SDL audio ring; the
project callback code converts no media and performs no file access, logging, explicit locking, or
dynamic allocation. SDL internals are outside that project-code claim.

Treat playback as a modal boot sequence owned by a pure reducer. Quit input has priority over the
sequence. A primary-action edge skips the sequence and is not forwarded to the front end on that
frame. EOS, source failure, or the bounded safety duration also enters the front end. Simulation
receives zero elapsed time on every frame that began with the sequence active, preventing a
post-movie scheduler catch-up. The first video frame must exist before PCM can be queued. Once the
first bounded PCM queue is accepted, a project-owned 48 kHz device-demand timeline supplies exact
elapsed intervals to the video presentation clock. Completion waits for decoded video completion,
the validated PCM source to be returned, and the audio ring to drain. Skip, completion, and any
presentation failure pause and clear movie audio before CPU and GPU movie resources are released.
Ring drain is a project queue observation; it does not prove that SDL conversion backlog or the final
hardware samples have completed playback.

Generated fixtures establish the parser and deinterleaver's safety and self-consistency. E-0102's
bounded owner-stream smoke establishes only that one external stream passed this structural gate;
it does not independently establish the custom field meanings, tag-1 PCM16LE mapping, all-ones loop
interpretation, four-byte selector handling, or channel-block order. Those semantics remain
provisional pending a sanitized metadata check and independent behavioral comparison.

Opening-movie playback is mutually exclusive with capture and capture replay. This decision adds
only opening-movie PCM presentation; it does not establish a general audio mixer, support other PSS
audio variants, or claim retail audiovisual timing parity.

## Reasons

- The runtime remains a clean-room native implementation and links no PS2 execution or emulator
  code.
- Explicit caller selection keeps proprietary inputs outside discovery, configuration, saves, and
  version control; automatic intro selection remains blocked on private owner-side observation.
- Hard parser, input, frame, output, and queue ceilings bound hostile or malformed media.
- A platform decoder behind a generic media boundary avoids importing Windows types into app,
  runtime, gameplay, or renderer interfaces.
- Fail-open boot policy keeps malformed or unsupported media from blocking the native menu.
- Stable texture identity avoids per-frame GPU resource churn while preserving exact size checks.
- A fixed single-producer audio ring bounds callback-visible state and keeps media parsing and PCM
  deinterleave off the audio thread.
- Advancing presentation from observed device demand prevents a slow or fast host loop from becoming
  the audiovisual clock once audio starts, without treating that synthetic policy as retail fact.

## Constraints

- Production code and committed tests use public format syntax and synthetic fixtures only.
- Private full-stream decode and visual behavior checks remain outside the repository and may not
  become golden assets, logged paths, committed hashes, or runtime assumptions.
- Decoder errors exposed above the media boundary are categorical; HRESULTs, decoder identities,
  paths, and source bytes do not escape.
- The main/render thread owns player calls, PCM decode/refill, audio control, and GPU uploads. Media
  Foundation and COM teardown occurs on the creating thread. The SDL callback may consume only the
  fixed ring and lock-free counters.
- Capture and replay reject both opening-movie selectors during launch-option validation rather
  than silently changing deterministic run semantics. The two selectors also reject each other.
- PSS inspection and PCM deinterleave remain platform-neutral media code with no SDL types. SDL
  playback remains an app/platform leaf, and Windows Media Foundation remains the only production
  H.262 decoder. Therefore non-Windows builds do not claim end-to-end movie decode or presentation.
- The fixed 48 kHz stereo acceptance rule is an opening-movie compatibility boundary, not a general
  native mix format or an assertion about every retail PSS stream.
- The PSS PCM grammar is a project-defined provisional compatibility hypothesis, not confirmed
  retail format semantics. Generated fixtures and one accepted owner stream do not promote it.
- Audio callback, queue, underrun, or clock failures during the modal movie window fail open to the
  front end after the runtime has paused and cleared movie audio. If containment itself fails, the
  run ends after releasing CPU/GPU movie state; the runtime must not enter an interactive front end
  while the device may still be running or retaining queued proprietary samples.

## Validation

- Synthetic archive-member lookup (including mixed case, missing/malformed input, and a sparse
  512 MiB plus one byte member), move-only ownership, path/owned-source rejection parity, MPEG-PS,
  elementary-stream, PSS PCM plan/deinterleave, H.262, decoder-state, NV12 conversion,
  opening-audio queue/drain/discard, isolated audio-clock and audio-fault policy,
  boot-reducer, launch
  isolation, texture-update, dependency-policy, and public-tree tests run in CI. The PSS PCM
  fixtures validate implemented boundaries and self-consistency, not independent format provenance.
- Windows builds cover the real Media Foundation implementation; Linux builds cover the unsupported
  H.262 stub and platform-neutral parser layers, not a non-Windows movie-playback claim.
- An opt-in generated app-boundary smoke drives one RGBA frame and bounded silent PCM through the
  actual app services. It covers natural EOS into Main plus next-frame navigation, primary skip at
  zero, two, and five advances, modal zero-simulation behavior, playback/texture/draw/audio
  containment, real queue-rejection and control faults raised after the pre-render health sample,
  one-shot fault re-baselining, and path/member-free categorical failure logging. A separate owned
  malformed-source case exercises production `OpeningMoviePlayer::Create` failure and proves the
  empty-persistence route remains on Profiles without publishing movie resources. Generated
  playback injection still bypasses production decode, so it does not establish Media Foundation
  teardown, finite-source PCM or hardware-backlog drain, perceptual synchronization, retail timing,
  or repeated owner validation.
- Private validation may confirm full-stream audiovisual presentation, skip, drain, menu transition,
  resource containment, and the absence of path-bearing logs without publishing the input or
  resulting artifacts. Such observation does not by itself establish frame-exact retail timing
  parity.

## Primary references

- https://learn.microsoft.com/windows/win32/medfound/mpeg-2-video-decoder
- https://learn.microsoft.com/windows/win32/medfound/basic-mft-processing-model
- https://learn.microsoft.com/windows/win32/medfound/mf-mt-mpeg-sequence-header-attribute
- https://wiki.libsdl.org/SDL3/CategoryGPU
- https://wiki.libsdl.org/SDL3/CategoryAudio
