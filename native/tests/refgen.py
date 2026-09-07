#!/usr/bin/env python3
"""Generate reference vectors for the C++ core from the Python implementation.

The Python engine is the validated reference; these files let the C++ port be
checked against it rather than merely against itself.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from scipy import signal as sg

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "reference")
SR = 44100


def dump(name: str, arr) -> None:
    a = np.asarray(arr, dtype=np.float64).reshape(-1)
    (OUT / f"{name}.f64").write_bytes(a.tobytes())


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(4)

    # --- signals -----------------------------------------------------------
    n = 4096
    t = np.arange(n) / SR
    sig = (np.sin(2 * np.pi * 220.0 * t) + 0.4 * np.sin(2 * np.pi * 1310.0 * t)
           + 0.15 * rng.normal(0, 1, n))
    dump("signal", sig)

    # --- FFT ---------------------------------------------------------------
    spec = np.fft.rfft(sig, 4096)
    dump("rfft_real", spec.real)
    dump("rfft_imag", spec.imag)
    dump("irfft", np.fft.irfft(spec, 4096))

    # --- Butterworth designs ----------------------------------------------
    designs = {
        "lp3_0p10": sg.butter(3, 0.10, btype="lowpass", output="sos"),
        "lp2_0p35": sg.butter(2, 0.35, btype="lowpass", output="sos"),
        "lp4_0p05": sg.butter(4, 0.05, btype="lowpass", output="sos"),
        "hp4_0p02": sg.butter(4, 0.02, btype="highpass", output="sos"),
        "hp2_0p004": sg.butter(2, 0.004, btype="highpass", output="sos"),
        "bp3_0p02_0p04": sg.butter(3, [0.02, 0.04], btype="bandpass", output="sos"),
        "bp2_0p10_0p30": sg.butter(2, [0.10, 0.30], btype="bandpass", output="sos"),
        "bp4_0p18_0p95": sg.butter(4, [0.18, 0.95], btype="bandpass", output="sos"),
    }
    for name, sos in designs.items():
        dump(f"sos_{name}", sos)
        dump(f"sosfilt_{name}", sg.sosfilt(sos, sig))
        dump(f"sosfiltfilt_{name}", sg.sosfiltfilt(sos, sig))
        dump(f"sosfilt_zi_{name}", sg.sosfilt_zi(sos))

    # --- dsp helpers -------------------------------------------------------
    from vocalai.audio.dsp import (
        frame_signal,
        interpolate_gaps,
        median_filter_nan,
        smooth,
    )

    short = sig[:600]
    dump("frames_512_128", frame_signal(short, 512, 128).reshape(-1))
    dump("frames_512_128_shape", [frame_signal(short, 512, 128).shape[0]])
    dump("reflect_pad_37", np.pad(short, 37, mode="reflect"))

    curve = np.linspace(0.0, 5.0, 400) + 0.4 * np.sin(np.arange(400) * 0.3)
    holed = curve.copy()
    holed[50:53] = np.nan      # short gap: bridged
    holed[200:230] = np.nan    # long gap: left alone
    holed[:4] = np.nan         # leading edge: never extrapolated
    dump("gaps_input", np.nan_to_num(holed, nan=-999.0))
    dump("gaps_bridged", np.nan_to_num(interpolate_gaps(holed, max_gap=5), nan=-999.0))
    dump("smooth_9", np.nan_to_num(smooth(holed, 9), nan=-999.0))
    dump("median_7", np.nan_to_num(median_filter_nan(holed, 7), nan=-999.0))

    xq = np.linspace(-1.0, 401.0, 233)
    dump("interp_x", xq)
    dump("interp_y", np.interp(xq, np.arange(400), curve))

    # --- a sung phrase, and what the Python engine reads from it -----------
    from vocalai.analysis.pitch import track_f0
    from vocalai.audio.io import write_wav
    from vocalai.testing.synth import melody, render_voice

    audio, _, _ = render_voice(
        melody([60, 62, 64, 65, 67, 65, 64, 60], dur=0.42,
               detune=[28, -25, 18, -30, 22, -20, 26, -22], vibrato=35.0, seed=7)
    )
    write_wav(OUT / "voice.wav", audio, SR, bits=24)
    dump("voice_audio", audio)

    pt = track_f0(audio, SR)
    dump("pitch_f0", np.nan_to_num(pt.f0, nan=-1.0))
    dump("pitch_voiced_prob", pt.voiced_prob)
    dump("pitch_periodicity", pt.periodicity)

    # One CMNDF row, to check the difference function on its own.
    from vocalai.analysis.pitch import cmndf
    from vocalai.audio.dsp import frame_signal
    frames = frame_signal(audio, 2048, 256)
    dump("cmndf_row_40", cmndf(frames[40:41], 1024).reshape(-1))
    dump("cmndf_frame_40", frames[40])

    # --- PSOLA -------------------------------------------------------------
    from vocalai.process.psola import detect_epochs, psola_modify, wsola_time_warp

    ep = detect_epochs(audio, SR, pt.f0, pt.hop)
    dump("epoch_marks", ep.marks.astype(np.float64))
    dump("epoch_periods", ep.periods)
    for tag, cents in (("up100", 100.0), ("dn150", -150.0)):
        ratio = np.full(pt.f0.size, 2.0 ** (cents / 1200.0))
        dump(f"psola_{tag}", psola_modify(audio, SR, ep, ratio, pt.hop))

    identity = np.arange(len(audio), dtype=float)
    dump("wsola_identity", wsola_time_warp(audio, SR, identity))
    dump("wsola_stretch", wsola_time_warp(audio, SR, np.arange(int(len(audio) * 1.3)) / 1.3))

    # --- note segmentation -------------------------------------------------
    from vocalai.analysis.notes import segment_notes

    ns = segment_notes(pt, audio)
    flat = []
    for nn in ns.notes:
        flat += [nn.start, nn.end, nn.core_start, nn.core_end, nn.center_hz,
                 nn.vibrato_hz, nn.vibrato_cents, nn.drift_cents, nn.confidence]
    dump("notes_flat", flat)
    dump("notes_count", [len(ns.notes)])

    print(f"wrote reference vectors to {OUT}/")


if __name__ == "__main__":
    main()
