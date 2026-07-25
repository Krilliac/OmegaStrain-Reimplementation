#!/usr/bin/env python3
"""Compare Command Center mission-ring state across several PCSX2 GS dumps.

This tool answers one question that a single capture cannot: between two dumps taken
at different moments, do the ring's marker quads sit at the *same* screen positions
while a *different* marker carries the opaque (selected) pass -- or do the marker
positions themselves move?

It reuses ``tools/gs_dump_inspect.py`` for container/GIF decoding, so every number it
prints is derived from that parser's own draw records; nothing is re-implemented here.

Extraction, per dump, restricted to the dump's first complete frame:

* A *ring quad* is a six-vertex ``triangle_strip`` draw with ``TME=1`` and ``ABE=1``.
* Ring quads wider than ``--band-min-width`` px are *band* quads (the elliptical track
  art, a single screen-aligned textured quad re-drawn many times per frame).
* The remaining ring quads are *marker* quads. They are clustered by screen centre
  with an 8 px radius, matching the clustering used for the original three-capture
  measurement recorded in ``analysis/formats/MISSION-RING.md``.
* A cluster's reported position is the centre of its smallest-area quad, again
  matching the original measurement.
* Only clusters whose draws fall inside the band's own draw span count as ring
  markers; the button-glyph quads emitted after the ring are excluded by that span.
* A quad is *opaque* when every one of its vertices carries alpha 127; every other
  ring quad in this corpus is emitted at alpha 0. The highlighted marker is the one
  whose opaque pass is a large halo/icon quad rather than its small dot -- reported
  as ``opaque_area_ratio``, the largest opaque quad's area over the node's smallest
  quad's area. A plain dot marker paints its dot opaque and scores ~1.0.

Privacy: like the parser it builds on, this tool emits derived geometry only -- screen
coordinates, quad sizes and counts. It never reads or writes texture payloads,
framebuffer contents or the embedded screenshot.

Usage::

    python -B tools/measure_mission_ring_state.py <dump-or-dir> [<dump-or-dir> ...]
    python -B tools/measure_mission_ring_state.py <dirs...> --json out.json
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs_dump_inspect import inspect  # noqa: E402

CLUSTER_RADIUS_PX = 8.0
OPAQUE_ALPHA = 127
# A node counts as highlighted when its opaque pass is at least this many times the
# area of its own smallest quad -- i.e. the game painted its halo/icon rather than
# its dot. Plain dot markers score 1.0; the largest non-highlight ratio measured in
# this corpus is 1.39 (the node-19 icon), so 2.0 separates the two populations with
# a wide margin.
HIGHLIGHT_AREA_RATIO = 2.0
# Per-draw jitter of the band quad reaches 6.94 px on one edge across this corpus,
# while the next-largest textured quad in these frames is 503 px wide -- more than
# 180 px from the band on every edge. 8 px therefore separates the two cleanly.
BAND_TOLERANCE_PX = 8.0


def _quad_geom(draw: dict[str, Any]) -> dict[str, float]:
    b = draw["screen_bbox"]
    w = b["xmax"] - b["xmin"]
    h = b["ymax"] - b["ymin"]
    return {
        "x0": b["xmin"], "y0": b["ymin"], "x1": b["xmax"], "y1": b["ymax"],
        "w": w, "h": h,
        "cx": (b["xmin"] + b["xmax"]) / 2.0,
        "cy": (b["ymin"] + b["ymax"]) / 2.0,
        "area": w * h,
    }


def _is_ring_quad(draw: dict[str, Any]) -> bool:
    return (
        draw["prim"] == "triangle_strip"
        and draw["vertex_count"] == 6
        and bool(draw["prim_flags"].get("TME"))
        and bool(draw["prim_flags"].get("ABE"))
    )


def _opaque(draw: dict[str, Any]) -> bool:
    verts = draw.get("vertices")
    if not verts:
        return draw["rgba_first"][3] == OPAQUE_ALPHA
    return all(v["rgba"][3] == OPAQUE_ALPHA for v in verts)


def extract_ring(result: dict[str, Any], frame: int | None,
                 band_min_width: float) -> dict[str, Any]:
    """Cluster the marker quads of one parsed dump into ring nodes."""
    draws = result["draws"]
    if frame is None:
        frames = sorted({d["vsync_index"] for d in draws})
        # The first frame index that carries a full complement of draws.
        counts = {f: sum(1 for d in draws if d["vsync_index"] == f) for f in frames}
        best = max(counts.values())
        frame = next(f for f in frames if counts[f] == best)
    frame_draws = [d for d in draws if d["vsync_index"] == frame]

    ring = [d for d in frame_draws if _is_ring_quad(d)]
    wide, markers = [], []
    for d in ring:
        g = _quad_geom(d)
        (wide if g["w"] >= band_min_width else markers).append((d, g))

    # The band is the wide quad the frame re-draws most often; other wide quads (a
    # backdrop, a footer plate) are not it. Accept a wide quad as band art only when
    # all four of its corners sit within BAND_TOLERANCE_PX of that modal box, which
    # is what lets the small per-draw jitter the doc records through while rejecting
    # unrelated geometry.
    bands: list[tuple[dict[str, Any], dict[str, float]]] = []
    modal_key: tuple | None = None
    if wide:
        tally: dict[tuple, int] = {}
        for _, g in wide:
            k = (round(g["x0"], 3), round(g["y0"], 3), round(g["x1"], 3), round(g["y1"], 3))
            tally[k] = tally.get(k, 0) + 1
        modal_key = max(tally, key=lambda k: tally[k])
        for d, g in wide:
            if all(abs(g[f] - modal_key[i]) <= BAND_TOLERANCE_PX
                   for i, f in enumerate(("x0", "y0", "x1", "y1"))):
                bands.append((d, g))
            else:
                markers.append((d, g))
    markers = [(d, g) for d, g in markers if g["w"] < band_min_width]

    # The ring is one uninterrupted run of these quads: the band draws seed it and it
    # is grown outwards while the neighbouring draw is still a ring quad. Quads beyond
    # that run (a backdrop plate, the bottom button glyphs) are other screen furniture
    # that happens to be a textured quad, and the run's ends are where the title
    # switches to its sprite text path.
    if bands:
        ring_indices = {d["index"] for d in ring}
        lo = min(d["index"] for d, _ in bands)
        hi = max(d["index"] for d, _ in bands)
        while lo - 1 in ring_indices:
            lo -= 1
        while hi + 1 in ring_indices:
            hi += 1
        outside = [d["index"] for d, _ in markers if not lo <= d["index"] <= hi]
        markers = [(d, g) for d, g in markers if lo <= d["index"] <= hi]
        bands = [(d, g) for d, g in bands if lo <= d["index"] <= hi]
    else:
        lo = hi = -1
        outside = []

    clusters: list[dict[str, Any]] = []
    for d, g in markers:
        hit = None
        for c in clusters:
            if math.hypot(g["cx"] - c["seed_cx"], g["cy"] - c["seed_cy"]) <= CLUSTER_RADIUS_PX:
                hit = c
                break
        if hit is None:
            hit = {
                "seed_cx": g["cx"], "seed_cy": g["cy"],
                "first_draw": d["index"], "quads": [],
            }
            clusters.append(hit)
        hit["quads"].append({
            "draw": d["index"], "opaque": _opaque(d),
            "tbp0": d["tex0"].get("TBP0"), "psm": d["tex0"].get("PSM"),
            "tw": d["tex0"].get("TW"), "th": d["tex0"].get("TH"),
            **g,
        })

    nodes = []
    for i, c in enumerate(clusters):
        quads = sorted(c["quads"], key=lambda q: q["area"])
        small, large = quads[0], quads[-1]
        opaque = [q for q in quads if q["opaque"]]
        ratio = (max(q["area"] for q in opaque) / small["area"]) if opaque else 0.0
        nodes.append({
            "node": i,
            "first_draw": c["first_draw"],
            "draws": len(quads),
            "cx": small["cx"], "cy": small["cy"],
            "x0": small["x0"], "y0": small["y0"], "x1": small["x1"], "y1": small["y1"],
            "w": small["w"], "h": small["h"],
            "largest_w": large["w"], "largest_h": large["h"],
            "opaque_count": len(opaque),
            "opaque_sizes": [[round(q["w"], 3), round(q["h"], 3)] for q in opaque],
            "opaque_area_ratio": round(ratio, 4),
            "highlighted": ratio >= HIGHLIGHT_AREA_RATIO,
            "quads": [
                {
                    "draw": q["draw"], "opaque": q["opaque"],
                    "w": round(q["w"], 3), "h": round(q["h"], 3),
                    "tbp0": q["tbp0"], "psm": q["psm"], "tw": q["tw"], "th": q["th"],
                }
                for q in sorted(quads, key=lambda q: q["draw"])
            ],
        })

    band_boxes = sorted({(round(g["x0"], 3), round(g["y0"], 3),
                          round(g["x1"], 3), round(g["y1"], 3)) for _, g in bands})
    modal = None
    if bands and modal_key is not None:
        modal = {
            "bbox": list(modal_key),
            "count": sum(1 for _, g in bands
                         if (round(g["x0"], 3), round(g["y0"], 3),
                             round(g["x1"], 3), round(g["y1"], 3)) == modal_key),
        }

    return {
        "frame": frame,
        "frame_draw_count": len(frame_draws),
        "ring_draw_span": [lo, hi],
        "excluded_outside_span": outside,
        "ring_quads": len(ring),
        "band_quads": len(bands),
        "band_distinct_boxes": len(band_boxes),
        "band_modal": modal,
        "band_draws": [
            {"draw": d["index"],
             "bbox": [round(g["x0"], 4), round(g["y0"], 4),
                      round(g["x1"], 4), round(g["y1"], 4)]}
            for d, g in sorted(bands, key=lambda p: p[0]["index"])
        ],
        "marker_quads": len(markers),
        "nodes": nodes,
    }


def summarise(path: Path, frame: int | None, band_min_width: float) -> dict[str, Any]:
    result = inspect(path, True, 0, 0)
    disp = result["priv_regs"]
    ring = extract_ring(result, frame, band_min_width)
    return {
        "dump": path.name,
        "slot": path.parent.name,
        "packets": result["container"]["packet_count"],
        "draw_call_count": result["draw_call_count"],
        "prim_histogram": result["prim_histogram"],
        "vsync_draw_marks": result["vsync_draw_marks"],
        "display": _display_of(disp),
        "ring": ring,
    }


def _display_of(priv: Any) -> Any:
    """Pull the visible-area fields out of whatever shape decode_priv_regs returns."""
    if not isinstance(priv, dict):
        return None
    for key in ("DISP0", "DISPLAY0", "DISPLAY1", "DISP1"):
        node = priv.get(key)
        if isinstance(node, dict):
            return {key: node}
    return {k: v for k, v in priv.items() if "DISP" in k.upper()}


def match_nodes(a: dict[str, Any], b: dict[str, Any]) -> dict[str, Any]:
    """Pair the two rings' nodes by emission ordinal and report positional deviation.

    Emission order is ring order (proven for this screen), so ordinal pairing is the
    honest test: it does not silently re-associate a marker that actually moved with
    whichever other marker happens to sit nearest its new position.
    """
    pairs = []
    for na, nb in zip(a["ring"]["nodes"], b["ring"]["nodes"]):
        pairs.append({
            "node": na["node"],
            "a_centre": [na["cx"], na["cy"]], "b_centre": [nb["cx"], nb["cy"]],
            "dx": nb["cx"] - na["cx"], "dy": nb["cy"] - na["cy"],
            "dist": math.hypot(nb["cx"] - na["cx"], nb["cy"] - na["cy"]),
        })
    return {
        "a": a["slot"], "b": b["slot"],
        "a_nodes": len(a["ring"]["nodes"]), "b_nodes": len(b["ring"]["nodes"]),
        "paired": len(pairs),
        "exact_zero": sum(1 for p in pairs if p["dist"] == 0.0),
        "max_dist": max((p["dist"] for p in pairs), default=0.0),
        "max_dist_excluding_node0": max((p["dist"] for p in pairs if p["node"] != 0),
                                        default=0.0),
        "a_highlight": [n["node"] for n in highlight_of(a)],
        "b_highlight": [n["node"] for n in highlight_of(b)],
        "pairs": pairs,
    }


def highlight_of(summary: dict[str, Any]) -> list[dict[str, Any]]:
    """Nodes whose opaque pass is a halo/icon rather than their own dot."""
    return [n for n in summary["ring"]["nodes"] if n["highlighted"]]


def _dumps_under(target: Path) -> list[Path]:
    if target.is_dir():
        return sorted(p for p in target.rglob("*")
                      if p.suffix in {".zst", ".xz"} or p.name.endswith(".gs"))
    return [target]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("targets", nargs="+", type=Path, help="dump files or directories")
    ap.add_argument("--frame", type=int, default=None,
                    help="vsync index to analyse (default: the fullest frame)")
    ap.add_argument("--band-min-width", type=float, default=120.0,
                    help="ring quads at least this wide are band art, not markers")
    ap.add_argument("--json", type=Path, default=None, help="write full results here")
    ap.add_argument("--nodes", action="store_true", help="print the per-node table")
    args = ap.parse_args(argv)

    paths: list[Path] = []
    for t in args.targets:
        paths.extend(_dumps_under(t))
    if not paths:
        sys.stderr.write("no dumps found\n")
        return 2

    summaries = [summarise(p, args.frame, args.band_min_width) for p in paths]

    for s in summaries:
        r = s["ring"]
        hl = highlight_of(s)
        print(f"{s['slot']}/{s['dump']}: packets={s['packets']} draws={s['draw_call_count']} "
              f"prims={s['prim_histogram']}")
        print(f"    frame {r['frame']}: {r['frame_draw_count']} draws, "
              f"{r['band_quads']} band quads ({r['band_distinct_boxes']} distinct boxes), "
              f"{r['marker_quads']} marker quads -> {len(r['nodes'])} clusters")
        if r["band_modal"]:
            bb = r["band_modal"]["bbox"]
            print(f"    modal band bbox ({bb[0]}, {bb[1]}) - ({bb[2]}, {bb[3]}) "
                  f"x{r['band_modal']['count']}")
        for n in hl:
            print(f"    HIGHLIGHT: node {n['node']} first_draw {n['first_draw']} "
                  f"centre ({n['cx']:.3f}, {n['cy']:.3f}) dot {n['w']:.2f}x{n['h']:.2f} "
                  f"opaque {n['opaque_sizes']} ratio {n['opaque_area_ratio']}")
        if not hl and r["nodes"]:
            print("    HIGHLIGHT: none (no node paints a halo/icon opaque)")
        noop = [n["node"] for n in r["nodes"] if n["opaque_count"] == 0]
        if noop:
            print(f"    nodes with no opaque pass at all: {noop}")
        if args.nodes:
            for n in r["nodes"]:
                print(f"      {n['node']:3d} fd{n['first_draw']:4d} d{n['draws']:2d} "
                      f"c=({n['cx']:8.3f},{n['cy']:8.3f}) "
                      f"sm={n['w']:6.2f}x{n['h']:6.2f} lg={n['largest_w']:6.2f}x"
                      f"{n['largest_h']:6.2f} op={n['opaque_count']} "
                      f"r={n['opaque_area_ratio']:.2f}")

    comparisons = []
    ring_summaries = [s for s in summaries if len(s["ring"]["nodes"]) >= 10]
    for i in range(len(ring_summaries)):
        for j in range(i + 1, len(ring_summaries)):
            cmp = match_nodes(ring_summaries[i], ring_summaries[j])
            comparisons.append(cmp)
            print(f"{cmp['a']} vs {cmp['b']}: nodes {cmp['a_nodes']}/{cmp['b_nodes']} "
                  f"paired {cmp['paired']} exact-0px {cmp['exact_zero']} "
                  f"max {cmp['max_dist']:.4f} px "
                  f"(excl node0 {cmp['max_dist_excluding_node0']:.4f}) "
                  f"highlight {cmp['a_highlight']} vs {cmp['b_highlight']}")
            for p in sorted(cmp["pairs"], key=lambda p: -p["dist"])[:3]:
                if p["dist"] > 0:
                    print(f"    moved: node {p['node']} ({p['a_centre'][0]:.3f}, "
                          f"{p['a_centre'][1]:.3f}) -> ({p['b_centre'][0]:.3f}, "
                          f"{p['b_centre'][1]:.3f}) d={p['dist']:.4f} "
                          f"({p['dx']:+.4f}, {p['dy']:+.4f})")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps({"dumps": summaries, "comparisons": comparisons},
                       indent=1, sort_keys=False),
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
