#!/usr/bin/env python3
"""Render a synthetic multitrack session to test VocalAI against.

Thin wrapper around :mod:`vocalai.testing.demo`, kept so the demo can be built
straight from a checkout without installing anything.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from vocalai.testing.demo import build


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", nargs="?", default="demo_session", help="output folder")
    args = ap.parse_args()
    out = Path(args.out)
    truth = build(out)
    print(f"wrote {len(list(out.glob('*.wav')))} tracks to {out}/")
    print(f"melody: {truth['melody_midi']}  key: {truth['key']}")


if __name__ == "__main__":
    main()
