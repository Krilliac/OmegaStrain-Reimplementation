# ADR 0003: Native opening-movie boundary

- Status: accepted
- Date: 2026-07-20

## Decision

Accept an opening movie only through the explicit `--opening-movie=PATH` launch option. The native
runtime does not search for media, persist the supplied path, or include it in diagnostics.

Inspect the external MPEG-2 Program Stream through bounded, renderer-neutral descriptors. Borrowed
video payload ranges are fed incrementally to a narrow Windows Media Foundation H.262 decoder. The
decoder publishes owned NV12 frames; the app converts due frames to opaque RGBA8 and updates one
stable SDL GPU texture in place. Non-Windows builds retain the same public boundary through a
fail-closed unsupported decoder stub, and the app fails open to the existing front end.

Accept opening-stream audio only through the validated PSS `private_stream_1` PCM variant currently
required by this path: complete `SShd`/`SSbd` framing, encoding tag 1, signed 16-bit little-endian
samples, 48,000 Hz stereo, and complete channel-interleave rounds. The media layer retains an
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

External movie playback is mutually exclusive with capture and capture replay. This decision adds
only opening-movie PCM presentation; it does not establish a general audio mixer, support other PSS
audio variants, or claim retail audiovisual timing parity.

## Reasons

- The runtime remains a clean-room native implementation and links no PS2 execution or emulator
  code.
- Explicit caller selection keeps proprietary inputs outside discovery, configuration, saves, and
  version control.
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
- Capture and replay reject `--opening-movie` during launch-option validation rather than silently
  changing deterministic run semantics.
- PSS inspection and PCM deinterleave remain platform-neutral media code with no SDL types. SDL
  playback remains an app/platform leaf, and Windows Media Foundation remains the only production
  H.262 decoder. Therefore non-Windows builds do not claim end-to-end movie decode or presentation.
- The fixed 48 kHz stereo acceptance rule is an opening-movie compatibility boundary, not a general
  native mix format or an assertion about every retail PSS stream.
- Audio callback, queue, underrun, clock, or containment failures fail open to the front end and may
  not leave the device running or retain queued proprietary samples.

## Validation

- Synthetic MPEG-PS, elementary-stream, PSS PCM plan/deinterleave, H.262, decoder-state, NV12
  conversion, opening-audio queue/drain/discard, isolated audio-clock, boot-reducer, launch
  isolation, texture-update, dependency-policy, and public-tree tests run in CI.
- Windows builds cover the real Media Foundation implementation; Linux builds cover the unsupported
  H.262 stub and platform-neutral parser layers, not a non-Windows movie-playback claim.
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
