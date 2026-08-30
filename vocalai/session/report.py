"""Reports: a machine-readable JSON dump and a note-editor style HTML view.

The HTML report is the closest thing here to Melodyne's window - every note is
drawn as a blob at the pitch it was sung, with the pitch curve running through
it and a marker at the pitch it was moved to.  Seeing the decisions is what
makes them arguable, and an automatic tuner that cannot be argued with is one
you cannot trust with a session.
"""

from __future__ import annotations

import html
import json
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from ..audio.dsp import NOTE_NAMES, hz_to_midi
from .engine import SessionResult, TrackResult

_CSS = """
:root {
  --bg: #f7f7f5; --panel: #ffffff; --ink: #1a1a19; --muted: #6b6b66;
  --line: #e2e2dd; --grid: #ececE6; --scale: #dfe7f0;
  --sung: #c2703f; --target: #2f6f4f; --curve: #3a3a38; --accent: #35566e;
  --warn: #a3562a;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #16171a; --panel: #1e2024; --ink: #e9e9e6; --muted: #9a9a94;
    --line: #2e3137; --grid: #24262b; --scale: #2b3540;
    --sung: #e0925c; --target: #6ec095; --curve: #b9bbc0; --accent: #7fa8c6;
    --warn: #d99a63;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink);
  font: 14px/1.55 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
.wrap { max-width: 1180px; margin: 0 auto; padding: 32px 24px 72px; }
h1 { font-size: 25px; margin: 0 0 4px; letter-spacing: -.01em; }
h2 { font-size: 17px; margin: 0 0 2px; }
.sub { color: var(--muted); margin: 0 0 28px; }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 10px;
  padding: 18px 20px; margin-bottom: 18px; }
.facts { display: flex; flex-wrap: wrap; gap: 10px 28px; margin: 12px 0 0; padding: 0; list-style: none; }
.facts li { display: flex; flex-direction: column; }
.facts b { font-size: 18px; font-weight: 600; font-variant-numeric: tabular-nums; }
.facts span { color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .05em; }
.role { display: inline-block; font-size: 11px; text-transform: uppercase; letter-spacing: .06em;
  padding: 2px 8px; border-radius: 999px; border: 1px solid var(--line); color: var(--muted); }
.scroll { overflow-x: auto; margin-top: 14px; }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; font-size: 13px; }
th, td { text-align: right; padding: 5px 10px; border-bottom: 1px solid var(--line); white-space: nowrap; }
th:first-child, td:first-child { text-align: left; }
th { color: var(--muted); font-weight: 600; font-size: 11px; text-transform: uppercase; letter-spacing: .05em; }
.legend { display: flex; gap: 18px; flex-wrap: wrap; color: var(--muted); font-size: 12px; margin-top: 10px; }
.key { display: inline-block; width: 11px; height: 11px; border-radius: 2px; margin-right: 6px;
  vertical-align: -1px; }
.skip { color: var(--muted); }
footer { color: var(--muted); font-size: 12px; margin-top: 30px; }
"""


def _svg_blobs(tr: TrackResult, width: int = 1100, height: int = 260) -> str:
    """A piano-roll of the track: pitch curve, sung notes, and their targets."""
    an = tr.analysis
    if an is None or not len(an.notes):
        return '<p class="skip">No notes detected.</p>'

    notes = an.notes.notes
    tuning = an.notes.tuning_hz or 440.0
    dur = max(an.duration, 1e-6)
    midis = [n.midi for n in notes] + [n.target_midi for n in notes if n.target_midi]
    lo = float(np.floor(min(midis) - 1.5))
    hi = float(np.ceil(max(midis) + 1.5))
    span = max(hi - lo, 2.0)

    pad_l, pad_r, pad_t, pad_b = 44, 12, 12, 22
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b

    def X(t: float) -> float:
        return pad_l + plot_w * (t / dur)

    def Y(m: float) -> float:
        return pad_t + plot_h * (1.0 - (m - lo) / span)

    key = tr.tune_result.key if tr.tune_result else None
    parts = [
        f'<svg viewBox="0 0 {width} {height}" width="100%" height="{height}" '
        f'role="img" aria-label="pitch and note view for {html.escape(tr.name)}">'
    ]

    # Semitone lanes, with the key's scale tones tinted.
    for m in range(int(lo), int(hi) + 1):
        y = Y(m)
        in_scale = key is not None and (m % 12) in key.pitch_classes
        if in_scale:
            parts.append(
                f'<rect x="{pad_l}" y="{y - plot_h / span / 2:.1f}" width="{plot_w}" '
                f'height="{plot_h / span:.1f}" fill="var(--scale)" opacity=".45"/>'
            )
        parts.append(
            f'<line x1="{pad_l}" y1="{y:.1f}" x2="{width - pad_r}" y2="{y:.1f}" '
            f'stroke="var(--grid)" stroke-width="1"/>'
        )
        if span <= 26 or m % 12 == 0:
            parts.append(
                f'<text x="{pad_l - 6}" y="{y + 3.5:.1f}" text-anchor="end" font-size="9.5" '
                f'fill="var(--muted)">{NOTE_NAMES[m % 12]}{m // 12 - 1}</text>'
            )

    # The raw pitch curve.
    f0 = an.pitch.f0
    times = an.pitch.times
    run: list[str] = []
    for t, f in zip(times, f0):
        if np.isfinite(f):
            run.append(f"{X(float(t)):.1f},{Y(float(hz_to_midi(f, tuning))):.1f}")
        elif run:
            parts.append(
                f'<polyline points="{" ".join(run)}" fill="none" stroke="var(--curve)" '
                f'stroke-width="1" opacity=".45"/>'
            )
            run = []
    if run:
        parts.append(
            f'<polyline points="{" ".join(run)}" fill="none" stroke="var(--curve)" '
            f'stroke-width="1" opacity=".45"/>'
        )

    # Note blobs: sung position, with a bar at the target pitch.
    lane = plot_h / span
    for n in notes:
        x0, x1 = X(n.start), X(n.end)
        w = max(x1 - x0, 2.0)
        parts.append(
            f'<rect x="{x0:.1f}" y="{Y(n.midi) - lane * .34:.1f}" width="{w:.1f}" '
            f'height="{lane * .68:.1f}" rx="2" fill="var(--sung)" opacity=".30"/>'
        )
        if n.target_midi is not None:
            ty = Y(n.target_midi)
            cx0, cx1 = X(n.core_start), X(n.core_end)
            parts.append(
                f'<line x1="{cx0:.1f}" y1="{ty:.1f}" x2="{max(cx1, cx0 + 2):.1f}" y2="{ty:.1f}" '
                f'stroke="var(--target)" stroke-width="2.5" stroke-linecap="round"/>'
            )
            if abs(n.error_to_target_cents) > 4.0:
                mid = (cx0 + cx1) / 2
                parts.append(
                    f'<line x1="{mid:.1f}" y1="{Y(n.midi):.1f}" x2="{mid:.1f}" y2="{ty:.1f}" '
                    f'stroke="var(--target)" stroke-width="1" opacity=".55"/>'
                )

    for k in range(int(dur) + 1):
        parts.append(
            f'<text x="{X(k):.1f}" y="{height - 6}" text-anchor="middle" font-size="9.5" '
            f'fill="var(--muted)">{k}s</text>'
        )
    parts.append("</svg>")
    return "".join(parts)


def _note_table(tr: TrackResult, limit: int = 60) -> str:
    an = tr.analysis
    if an is None or not len(an.notes):
        return ""
    rows = [
        "<div class='scroll'><table><thead><tr>"
        "<th>#</th><th>start</th><th>len</th><th>sung</th><th>off</th>"
        "<th>target</th><th>moved</th><th>vibrato</th><th>note</th>"
        "</tr></thead><tbody>"
    ]
    for n in an.notes.notes[:limit]:
        vib = f"{n.vibrato_cents:.0f}c / {n.vibrato_hz:.1f}Hz" if n.vibrato_cents > 0 else "-"
        rows.append(
            f"<tr><td>{n.index}</td><td>{n.start:.2f}s</td><td>{n.duration:.2f}s</td>"
            f"<td>{html.escape(n.name)}</td><td>{n.cents_off:+.0f}c</td>"
            f"<td>{html.escape(n.target_name or '-')}</td>"
            f"<td>{n.correction_cents:+.0f}c</td><td>{vib}</td>"
            f"<td>{html.escape(n.reason or '')}</td></tr>"
        )
    if len(an.notes) > limit:
        rows.append(f"<tr><td colspan='9'>... {len(an.notes) - limit} more notes</td></tr>")
    rows.append("</tbody></table></div>")
    return "".join(rows)


def _facts(items: list[tuple[str, str]]) -> str:
    lis = "".join(f"<li><b>{html.escape(v)}</b><span>{html.escape(k)}</span></li>" for k, v in items)
    return f"<ul class='facts'>{lis}</ul>"


def to_html(result: SessionResult, title: str = "VocalAI session report") -> str:
    """Render the whole session as one self-contained HTML page."""
    key = result.key
    head = [
        f"<h1>{html.escape(title)}</h1>",
        f"<p class='sub'>{datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')} &middot; "
        f"{len(result.tracks)} tracks, {len(result.vocals)} vocal</p>",
    ]
    facts = []
    if key:
        facts += [
            ("key", key.name),
            ("key confidence", f"{key.confidence:.2f}"),
            ("concert pitch", f"{key.tuning_hz:.2f} Hz"),
        ]
    if result.lead:
        facts.append(("lead vocal", result.lead))
    head.append(f"<div class='panel'>{_facts(facts)}</div>")

    body = []
    for tr in result.tracks:
        if not tr.processed:
            body.append(
                f"<div class='panel'><h2>{html.escape(tr.name)} "
                f"<span class='role'>{html.escape(tr.profile.role)}</span></h2>"
                f"<p class='skip'>Skipped &mdash; {html.escape(tr.skipped_reason)} "
                f"(vocal score {tr.profile.score:.2f})</p></div>"
            )
            continue

        items = [("notes", str(tr.note_count)), ("vocal score", f"{tr.profile.score:.2f}")]
        if tr.tune_result:
            s = tr.tune_result.stats
            items += [
                ("error before", f"{s['mean_error_before_cents']:.1f}c"),
                ("error after", f"{s['mean_error_after_cents']:.1f}c"),
                ("notes moved", str(s["notes_moved"])),
                ("largest move", f"{s['max_shift_cents']:.0f}c"),
            ]
        if tr.align_result:
            a = tr.align_result.stats
            items += [
                ("timing shift", f"{a['median_abs_shift_ms']:.0f} ms"),
                ("largest shift", f"{a['max_shift_ms']:.0f} ms"),
            ]
        if tr.profile.interval_semitones is not None:
            items.append(("interval vs lead", f"{tr.profile.interval_semitones:+.1f} st"))

        body.append(
            f"<div class='panel'><h2>{html.escape(tr.name)} "
            f"<span class='role'>{html.escape(tr.profile.role)}</span></h2>"
            + _facts(items)
            + _svg_blobs(tr)
            + "<div class='legend'>"
            "<span><i class='key' style='background:var(--sung);opacity:.5'></i>sung note</span>"
            "<span><i class='key' style='background:var(--target)'></i>target pitch</span>"
            "<span><i class='key' style='background:var(--curve);opacity:.5'></i>pitch curve</span>"
            "<span><i class='key' style='background:var(--scale)'></i>scale tone</span>"
            "</div>"
            + _note_table(tr)
            + "</div>"
        )

    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        f"<title>{html.escape(title)}</title><style>{_CSS}</style></head><body><div class='wrap'>"
        + "".join(head)
        + "".join(body)
        + "<footer>Generated by VocalAI &mdash; automatic vocal tuning and alignment.</footer>"
        "</div></body></html>"
    )


def to_dict(result: SessionResult) -> dict:
    """The full session outcome as plain data."""
    payload: dict = {
        "generated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "lead": result.lead,
        "output_dir": str(result.output_dir) if result.output_dir else None,
        "tracks": [],
    }
    if result.key:
        payload["key"] = {
            "name": result.key.name,
            "tonic": result.key.tonic,
            "mode": result.key.mode,
            "scale": result.key.scale_name,
            "pitch_classes": list(result.key.pitch_classes),
            "confidence": round(result.key.confidence, 3),
            "tuning_hz": round(result.key.tuning_hz, 3),
        }
    for tr in result.tracks:
        entry = {
            "name": tr.name,
            "path": str(tr.path),
            "role": tr.profile.role,
            "is_vocal": tr.profile.is_vocal,
            "vocal_score": round(tr.profile.score, 3),
            "evidence": {k: round(v, 3) for k, v in tr.profile.evidence.items()},
            "processed": tr.processed,
            "skipped_reason": tr.skipped_reason,
            "output": str(tr.output_path) if tr.output_path else None,
            "notes": tr.note_count,
        }
        if tr.profile.interval_semitones is not None:
            entry["interval_vs_lead_semitones"] = round(tr.profile.interval_semitones, 2)
        if tr.tune_result:
            entry["tuning"] = tr.tune_result.stats
            entry["note_detail"] = [n.to_dict() for n in tr.tune_result.sequence.notes]
        if tr.align_result:
            entry["alignment"] = tr.align_result.stats
        payload["tracks"].append(entry)
    return payload


def write_reports(
    result: SessionResult, out_dir: str | Path, stem: str = "vocalai_report"
) -> dict[str, Path]:
    """Write both reports next to the processed audio."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / f"{stem}.json"
    html_path = out_dir / f"{stem}.html"
    json_path.write_text(json.dumps(to_dict(result), indent=2))
    html_path.write_text(to_html(result))
    return {"json": json_path, "html": html_path}
