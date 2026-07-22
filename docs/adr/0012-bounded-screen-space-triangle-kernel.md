# ADR 0012: Bounded screen-space triangle kernel

- Status: Accepted
- Date: 2026-07-22

## Context

The retail-derived front-end compositor needs a small amount of raster behavior that is supported by
behavioral evidence, while projection, texture sampling, blending, depth, primitive state, and other
render semantics remain separate research questions. Folding those open questions into a full
software renderer would make a successful draw look more authoritative than the evidence permits.

The compositor also cannot allocate per pixel, retain a framebuffer, or accept an unbounded clip or
attribute set. A caller must be able to tighten every work budget, and a failure must not publish an
apparently valid prefix unless the caller explicitly asks the visitor to stop.

## Decision

`omega_frontend_triangle_kernel` is a platform-neutral leaf target with a single pure traversal API.
It accepts exactly three already-projected screen-space vertices, a half-open integer clip rectangle,
caller-tightenable limits, and a function-pointer visitor. Input spans are snapshotted into bounded
fixed stack storage before traversal; no input view escapes the call.

Coverage follows the established ceil scan conversion at integer-lattice sample positions. Scan rows
and both edge intersections are ceiled, making top and left inclusive and bottom and right exclusive.
Winding does not change coverage. Samples are delivered deterministically in increasing y and then
increasing x.

The kernel plane-interpolates a bounded generic affine-channel span and the explicit S, T, and Q
channels. It reports both interpolated S/T/Q and S/Q plus T/Q. Zero or non-finite division results are
typed perspective-division failures; the kernel does not invent an epsilon. It preflights all covered
samples and the pixel budget before the first visitor call, so geometry, arithmetic, perspective, and
limit failures expose no partial callback prefix.

Hard ceilings cap clip dimensions, covered pixels, coordinate magnitude, and affine-channel count.
Callers may only lower them. The implementation uses fixed stack arrays, owns no framebuffer, makes
no service or filesystem calls, and allocates neither per triangle nor per pixel.

## Deliberate non-semantics

This boundary does not implement or assign:

- world, view, viewport, or GS projection;
- texture lookup, filtering, addressing, TCC, FST, or texture-function state;
- color combination, alpha blending, PABE/ABE, or output-alpha behavior;
- depth testing, depth writes, scissoring beyond the explicit host clip, or draw submission; or
- primitive selection, culling, retail ordering, animation, or full Title composition.

Those behaviors remain separate evidence-gated modules. A successful triangle traversal is only a
bounded screen-space coverage and interpolation result; it is not a canonical retail frame.

## Ownership and thread contract

The caller owns input channel arrays and the visitor context. The kernel borrows them for one call,
copies all vertex values before executing the visitor, and retains nothing. Each sample is a fixed
owned value whose reference is valid only during the callback and may be copied by the caller.

The entry point is stateless, reentrant, callable from any ordinary thread, and hot-reload-safe as a
leaf boundary. It is not suitable for an audio callback or another restricted callback because the
caller-supplied visitor is outside the kernel's control.
