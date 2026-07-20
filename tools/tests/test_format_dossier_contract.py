from __future__ import annotations

import json
import re
import subprocess
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DOSSIER_ROOT = REPOSITORY_ROOT / "analysis" / "formats" / "dossiers"
CATALOG_PATH = DOSSIER_ROOT / "catalog.json"

EXPECTED_MATRIX = {
    ".col": "canonical_decoder",
    ".hog": "canonical_decoder",
    ".pop": "canonical_decoder",
    ".tdx": "canonical_decoder",
    ".vag": "canonical_decoder",
    ".vum": "canonical_decoder",
    ".lpd": "structural_envelope_only",
    ".par": "structural_envelope_only",
    ".skas": "structural_envelope_only",
    ".vpk": "structural_envelope_only",
    ".ska": "passive_descriptor_only",
    ".skl": "passive_descriptor_only",
    ".skm": "passive_descriptor_only",
    ".bin": "aggregate_scanner_only",
    ".bnk": "aggregate_scanner_only",
    ".bon": "aggregate_scanner_only",
    ".fnt": "aggregate_scanner_only",
    ".gui": "aggregate_scanner_only",
    ".gun": "aggregate_scanner_only",
    ".ie": "aggregate_scanner_only",
    ".prn": "aggregate_scanner_only",
    ".pss": "aggregate_scanner_only",
    ".scc": "aggregate_scanner_only",
    ".skel": "aggregate_scanner_only",
    ".skf": "aggregate_scanner_only",
    ".so": "aggregate_scanner_only",
    ".sub": "aggregate_scanner_only",
    ".txt": "aggregate_scanner_only",
    ".pf": "unknown",
    ".tbl": "unknown",
    ".tm2": "unknown",
}

EXPECTED_WHOLE_DISC = {
    ".64",
    ".bd",
    ".cnf",
    ".dat",
    ".elf",
    ".hd",
    ".icn",
    ".ico",
    ".img",
    ".ini",
    ".irx",
    ".log",
    ".map",
    ".rgb",
    ".sys",
    ".wdb",
}

EXPECTED_PARTITIONS = {
    "canonical_decoder": 6,
    "structural_envelope_only": 4,
    "passive_descriptor_only": 3,
    "aggregate_scanner_only": 15,
    "unknown": 3,
    "whole_disc_disposition": 16,
}

REQUIRED_SECTIONS = (
    "occurrence evidence",
    "confirmed facts",
    "aggregate-only facts",
    "hypotheses",
    "missing observations",
    "decoder/tooling status",
    "codex work order",
)

WINDOWS_RESERVED_STEMS = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    *(f"COM{index}" for index in range(1, 10)),
    *(f"LPT{index}" for index in range(1, 10)),
}


class FormatDossierContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        cls.families = cls.catalog["families"]
        cls.by_suffix = {entry["suffix"]: entry for entry in cls.families}

    def test_catalog_matches_all_tracked_occurrence_inventories(self) -> None:
        fingerprints = json.loads(
            (REPOSITORY_ROOT / "analysis/formats/asset-fingerprints.json").read_text(
                encoding="utf-8"
            )
        )
        hog_validation = json.loads(
            (REPOSITORY_ROOT / "analysis/formats/hog-validation.json").read_text(
                encoding="utf-8"
            )
        )
        disc_summary = json.loads(
            (REPOSITORY_ROOT / "analysis/manifests/disc-summary.json").read_text(
                encoding="utf-8"
            )
        )
        observed = (
            set(fingerprints["scan"]["extensions"])
            | set(hog_validation["entry_extensions"])
            | set(disc_summary["extensions"])
        )
        self.assertEqual(len(self.families), 47)
        self.assertEqual(len(self.by_suffix), 47)
        self.assertEqual(set(self.by_suffix), observed)

    def test_catalog_freezes_authoritative_matrix_and_whole_disc_partition(self) -> None:
        matrix = {
            suffix: entry["decoder_class"]
            for suffix, entry in self.by_suffix.items()
            if entry["scope"] == "decoder_matrix"
        }
        whole_disc = {
            suffix
            for suffix, entry in self.by_suffix.items()
            if entry["scope"] == "whole_disc_disposition"
        }
        self.assertEqual(matrix, EXPECTED_MATRIX)
        self.assertEqual(whole_disc, EXPECTED_WHOLE_DISC)
        self.assertTrue(
            all(self.by_suffix[suffix]["decoder_class"] is None for suffix in whole_disc)
        )

    def test_dossier_files_are_complete_unique_and_windows_safe(self) -> None:
        catalog_files = {entry["dossier"] for entry in self.families}
        actual_files = {
            path.name for path in DOSSIER_ROOT.glob("*.md") if path.name != "INDEX.md"
        }
        self.assertEqual(len(catalog_files), 47)
        self.assertEqual(actual_files, catalog_files)
        for filename in catalog_files:
            path = DOSSIER_ROOT / filename
            self.assertTrue(path.is_file())
            self.assertNotIn(path.stem.upper(), WINDOWS_RESERVED_STEMS)
            self.assertEqual(filename, filename.rstrip(". "))

    def test_every_dossier_has_the_required_evidence_sections(self) -> None:
        for entry in self.families:
            path = DOSSIER_ROOT / entry["dossier"]
            headings = []
            for line in path.read_text(encoding="utf-8").splitlines():
                match = re.match(r"^##\s+(?:\d+\.\s*)?(.*)$", line)
                if match:
                    headings.append(match.group(1).strip().lower())
            for required in REQUIRED_SECTIONS:
                with self.subTest(dossier=path.name, section=required):
                    self.assertTrue(
                        any(heading.startswith(required) for heading in headings),
                        f"{path.name} lacks {required!r}",
                    )

    def test_index_partition_counts_and_links_match_catalog(self) -> None:
        text = (DOSSIER_ROOT / "INDEX.md").read_text(encoding="utf-8")
        matches = list(
            re.finditer(r"(?m)^###\s+([a-z_]+)\s+\((\d+)\)\s*$", text)
        )
        found: dict[str, set[str]] = {}
        for index, match in enumerate(matches):
            category = match.group(1)
            end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
            block = text[match.end() : end]
            files = set(re.findall(r"\]\(([^/)]+\.md)\)", block))
            self.assertEqual(int(match.group(2)), EXPECTED_PARTITIONS[category])
            self.assertEqual(len(files), EXPECTED_PARTITIONS[category])
            found[category] = files

        self.assertEqual(set(found), set(EXPECTED_PARTITIONS))
        expected_files: dict[str, set[str]] = {
            category: {
                entry["dossier"]
                for entry in self.families
                if entry["decoder_class"] == category
            }
            for category in EXPECTED_MATRIX.values()
        }
        expected_files = {
            category: files for category, files in expected_files.items() if files
        }
        expected_files["whole_disc_disposition"] = {
            entry["dossier"]
            for entry in self.families
            if entry["scope"] == "whole_disc_disposition"
        }
        self.assertEqual(found, expected_files)

    def test_catalog_sources_and_local_markdown_links_resolve(self) -> None:
        tracked = set(
            subprocess.check_output(
                ["git", "-C", str(REPOSITORY_ROOT), "ls-files"], text=True
            ).splitlines()
        )
        for source in (
            self.catalog["authoritative_decoder_matrix"],
            *self.catalog["source_inventories"],
        ):
            path = (DOSSIER_ROOT / source).resolve()
            self.assertTrue(path.is_file())
            relative = path.relative_to(REPOSITORY_ROOT).as_posix()
            self.assertIn(relative, tracked)

        for markdown in DOSSIER_ROOT.glob("*.md"):
            text = markdown.read_text(encoding="utf-8")
            for target in re.findall(r"\[[^\]]+\]\(([^)]+)\)", text):
                if "://" in target or target.startswith("#"):
                    continue
                clean_target = target.split("#", 1)[0]
                with self.subTest(source=markdown.name, target=target):
                    self.assertTrue((markdown.parent / clean_target).resolve().is_file())

    def test_promotion_checklist_occurrence_table_matches_inventories(self) -> None:
        text = (
            REPOSITORY_ROOT / "docs/native-scaffolds/README.md"
        ).read_text(encoding="utf-8")
        rows = {
            suffix: (int(recursive), int(top_level))
            for suffix, recursive, top_level in re.findall(
                r"(?m)^\| `(\.[^`]+)` \| (\d+) \| (\d+) \|$", text
            )
        }
        fingerprints = json.loads(
            (REPOSITORY_ROOT / "analysis/formats/asset-fingerprints.json").read_text(
                encoding="utf-8"
            )
        )["scan"]["extensions"]
        top_level = json.loads(
            (REPOSITORY_ROOT / "analysis/formats/hog-validation.json").read_text(
                encoding="utf-8"
            )
        )["entry_extensions"]
        self.assertEqual(
            rows,
            {
                suffix: (fingerprints.get(suffix, 0), top_level.get(suffix, 0))
                for suffix in rows
            },
        )
        self.assertEqual(len(rows), 13)

    def test_dossiers_exclude_identity_bearing_or_raw_payload_constructs(self) -> None:
        forbidden_patterns = {
            "absolute Windows path": re.compile(r"(?i)(?<![A-Za-z0-9_])[A-Z]:[\\/]"),
            "UNC path": re.compile(r"\\\\[^\\\s]+\\"),
            "file URI": re.compile(r"(?i)file://"),
            "literal full hash": re.compile(
                r"(?<![0-9A-Fa-f])(?:[0-9A-Fa-f]{40}|[0-9A-Fa-f]{64})(?![0-9A-Fa-f])"
            ),
            "raw hex dump": re.compile(
                r"(?i)(?:\b[0-9a-f]{2}[ ,:;-]+){15,}[0-9a-f]{2}\b"
            ),
            "base64-like payload": re.compile(
                r"(?<![A-Za-z0-9+/])[A-Za-z0-9+/]{100,}={0,2}(?![A-Za-z0-9+/])"
            ),
            "zero-width character": re.compile(r"[\u200b\u200c\u200d\ufeff]"),
            "untracked runtime-log citation": re.compile(r"runtime/omega-boot", re.I),
            "raw HOG header field": re.compile(r"header_ascii", re.I),
        }
        for markdown in DOSSIER_ROOT.glob("*.md"):
            text = markdown.read_text(encoding="utf-8")
            for label, pattern in forbidden_patterns.items():
                with self.subTest(dossier=markdown.name, construct=label):
                    self.assertIsNone(pattern.search(text))


if __name__ == "__main__":
    unittest.main()
