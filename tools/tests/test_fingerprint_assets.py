from __future__ import annotations

import collections
import io
import json
import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import fingerprint_assets as fingerprints  # noqa: E402


def make_ska(
    *,
    version: int = 3,
    word_0x04: int = 2,
    word_0x08: int = 56,
    word_0x10: int = 1,
    tail_bytes: int = 0,
    dirty_tail: bool = False,
) -> bytes:
    logical_size = 112 + 4 * word_0x08 * (word_0x04 + int(word_0x10 == 0))
    data = bytearray(logical_size + tail_bytes)
    struct.pack_into("<5I", data, 0, version, word_0x04, word_0x08, 0, word_0x10)
    if dirty_tail:
        data[-1] = 1
    return bytes(data)


def ska_stats(data: bytes) -> dict[str, object]:
    stats = fingerprints.Aggregate()
    fingerprints.fingerprint_ska(
        io.BytesIO(data), fingerprints.Span("synthetic.ska", 0, len(data)), stats
    )
    return stats.as_dict()


def skas_stats(data: bytes, declared_size: int | None = None) -> dict[str, object]:
    stats = fingerprints.Aggregate()
    fingerprints.fingerprint_skas(
        io.BytesIO(data),
        fingerprints.Span(
            "synthetic.skas",
            0,
            len(data) if declared_size is None else declared_size,
        ),
        stats,
    )
    return stats.as_dict()


class SkaFingerprintTests(unittest.TestCase):
    def test_counted_word_formula_accepts_both_observed_layout_branches(self) -> None:
        for word_0x10, expected_size in ((1, 560), (0, 784)):
            with self.subTest(word_0x10=word_0x10):
                result = ska_stats(make_ska(word_0x10=word_0x10))
                self.assertEqual(result["computed_counted_word_span_exact"], 1)
                self.assertEqual(
                    result["computed_logical_bytes_range"],
                    [expected_size, expected_size],
                )

    def test_counted_word_extent_classifies_zero_and_nonzero_tails(self) -> None:
        zero_tail = ska_stats(make_ska(tail_bytes=16))
        self.assertEqual(zero_tail["computed_counted_word_span_zero_padded"], 1)
        self.assertEqual(zero_tail["computed_counted_word_span_padding_bytes_range"], [16, 16])

        dirty_tail = ska_stats(make_ska(tail_bytes=16, dirty_tail=True))
        self.assertEqual(dirty_tail["computed_counted_word_span_nonzero_tail"], 1)

    def test_counted_word_extent_reports_exceeds_span(self) -> None:
        data = bytearray(112)
        struct.pack_into("<5I", data, 0, 3, 2, 56, 0, 1)
        result = ska_stats(bytes(data))
        self.assertEqual(result["computed_counted_word_span_exceeds_span"], 1)

    def test_invalid_ska_headers_stop_before_extent_classification(self) -> None:
        cases = {
            "short": bytes(111),
            "version": make_ska(version=2),
            "zero_word_0x04": make_ska(word_0x04=0),
            "zero_word_0x08": make_ska(word_0x08=0),
            "word_0x10": make_ska(word_0x10=2),
        }
        expected = {
            "short": "short_header",
            "version": "unsupported_version_word",
            "zero_word_0x04": "zero_counted_word",
            "zero_word_0x08": "zero_counted_word",
            "word_0x10": "unsupported_observed_word_0x10",
        }
        for name, data in cases.items():
            with self.subTest(name=name):
                result = ska_stats(data)
                self.assertEqual(result[expected[name]], 1)
                self.assertFalse(
                    any(
                        key.startswith("computed_counted_word_span_")
                        for key in result
                    )
                )

    def test_skas_text_shape_is_aggregate_only(self) -> None:
        result = skas_stats(b"A:B\r\n\r\nC:D\r\n" + b"\0" * 3)
        self.assertEqual(result["ascii"], 1)
        self.assertEqual(result["printable_ascii_with_line_endings"], 1)
        self.assertEqual(result["crlf_only"], 1)
        self.assertEqual(result["ends_with_crlf"], 1)
        self.assertEqual(result["line_count_range"], [3, 3])
        self.assertEqual(result["blank_line_count_range"], [1, 1])
        self.assertEqual(result["single_colon_line_count_range"], [2, 2])
        self.assertEqual(result["padding_bytes_range"], [3, 3])

    def test_skas_rejects_internal_nul_non_ascii_and_other_control_bytes(self) -> None:
        self.assertEqual(skas_stats(b"A\0B\r\n\0")["internal_nul"], 1)
        self.assertEqual(skas_stats(b"\xff\r\n\0")["non_ascii"], 1)
        self.assertEqual(
            skas_stats(b"A\tB\r\n\0")["ascii_outside_text_envelope"], 1
        )

    def test_skas_size_limit_is_checked_before_reading(self) -> None:
        result = skas_stats(b"", declared_size=1_048_577)
        self.assertEqual(result["too_large_for_text_validation"], 1)

    def test_registered_scans_are_deterministic_and_do_not_publish_names(self) -> None:
        ska = make_ska()
        skas = b"A:B\r\n\0"
        combined = ska + skas
        spans = (
            fingerprints.Span("PRIVATE_SENTINEL.ska", 0, len(ska)),
            fingerprints.Span("SECRET_SENTINEL.skas", len(ska), len(skas)),
        )

        def scan(order: tuple[int, int]) -> str:
            formats: dict[str, fingerprints.Aggregate] = collections.defaultdict(
                fingerprints.Aggregate
            )
            aggregate = fingerprints.Aggregate()
            file = io.BytesIO(combined)
            for index in order:
                fingerprints.scan_asset(file, spans[index], 0, formats, aggregate)
            result = {
                "formats": {
                    extension: stats.as_dict()
                    for extension, stats in sorted(formats.items())
                },
                "scan": aggregate.as_dict(),
            }
            return json.dumps(result, sort_keys=True)

        forward = scan((0, 1))
        self.assertEqual(forward, scan((1, 0)))
        self.assertNotIn("PRIVATE_SENTINEL", forward)
        self.assertNotIn("SECRET_SENTINEL", forward)
        self.assertIs(fingerprints.FORMAT_HANDLERS[".ska"], fingerprints.fingerprint_ska)
        self.assertIs(fingerprints.FORMAT_HANDLERS[".skas"], fingerprints.fingerprint_skas)


if __name__ == "__main__":
    unittest.main()
