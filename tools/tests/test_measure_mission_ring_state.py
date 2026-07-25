from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS_ROOT = Path(__file__).resolve().parents[1]
if str(TOOLS_ROOT) not in sys.path:
    sys.path.insert(0, str(TOOLS_ROOT))

import measure_mission_ring_state as mrs  # noqa: E402


def quad(index: int, cx: float, cy: float, w: float, h: float, *,
         alpha: int = 0, frame: int = 0, tbp0: int = 0) -> dict:
    """A draw record shaped like the one ``gs_dump_inspect`` emits."""
    return {
        "index": index,
        "vsync_index": frame,
        "prim": "triangle_strip",
        "prim_flags": {"TME": 1, "ABE": 1},
        "vertex_count": 6,
        "screen_bbox": {
            "xmin": cx - w / 2.0, "xmax": cx + w / 2.0,
            "ymin": cy - h / 2.0, "ymax": cy + h / 2.0,
        },
        "tex0": {"TBP0": tbp0, "PSM": 19, "TW": 5, "TH": 5},
        "rgba_first": [0, 0, 0, alpha],
        "vertices": [{"rgba": [0, 0, 0, alpha]} for _ in range(6)],
    }


def synthetic_ring(highlight_node: int, *, frame: int = 0) -> dict:
    """Two band draws bracketing three markers, one of which paints a big halo."""
    draws: list[dict] = []
    idx = 0
    draws.append(quad(idx, 300.0, 240.0, 320.0, 290.0, frame=frame))
    idx += 1
    for node, (cx, cy) in enumerate([(200.0, 100.0), (300.0, 380.0), (400.0, 120.0)]):
        big_alpha = 127 if node == highlight_node else 0
        dot_alpha = 0 if node == highlight_node else 127
        draws.append(quad(idx, cx, cy, 52.0, 47.0, alpha=big_alpha, frame=frame))
        idx += 1
        draws.append(quad(idx, cx, cy, 29.0, 27.0, alpha=dot_alpha, frame=frame))
        idx += 1
    draws.append(quad(idx, 300.0, 240.0, 320.0, 290.0, frame=frame))
    idx += 1
    # Screen furniture after the ring run: a lone quad separated by a non-ring draw.
    draws.append({
        "index": idx, "vsync_index": frame, "prim": "sprite", "prim_flags": {},
        "vertex_count": 2,
        "screen_bbox": {"xmin": 0.0, "xmax": 10.0, "ymin": 0.0, "ymax": 10.0},
        "tex0": {}, "rgba_first": [0, 0, 0, 127], "vertices": [],
    })
    idx += 1
    draws.append(quad(idx, 90.0, 410.0, 33.0, 27.0, alpha=127, frame=frame))
    return {"draws": draws}


class ExtractRingTest(unittest.TestCase):
    def test_separates_band_markers_and_trailing_furniture(self) -> None:
        ring = mrs.extract_ring(synthetic_ring(1), None, 120.0)
        self.assertEqual(ring["band_quads"], 2)
        self.assertEqual(ring["marker_quads"], 6)
        self.assertEqual(len(ring["nodes"]), 3)
        # Draws 0-7 are the ring run; draw 8 is a sprite, so the button-glyph quad
        # at draw 9 falls outside it.
        self.assertEqual(ring["ring_draw_span"], [0, 7])
        self.assertEqual(ring["excluded_outside_span"], [9])

    def test_reports_the_smallest_quad_centre_as_the_node_position(self) -> None:
        ring = mrs.extract_ring(synthetic_ring(1), None, 120.0)
        self.assertEqual(
            [(n["cx"], n["cy"]) for n in ring["nodes"]],
            [(200.0, 100.0), (300.0, 380.0), (400.0, 120.0)],
        )
        self.assertEqual([n["w"] for n in ring["nodes"]], [29.0, 29.0, 29.0])
        self.assertEqual([n["largest_w"] for n in ring["nodes"]], [52.0, 52.0, 52.0])

    def test_highlight_is_the_node_whose_opaque_pass_is_not_its_dot(self) -> None:
        for expected in (0, 1, 2):
            with self.subTest(highlight=expected):
                summary = {"ring": mrs.extract_ring(synthetic_ring(expected), None, 120.0)}
                self.assertEqual([n["node"] for n in mrs.highlight_of(summary)], [expected])
                ratios = [n["opaque_area_ratio"] for n in summary["ring"]["nodes"]]
                self.assertGreater(ratios[expected], mrs.HIGHLIGHT_AREA_RATIO)
                for i, r in enumerate(ratios):
                    if i != expected:
                        self.assertEqual(r, 1.0)

    def test_band_modal_box_ignores_an_unrelated_wide_quad(self) -> None:
        result = synthetic_ring(1)
        # A wide backdrop far from the band must not be counted as band art.
        result["draws"].insert(0, quad(-1, 300.0, 240.0, 700.0, 510.0))
        for i, d in enumerate(result["draws"]):
            d["index"] = i
        ring = mrs.extract_ring(result, None, 120.0)
        self.assertEqual(ring["band_quads"], 2)
        self.assertEqual(ring["band_modal"]["count"], 2)

    def test_a_close_pass_merges_two_markers_at_the_default_radius(self) -> None:
        result = synthetic_ring(1)
        # An extra quad 4 px from node 0 joins it; 2 px of separation is under 8 px.
        result["draws"].insert(1, quad(-1, 204.0, 100.0, 43.0, 37.0))
        for i, d in enumerate(result["draws"]):
            d["index"] = i
        self.assertEqual(len(mrs.extract_ring(result, None, 120.0)["nodes"]), 3)
        self.assertEqual(
            len(mrs.extract_ring(result, None, 120.0, cluster_radius=1.0)["nodes"]), 4)


class MatchNodesTest(unittest.TestCase):
    def _summary(self, highlight: int) -> dict:
        return {"slot": f"h{highlight}", "ring": mrs.extract_ring(
            synthetic_ring(highlight), None, 120.0)}

    def test_pairs_by_ordinal_and_reports_zero_movement(self) -> None:
        cmp = mrs.match_nodes(self._summary(1), self._summary(2))
        self.assertEqual(cmp["paired"], 3)
        self.assertEqual(cmp["exact_zero"], 3)
        self.assertEqual(cmp["max_dist"], 0.0)
        self.assertEqual(cmp["a_highlight"], [1])
        self.assertEqual(cmp["b_highlight"], [2])

    def test_reports_movement_when_a_marker_actually_moves(self) -> None:
        moved = synthetic_ring(1)
        for d in moved["draws"][1:3]:
            d["screen_bbox"]["xmin"] += 10.0
            d["screen_bbox"]["xmax"] += 10.0
        cmp = mrs.match_nodes(
            self._summary(1), {"slot": "moved", "ring": mrs.extract_ring(moved, None, 120.0)})
        self.assertEqual(cmp["exact_zero"], 2)
        self.assertAlmostEqual(cmp["max_dist"], 10.0)


if __name__ == "__main__":
    unittest.main()
