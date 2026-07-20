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

Treat playback as a modal boot sequence owned by a pure reducer. Quit input has priority over the
sequence. A primary-action edge skips the sequence and is not forwarded to the front end on that
frame. EOS, source failure, or the bounded safety duration also enters the front end. Simulation
receives zero elapsed time on every frame that began with the sequence active, preventing a
post-movie scheduler catch-up. CPU and GPU movie resources are released on transition.

External movie playback is mutually exclusive with capture and capture replay. The accepted
milestone presents video only; opening-stream audio remains a separate native media decision.

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

## Constraints

- Production code and committed tests use public format syntax and synthetic fixtures only.
- Private full-stream decode and visual behavior checks remain outside the repository and may not
  become golden assets, logged paths, committed hashes, or runtime assumptions.
- Decoder errors exposed above the media boundary are categorical; HRESULTs, decoder identities,
  paths, and source bytes do not escape.
- The main/render thread owns player calls and GPU uploads. Media Foundation and COM teardown occurs
  on the creating thread.
- Capture and replay reject `--opening-movie` during launch-option validation rather than silently
  changing deterministic run semantics.
- Audio support must preserve the same ownership, bounded-memory, timing, privacy, and fail-open
  properties before it can join this path.

## Validation

- Synthetic MPEG-PS, elementary-stream, H.262, decoder-state, NV12 conversion, boot-reducer, launch
  isolation, texture-update, dependency-policy, and public-tree tests run in CI.
- Windows builds cover the real Media Foundation implementation; Linux builds cover the unsupported
  stub and platform-neutral parser layers.
- Private validation may confirm full-stream decode, presentation, skip, menu transition, resource
  release, and the absence of path-bearing logs without publishing the input or resulting artifacts.

## Primary references

- https://learn.microsoft.com/windows/win32/medfound/mpeg-2-video-decoder
- https://learn.microsoft.com/windows/win32/medfound/basic-mft-processing-model
- https://learn.microsoft.com/windows/win32/medfound/mf-mt-mpeg-sequence-header-attribute
- https://wiki.libsdl.org/SDL3/CategoryGPU
