# ADR 0002: SDL3 platform layer

- Status: accepted
- Date: 2026-07-16
- Pinned baseline: SDL 3.4.10

## Decision

Use SDL3 behind private platform interfaces for the native window, events, gamepads, audio
device/streams, and GPU device/swapchain. The first renderer backend is SDL_GPU, which maps to
modern Direct3D 12, Vulkan, or Metal-style APIs.

Gameplay, simulation, retail-format decoders, and asset types never include SDL headers.
`PlatformService`, `InputService`, `AudioService`, and `RenderService` own the SDL-facing code.

## Reasons

- One zlib-licensed dependency covers the native host surfaces without any PS2 execution code.
- SDL's gamepad API normalizes common controller layouts while retaining hotplug support.
- SDL audio streams provide device migration, conversion, buffering, and mixing primitives.
- SDL_GPU supplies modern 3D/compute resources and command buffers across D3D12, Vulkan, and
  Metal without requiring separate gameplay-facing renderer implementations.

## Constraints

- Pin an exact stable release and update intentionally through a tested dependency change.
- Precompile shipping shaders; runtime shader cross-compilation is a development convenience.
- The render thread owns the GPU device and queue. The game thread submits immutable frames.
- SDL coordinate conventions are a renderer detail; retail importers convert into the engine's
  canonical coordinate system.

## Primary references

- https://github.com/libsdl-org/SDL/releases/tag/release-3.4.10
- https://wiki.libsdl.org/SDL3/CategoryGPU
- https://wiki.libsdl.org/SDL3/CategoryAudio
- https://wiki.libsdl.org/SDL3/CategoryGamepad
