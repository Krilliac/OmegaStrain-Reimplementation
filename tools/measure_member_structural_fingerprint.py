#!/usr/bin/env python3
"""Measure per-suffix structural payload fingerprints for front-end HOG members.

Fixed-schema, aggregate-only, path-free. For each member whose name carries a
frozen front-end suffix, this records only the payload-size structure across the
recursive HOG tree: candidate count, minimum, maximum, distinct-size count, and
the greatest common divisor of the observed payload sizes. It assigns NO field,
layout, alignment, role, or format semantics.

``size_gcd`` is the greatest common divisor of the member payload *sizes*. It is
NOT a payload-address alignment and must never be reported or interpreted as
one; it is a pure arithmetic property of the size multiset.

This is an evidence *collector*, not a decoder. It exists to produce the
member-size structure that the front-end evidence audit records as missing for
``.gui``/``.fnt``/``.ie``. Building and synthetic-testing it is safe; only a run
against the owner corpus (a separate, private step) yields real evidence, and
only real evidence may later justify a semantic decoder. A plausible invented
decoder is a regression.

The bounded, reparse/symlink/case-fold-safe HOG traversal, resource budget, and
typed path-free error vocabulary are reused unchanged from
``measure_frontend_hog_topology`` so both public scanners share one audited
safety core; this tool only adds the per-suffix size aggregation.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Sequence

if __package__:
    from .fingerprint_assets import Span
    from .measure_frontend_hog_topology import (
        ERROR_CATEGORIES,
        AggregateArgumentError,
        AggregateArgumentParser,
        ScanBudget,
        ScanFailure,
        ScanLimits,
        _bounded_hog,
        _discover_root_archives,
        _extension_and_stem,
        _normalize_member_name,
        _open_regular,
        _validate_limits,
    )
else:  # Direct execution adds tools/ rather than the repository root.
    from fingerprint_assets import Span
    from measure_frontend_hog_topology import (
        ERROR_CATEGORIES,
        AggregateArgumentError,
        AggregateArgumentParser,
        ScanBudget,
        ScanFailure,
        ScanLimits,
        _bounded_hog,
        _discover_root_archives,
        _extension_and_stem,
        _normalize_member_name,
        _open_regular,
        _validate_limits,
    )


SCHEMA_VERSION = 1

# Frozen public vocabulary: the three front-end member suffixes the evidence
# audit records with no size/byte/alignment data. Changing this set is a
# deliberate schema change that must update the fixtures and this constant
# together, exactly as the topology scanner treats its own vocabulary.
FROZEN_SUFFIXES = (".fnt", ".gui", ".ie")

TOTAL_FIELDS = (
    "root_archives_discovered",
    "root_archives_scanned",
    "archive_occurrences",
    "matched_member_occurrences",
    "errors",
)


@dataclass
class SuffixFingerprint:
    count: int = 0
    minimum: int | None = None
    maximum: int | None = None
    size_gcd: int = 0
    distinct: set[int] = field(default_factory=set)

    def observe(self, size: int) -> None:
        self.count += 1
        self.minimum = size if self.minimum is None else min(self.minimum, size)
        self.maximum = size if self.maximum is None else max(self.maximum, size)
        self.size_gcd = math.gcd(self.size_gcd, size)
        self.distinct.add(size)

    def document(self) -> dict[str, int]:
        return {
            "count": self.count,
            "size_min": self.minimum if self.minimum is not None else 0,
            "size_max": self.maximum if self.maximum is not None else 0,
            "size_distinct": len(self.distinct),
            "size_gcd": self.size_gcd,
        }


@dataclass
class Aggregate:
    totals: dict[str, int] = field(
        default_factory=lambda: {name: 0 for name in TOTAL_FIELDS}
    )
    suffixes: dict[str, SuffixFingerprint] = field(
        default_factory=lambda: {suffix: SuffixFingerprint() for suffix in FROZEN_SUFFIXES}
    )

    def observe(self, suffix: str, size: int) -> None:
        self.totals["matched_member_occurrences"] += 1
        self.suffixes[suffix].observe(size)

    def document(self) -> dict[str, object]:
        return {
            "schema_version": SCHEMA_VERSION,
            "scope": (
                "fixed-vocabulary aggregate per-suffix member payload-size structure "
                "across recursive HOG containment only; no paths, member names, "
                "hashes, offsets, payload bytes, raw suffixes, per-archive rows, "
                "identifiers, roles, layout, or semantics. size_gcd is the common "
                "divisor of payload sizes, NOT a payload-address alignment"
            ),
            "frozen_suffixes": list(FROZEN_SUFFIXES),
            "totals": {name: int(self.totals[name]) for name in TOTAL_FIELDS},
            "suffix_size_fingerprints": {
                suffix: self.suffixes[suffix].document() for suffix in FROZEN_SUFFIXES
            },
            "error_categories": {category: 0 for category in ERROR_CATEGORIES},
        }


def _scan_archive(
    stream: BinaryIO,
    span: Span,
    depth: int,
    budget: ScanBudget,
    aggregate: Aggregate,
) -> None:
    directory = _bounded_hog(stream, span, depth, budget)
    aggregate.totals["archive_occurrences"] += 1

    seen: set[str] = set()
    nested: list[Span] = []
    for entry in directory.entries:
        normalized = _normalize_member_name(
            entry.name, budget.limits.maximum_name_bytes
        )
        if normalized in seen:
            raise ScanFailure("normalized_collision")
        seen.add(normalized)
        extension, _stem = _extension_and_stem(normalized)
        if extension in aggregate.suffixes:
            aggregate.observe(extension, entry.size)
        if extension == ".hog":
            nested.append(entry)

    for entry in nested:
        _scan_archive(stream, entry, depth + 1, budget, aggregate)


def scan_input(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    _validate_limits(limits)
    budget = ScanBudget(limits)
    roots = _discover_root_archives(root, budget)
    aggregate = Aggregate()
    aggregate.totals["root_archives_discovered"] = len(roots)
    for path in roots:
        with _open_regular(path) as (stream, size):
            _scan_archive(
                stream, Span(name="", offset=0, size=size), 0, budget, aggregate
            )
        aggregate.totals["root_archives_scanned"] += 1
    return aggregate.document()


def failure_document(category: str) -> dict[str, object]:
    document = Aggregate().document()
    document["totals"]["errors"] = 1
    document["error_categories"][category] = 1
    return document


def main(argv: Sequence[str] | None = None) -> int:
    parser = AggregateArgumentParser(add_help=False)
    parser.add_argument("input", type=Path)
    try:
        args = parser.parse_args(argv)
    except AggregateArgumentError:
        result = failure_document("config")
        print(json.dumps(result, separators=(",", ":"), sort_keys=True))
        return 1

    try:
        result = scan_input(args.input)
    except ScanFailure as failure:
        result = failure_document(failure.category)
    except (OSError, ValueError) as _exc:
        del _exc
        result = failure_document("io")
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 1 if result["totals"]["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
