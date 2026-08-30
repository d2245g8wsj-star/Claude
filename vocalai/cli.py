"""Command line interface.

    vocalai tune SESSION [-o OUT]     tune (and align) every vocal in a session
    vocalai analyse TRACK             report notes, key and tuning; no audio written
    vocalai align DUB --guide LEAD    stand-alone time alignment
    vocalai notes TRACK -o notes.json export the editable note model
    vocalai render TRACK --notes F    re-render from an edited note model
    vocalai demo DIR                  write a synthetic session to try it on
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

from .analysis.key import estimate_key, key_from_name
from .audio.io import AudioBuffer, read_wav, write_buffer, write_wav
from .model import NoteSequence
from .pipeline import analyze_track, render_correction, tune_track
from .process.align import AlignConfig, align_to_guide
from .process.tuner import TuneConfig, build_correction
from .session.engine import SessionConfig, process_session
from .session.report import write_reports

STYLES = ("transparent", "natural", "tight", "hard")


def _tune_config(args) -> TuneConfig:
    cfg = TuneConfig.style(args.style)
    if args.strength is not None:
        cfg.strength = float(np.clip(args.strength, 0.0, 1.0))
    if args.vibrato is not None:
        cfg.vibrato_keep = float(np.clip(args.vibrato, 0.0, 2.0))
    if args.drift is not None:
        cfg.drift_keep = float(np.clip(args.drift, 0.0, 1.0))
    if getattr(args, "no_transitions", False):
        cfg.preserve_transitions = False
    if getattr(args, "max_shift", None) is not None:
        cfg.max_shift_cents = float(max(args.max_shift, 0.0))
    return cfg


def _add_tune_flags(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--style", choices=STYLES, default="natural",
                    help="how aggressive the correction is (default: natural)")
    ap.add_argument("--strength", type=float, help="override: 0..1 amount of error removed")
    ap.add_argument("--vibrato", type=float, help="override: 1.0 keeps vibrato, 0 flattens it")
    ap.add_argument("--drift", type=float, help="override: how much in-note pitch sag survives")
    ap.add_argument("--no-transitions", action="store_true",
                    help="do not preserve scoops and falls at phrase edges")
    ap.add_argument("--key", help='force a key, e.g. "F# minor", "Bb dorian", "chromatic"')
    ap.add_argument("--tuning", type=float, help="force concert pitch in Hz (default: detected)")
    ap.add_argument("--max-shift", type=float,
                    help="largest move allowed, in cents (default: 320; raise it for "
                         "hand-edited note files that transpose by more than a few semitones)")


def cmd_tune(args) -> int:
    cfg = SessionConfig(
        style=args.style,
        tune=_tune_config(args),
        align=AlignConfig(strength=args.align_strength, smooth_ms=args.align_smooth),
        do_align=not args.no_align,
        do_tune=not args.no_tune,
        key=args.key,
        tuning_hz=args.tuning,
        include=args.include or None,
        exclude=args.exclude or None,
        output_suffix=args.suffix,
        write_audio=not args.dry_run,
        bit_depth=args.bits,
    )
    out_dir = Path(args.out) if args.out else Path(args.session) / "vocalai_out"
    say = (lambda m: None) if args.quiet else (lambda m: print(f"  {m}", file=sys.stderr))
    result = process_session(args.session, out_dir, cfg, progress=say)

    print(result.summary())
    if not args.no_report:
        paths = write_reports(result, out_dir)
        print(f"\nreport: {paths['html']}")
    if not args.dry_run:
        print(f"audio:  {out_dir}")
    return 0


def cmd_analyse(args) -> int:
    buf = read_wav(args.track)
    an = analyze_track(buf, name=Path(args.track).stem)
    key = key_from_name(args.key, tuning_hz=args.tuning or 440.0) if args.key else estimate_key(an.notes)
    if args.tuning:
        key.tuning_hz = args.tuning
    _, result = tune_track(an, _tune_config(args), key=key, render=False)
    print(an.notes.summary())
    print()
    for k, v in result.stats.items():
        print(f"{k:>26}: {v}")
    if args.json:
        an.notes.to_json(args.json)
        print(f"\nwrote {args.json}")
    return 0


def cmd_align(args) -> int:
    dub = read_wav(args.dub)
    guide = read_wav(args.guide)
    if dub.sample_rate != guide.sample_rate:
        raise SystemExit(
            f"sample rate mismatch: {args.dub} is {dub.sample_rate} Hz, "
            f"{args.guide} is {guide.sample_rate} Hz"
        )
    cfg = AlignConfig(strength=args.strength, smooth_ms=args.smooth, max_stretch=args.max_stretch)
    rendered, result = align_to_guide(dub.mono(), guide.mono(), dub.sample_rate, cfg=cfg)
    out = Path(args.out)
    write_wav(out, rendered, dub.sample_rate, bits=dub.source_bits)
    for k, v in result.stats.items():
        print(f"{k:>22}: {v}")
    print(f"wrote {out}")
    return 0


def cmd_notes(args) -> int:
    buf = read_wav(args.track)
    an = analyze_track(buf, name=Path(args.track).stem)
    key = key_from_name(args.key) if args.key else estimate_key(an.notes)
    tune_track(an, _tune_config(args), key=key, render=False)
    an.notes.to_json(args.out)
    print(f"wrote {len(an.notes)} notes to {args.out}")
    print("Edit target_midi, pitch_offset_cents, vibrato_scale, strength or locked,")
    print("then re-render with:  vocalai render TRACK --notes " + str(args.out) + " -o out.wav")
    return 0


def cmd_render(args) -> int:
    buf = read_wav(args.track)
    an = analyze_track(buf, name=Path(args.track).stem)
    edited = NoteSequence.from_json(Path(args.notes))
    if len(edited) != len(an.notes):
        print(
            f"warning: the file has {len(edited)} notes but the track analyses to "
            f"{len(an.notes)}; rendering against the file's notes by time position.",
            file=sys.stderr,
        )
    key = key_from_name(edited.key) if edited.key else estimate_key(an.notes)
    if edited.tuning_hz:
        key.tuning_hz = edited.tuning_hz
    cfg = _tune_config(args)
    correction = build_correction(edited, an.pitch, key, cfg)
    audio = render_correction(an, 2.0 ** (correction / 1200.0))
    out_buf = AudioBuffer(audio, buf.sample_rate, source_bits=buf.source_bits,
                          source_float=buf.source_float)
    write_buffer(args.out, out_buf, bits=args.bits)
    print(f"wrote {args.out}")
    return 0


def cmd_demo(args) -> int:
    from .testing.demo import build  # noqa: PLC0415

    truth = build(Path(args.out))
    print(f"wrote a demo session to {args.out}/  (key {truth['key']})")
    print(f"try:  vocalai tune {args.out}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="vocalai",
        description="Automatic vocal tuning and alignment for a whole session.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = ap.add_subparsers(dest="command", required=True)

    t = sub.add_parser("tune", help="tune and align every vocal in a session")
    t.add_argument("session", help="session folder (or a single audio file)")
    t.add_argument("-o", "--out", help="output folder (default: SESSION/vocalai_out)")
    _add_tune_flags(t)
    t.add_argument("--no-align", action="store_true", help="skip time alignment")
    t.add_argument("--no-tune", action="store_true", help="align only, do not tune")
    t.add_argument("--align-strength", type=float, default=1.0,
                   help="0..1 how far stacks are pulled onto the lead (default: 1)")
    t.add_argument("--align-smooth", type=float, default=50.0,
                   help="alignment smoothing in ms (default: 50)")
    t.add_argument("--include", action="append", help="only tracks whose name contains this")
    t.add_argument("--exclude", action="append", help="skip tracks whose name contains this")
    t.add_argument("--suffix", default="_tuned", help="output filename suffix")
    t.add_argument("--bits", type=int, choices=(16, 24, 32), help="output word length")
    t.add_argument("--dry-run", action="store_true", help="analyse and report, write no audio")
    t.add_argument("--no-report", action="store_true", help="do not write the HTML/JSON report")
    t.add_argument("-q", "--quiet", action="store_true")
    t.set_defaults(func=cmd_tune)

    a = sub.add_parser("analyse", aliases=["analyze"], help="report notes, key and tuning")
    a.add_argument("track")
    a.add_argument("--json", help="also write the note model here")
    _add_tune_flags(a)
    a.set_defaults(func=cmd_analyse)

    g = sub.add_parser("align", help="align one track to a guide")
    g.add_argument("dub")
    g.add_argument("--guide", required=True, help="the track to align to")
    g.add_argument("-o", "--out", required=True)
    g.add_argument("--strength", type=float, default=1.0)
    g.add_argument("--smooth", type=float, default=50.0, help="warp smoothing in ms")
    g.add_argument("--max-stretch", type=float, default=2.0)
    g.set_defaults(func=cmd_align)

    n = sub.add_parser("notes", help="export the editable note model as JSON")
    n.add_argument("track")
    n.add_argument("-o", "--out", required=True)
    _add_tune_flags(n)
    n.set_defaults(func=cmd_notes)

    r = sub.add_parser("render", help="re-render a track from an edited note model")
    r.add_argument("track")
    r.add_argument("--notes", required=True, help="note JSON, as written by 'notes'")
    r.add_argument("-o", "--out", required=True)
    r.add_argument("--bits", type=int, choices=(16, 24, 32))
    _add_tune_flags(r)
    r.set_defaults(func=cmd_render)

    d = sub.add_parser("demo", help="write a synthetic session to experiment with")
    d.add_argument("out", nargs="?", default="demo_session")
    d.set_defaults(func=cmd_demo)
    return ap


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, ValueError) as exc:
        print(f"vocalai: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
