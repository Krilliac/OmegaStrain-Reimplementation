# SKAS structural text envelope

## Evidence boundary

The public fingerprint corpus records two SKAS candidates only as aggregate text-shape
measurements. Both candidates fall within the following closed envelope:

- physical span: 5,132 through 5,156 bytes;
- logical text: 5,129 through 5,155 bytes;
- trailing zero padding: one through three bytes;
- logical bytes: printable ASCII `0x20` through `0x7e` plus paired CRLF terminators;
- exactly 72 CRLF-terminated lines, including exactly five empty lines; and
- exactly 67 lines containing exactly one colon.

These are independent aggregate ranges and counts. They do not publish source identity or
payload text and do not establish any label, value, record, relationship, animation, skeleton,
or SKA-association semantics.

## Native contract

`omega::retail::DecodeSkasTextEnvelope` is a stateless, reentrant structural decoder. It accepts
only the complete envelope above. The result owns the exact logical text, including all CRLF
pairs, and a source-order array of opaque byte ranges that address each line's content and
two-byte terminator. It also retains only the zero-padding, blank-line, and single-colon-line
counts. The zero padding itself is validated and omitted.

The decoder deliberately does not split a line at its colon. A colon may occur at either end of a
nonempty line because the aggregate evidence establishes a count, not a key/value grammar.

## Resource limits

The fixed decoder ceilings are:

- 5,156 physical input bytes;
- 72 line records plus one root item;
- 5,155 owned logical-text bytes plus the root and 72 fixed-size line records;
- zero dynamic scratch bytes; and
- root nesting depth zero.

Caller `DecodeLimits` may tighten input, item, and owned-output budgets but cannot widen the fixed
envelope. Because the proven logical text exceeds the shared 4,096-byte string default, the
decoder's format-scoped default preserves every other shared limit but sets its string budget to
the fixed 5,155-byte ceiling. An explicitly supplied caller string budget can only tighten it. Both
owning buffers are constructed at their final sizes inside the exception boundary; allocation and
representation failures become typed, path-free decode errors.

## Non-claims

This slice does not implement or infer:

- label or value syntax, types, defaults, or meanings;
- whitespace normalization, comment rules, escaping, or alternate line endings;
- a relationship to any SKA, SKL, SKM, actor, animation, or runtime object;
- lookup, selection, playback, rendering, simulation, or gameplay behavior; or
- retail behavioral or emulator equivalence.

Tests use project-generated printable text only. No owner file, proprietary byte, path, filename,
disc image, executable, save, savestate, emulator state, or PCSX2 input is embedded or required.
