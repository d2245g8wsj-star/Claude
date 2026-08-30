"""A small singing-voice synthesiser used for tests, demos and benchmarks.

It is deliberately physical rather than pretty: a Rosenberg glottal pulse
train excites a cascade of formant resonators, with jitter, shimmer, vibrato
and breath noise on top, and consonants rendered as shaped noise bursts.  The
point is to produce signals with *known* ground-truth pitch so the tracker and
the tuner can be scored objectively.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy import signal

# (F1, F2, F3, F4) in Hz for a handful of sung vowels.
VOWELS: dict[str, tuple[float, ...]] = {
    "a": (730, 1090, 2440, 3400),
    "e": (530, 1840, 2480, 3500),
    "i": (270, 2290, 3010, 3700),
    "o": (570, 840, 2410, 3300),
    "u": (300, 870, 2240, 3200),
}
BANDWIDTHS = (80.0, 100.0, 140.0, 200.0)


@dataclass
class NoteSpec:
    """One sung note: where it sits, how long, and how it is performed."""

    midi: float
    start: float
    duration: float
    vowel: str = "a"
    level: float = 1.0
    detune_cents: float = 0.0          # steady-state pitch error
    onset_cents: float = 0.0           # scoop / fall into the note
    onset_time: float = 0.06           # how long that approach takes
    vibrato_hz: float = 5.4
    vibrato_cents: float = 0.0
    vibrato_delay: float = 0.25
    drift_cents: float = 0.0           # slow sag across the note


@dataclass
class VoiceSpec:
    """A whole vocal performance."""

    notes: list[NoteSpec] = field(default_factory=list)
    sample_rate: int = 44100
    jitter: float = 0.004              # cycle-to-cycle period noise
    shimmer: float = 0.05              # cycle-to-cycle amplitude noise
    breath: float = 0.004              # aspiration noise level
    consonants: bool = True
    seed: int = 0


def _rosenberg(phase: np.ndarray, open_quotient: float = 0.6) -> np.ndarray:
    """Rosenberg glottal flow derivative over a 0..1 normalised cycle."""
    out = np.zeros_like(phase)
    tp = open_quotient * 0.75
    tn = open_quotient
    rise = phase < tp
    fall = (phase >= tp) & (phase < tn)
    out[rise] = 0.5 * (1.0 - np.cos(np.pi * phase[rise] / tp))
    out[fall] = np.cos(0.5 * np.pi * (phase[fall] - tp) / max(tn - tp, 1e-6))
    return out - out.mean()


def _formant_filter(x: np.ndarray, sample_rate: int, formants: tuple[float, ...]) -> np.ndarray:
    y = x
    for f, bw in zip(formants, BANDWIDTHS):
        if f >= sample_rate / 2.0 * 0.95:
            continue
        r = np.exp(-np.pi * bw / sample_rate)
        theta = 2.0 * np.pi * f / sample_rate
        a = [1.0, -2.0 * r * np.cos(theta), r * r]
        b = [(1.0 - r) * np.sqrt(1.0 - 2.0 * r * np.cos(2 * theta) + r * r)]
        y = signal.lfilter(b, a, y)
    return y


def _pitch_contour(note: NoteSpec, n: int, sample_rate: int, rng) -> np.ndarray:
    """Cents offset from the nominal MIDI pitch, sample by sample."""
    t = np.arange(n) / sample_rate
    dur = max(note.duration, 1e-6)
    cents = np.full(n, note.detune_cents)

    if note.onset_cents and note.onset_time > 0:
        ramp = np.clip(t / note.onset_time, 0.0, 1.0)
        cents = cents + note.onset_cents * (1.0 - ramp) ** 2

    if note.vibrato_cents:
        depth = np.clip((t - note.vibrato_delay) / 0.15, 0.0, 1.0)
        cents = cents + note.vibrato_cents * depth * np.sin(2 * np.pi * note.vibrato_hz * t)

    if note.drift_cents:
        cents = cents + note.drift_cents * (t / dur)
    return cents


def render_voice(spec: VoiceSpec) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Render a performance.

    Returns ``(audio, f0_truth, time)`` where ``f0_truth`` is the exact
    instantaneous F0 per sample (NaN where nothing is sung).
    """
    sr = spec.sample_rate
    rng = np.random.default_rng(spec.seed)
    if not spec.notes:
        return np.zeros(1), np.full(1, np.nan), np.zeros(1)

    end = max(n.start + n.duration for n in spec.notes) + 0.4
    total = int(end * sr)
    audio = np.zeros(total)
    truth = np.full(total, np.nan)

    for note in spec.notes:
        i0 = int(note.start * sr)
        n = int(note.duration * sr)
        if n <= 0 or i0 >= total:
            continue
        n = min(n, total - i0)
        base = 440.0 * 2.0 ** ((note.midi - 69.0) / 12.0)
        cents = _pitch_contour(note, n, sr, rng)
        f0 = base * 2.0 ** (cents / 1200.0)

        # Integrate instantaneous frequency, adding period jitter per cycle.
        phase = np.cumsum(f0) / sr
        if spec.jitter:
            phase = phase + np.cumsum(rng.normal(0.0, spec.jitter, n) * f0 / sr)
        cycle = np.mod(phase, 1.0)
        glottal = _rosenberg(cycle)

        if spec.shimmer:
            cycle_idx = np.floor(phase).astype(np.int64)
            gains = 1.0 + rng.normal(0.0, spec.shimmer, cycle_idx.max() + 2)
            glottal = glottal * gains[cycle_idx - cycle_idx.min()]

        excite = glottal + spec.breath * rng.normal(0.0, 1.0, n)
        voiced = _formant_filter(excite, sr, VOWELS.get(note.vowel, VOWELS["a"]))

        env = _envelope(n, sr, note.duration)
        seg = voiced * env * note.level

        if spec.consonants and note.duration > 0.12:
            seg = _add_consonant(seg, sr, rng)

        audio[i0 : i0 + n] += seg
        truth[i0 : i0 + n] = np.where(env > 0.05, f0, np.nan)

    peak = np.max(np.abs(audio))
    if peak > 0:
        audio = audio / peak * 0.7
    return audio, truth, np.arange(total) / sr


def _envelope(n: int, sample_rate: int, duration: float) -> np.ndarray:
    attack = int(min(0.03, duration * 0.2) * sample_rate)
    release = int(min(0.06, duration * 0.3) * sample_rate)
    env = np.ones(n)
    if attack > 0:
        env[:attack] = np.linspace(0.0, 1.0, attack) ** 1.5
    if release > 0:
        env[-release:] = np.linspace(1.0, 0.0, release) ** 1.5
    return env


def _add_consonant(seg: np.ndarray, sample_rate: int, rng) -> np.ndarray:
    """Prepend a short sibilant so the material has genuine unvoiced frames."""
    n = int(0.045 * sample_rate)
    if n >= seg.size:
        return seg
    noise = rng.normal(0.0, 1.0, n)
    sos = signal.butter(4, [4000 / (sample_rate / 2), 0.95], btype="bandpass", output="sos")
    hiss = signal.sosfilt(sos, noise) * np.hanning(n) * 0.25 * np.max(np.abs(seg) + 1e-9)
    out = seg.copy()
    out[:n] = out[:n] * np.linspace(0.0, 1.0, n) + hiss
    return out


def melody(
    midis: list[float],
    dur: float = 0.55,
    gap: float = 0.05,
    start: float = 0.2,
    detune: list[float] | None = None,
    vibrato: float = 35.0,
    vowels: str = "aeiou",
    seed: int = 0,
) -> VoiceSpec:
    """Build a simple sung line, optionally with per-note tuning errors."""
    rng = np.random.default_rng(seed)
    notes = []
    t = start
    for i, m in enumerate(midis):
        off = detune[i] if detune is not None else float(rng.normal(0.0, 28.0))
        notes.append(
            NoteSpec(
                midi=m,
                start=t,
                duration=dur,
                vowel=vowels[i % len(vowels)],
                detune_cents=off,
                onset_cents=float(rng.uniform(-90.0, -20.0)),
                vibrato_cents=vibrato,
                vibrato_hz=float(rng.uniform(4.8, 6.2)),
                drift_cents=float(rng.normal(0.0, 12.0)),
            )
        )
        t += dur + gap
    return VoiceSpec(notes=notes, seed=seed)


# --- non-vocal material, for testing that the classifier leaves it alone -----


def render_plucked(
    midis: list[float], dur: float = 0.5, gap: float = 0.05, start: float = 0.2,
    sample_rate: int = 44100, decay: float = 3.0, seed: int = 0, poly: bool = False,
) -> np.ndarray:
    """Karplus-Strong style plucked string: pitched, but no glides or consonants."""
    rng = np.random.default_rng(seed)
    total = int((start + len(midis) * (dur + gap) + 0.5) * sample_rate)
    out = np.zeros(total)
    t = start
    for m in midis:
        f = 440.0 * 2.0 ** ((m - 69.0) / 12.0)
        n = int(dur * sample_rate)
        i0 = int(t * sample_rate)
        buf = rng.normal(0.0, 1.0, int(sample_rate / f) + 1)
        y = np.zeros(n)
        L = buf.size
        for i in range(n):
            y[i] = buf[i % L]
            buf[i % L] = 0.5 * (buf[i % L] + buf[(i + 1) % L]) * 0.999
        env = np.exp(-decay * np.arange(n) / sample_rate)
        seg = y * env
        if i0 + n <= total:
            out[i0 : i0 + n] += seg
        t += dur + gap
    out = signal.sosfilt(
        signal.butter(2, 70.0 / (sample_rate / 2), btype="highpass", output="sos"), out
    )
    peak = np.max(np.abs(out))
    return out / peak * 0.6 if peak > 0 else out


def render_drums(bars: int = 4, bpm: float = 96.0, sample_rate: int = 44100,
                 seed: int = 0) -> np.ndarray:
    """Kick / snare / hat pattern: broadband, unpitched, transient-heavy."""
    rng = np.random.default_rng(seed)
    beat = 60.0 / bpm
    total = int((bars * 4 * beat + 0.5) * sample_rate)
    out = np.zeros(total)

    def place(idx: int, sig: np.ndarray) -> None:
        if idx + sig.size <= total:
            out[idx : idx + sig.size] += sig

    for b in range(bars * 4):
        t = int(b * beat * sample_rate)
        if b % 4 in (0, 2):
            n = int(0.18 * sample_rate)
            env = np.exp(-14.0 * np.arange(n) / sample_rate)
            f = 110.0 * np.exp(-24.0 * np.arange(n) / sample_rate) + 42.0
            place(t, np.sin(2 * np.pi * np.cumsum(f) / sample_rate) * env)
        if b % 4 == 2:
            n = int(0.16 * sample_rate)
            env = np.exp(-22.0 * np.arange(n) / sample_rate)
            place(t, (rng.normal(0, 1, n) * 0.7 + np.sin(2 * np.pi * 190 * np.arange(n) / sample_rate)) * env * 0.6)
        n = int(0.05 * sample_rate)
        env = np.exp(-60.0 * np.arange(n) / sample_rate)
        hat = signal.sosfilt(
            signal.butter(4, 6000 / (sample_rate / 2), btype="highpass", output="sos"),
            rng.normal(0, 1, n),
        )
        place(t, hat * env * 0.25)
        place(t + int(beat * sample_rate / 2), hat * env * 0.18)
    peak = np.max(np.abs(out))
    return out / peak * 0.7 if peak > 0 else out


def render_bass(midis: list[float], dur: float = 0.5, gap: float = 0.0, start: float = 0.2,
                sample_rate: int = 44100) -> np.ndarray:
    """Sub-heavy synth bass: pitched and sustained, but all its energy is low."""
    total = int((start + len(midis) * (dur + gap) + 0.5) * sample_rate)
    out = np.zeros(total)
    t = start
    for m in midis:
        f = 440.0 * 2.0 ** ((m - 69.0) / 12.0)
        n = int(dur * sample_rate)
        i0 = int(t * sample_rate)
        ph = 2 * np.pi * f * np.arange(n) / sample_rate
        sig = np.sin(ph) + 0.3 * np.sin(2 * ph) + 0.12 * np.sin(3 * ph)
        env = np.minimum(1.0, np.arange(n) / (0.01 * sample_rate)) * np.exp(
            -1.2 * np.arange(n) / sample_rate
        )
        if i0 + n <= total:
            out[i0 : i0 + n] += sig * env
        t += dur + gap
    peak = np.max(np.abs(out))
    return out / peak * 0.75 if peak > 0 else out
