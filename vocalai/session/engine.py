"""Run the whole session: find the vocals, align the stacks, tune everything.

The order mirrors how the work is done by hand.  Read every track first and
decide what it is.  Take the session's concert pitch from the instruments,
because a singer who is uniformly flat is not evidence that the band is.  Work
out the key from everything that has pitch.  Then align the doubles and
harmonies to the lead, and only then tune - so that tuning operates on the
audio that will actually be heard.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from ..analysis.key import KeyEstimate, estimate_key, estimate_tuning, key_from_name
from ..analysis.voice import TrackProfile, group_vocals, profile_track
from ..audio.dsp import resample_poly
from ..audio.io import AudioBuffer, read_wav, write_buffer
from ..model import NoteSequence
from ..pipeline import TrackAnalysis, analyze_track, render_correction
from ..process.align import AlignConfig, AlignResult, align_to_guide
from ..process.tuner import TuneConfig, TuneResult, tune
from .scan import TrackFile, scan_session


@dataclass
class SessionConfig:
    """Everything the engine needs to know before it touches a session."""

    style: str = "natural"
    tune: TuneConfig = field(default_factory=TuneConfig)
    align: AlignConfig = field(default_factory=AlignConfig)
    do_align: bool = True
    do_tune: bool = True
    key: str | None = None            # force a key, e.g. "F# minor"
    tuning_hz: float | None = None    # force concert pitch
    hop: int = 256
    vocal_threshold: float = 0.55
    include: list[str] | None = None  # only process tracks whose name matches
    exclude: list[str] | None = None
    output_suffix: str = "_tuned"
    write_audio: bool = True
    bit_depth: int | None = None

    @classmethod
    def from_style(cls, style: str, **kw) -> "SessionConfig":
        return cls(style=style, tune=TuneConfig.style(style), **kw)


@dataclass
class TrackResult:
    """What happened to one track."""

    name: str
    path: Path
    profile: TrackProfile
    analysis: TrackAnalysis | None = None
    tune_result: TuneResult | None = None
    align_result: AlignResult | None = None
    output_path: Path | None = None
    processed: bool = False
    note_count: int = 0
    skipped_reason: str = ""

    def summary(self) -> str:
        if not self.processed:
            return f"{self.name:<24} {self.profile.role:<10} skipped ({self.skipped_reason})"
        bits = [f"{self.name:<24} {self.profile.role:<10} {self.note_count:3d} notes"]
        if self.tune_result:
            s = self.tune_result.stats
            bits.append(
                f"tuned {s['mean_error_before_cents']:5.1f}c -> "
                f"{s['mean_error_after_cents']:4.1f}c"
            )
        if self.align_result:
            bits.append(f"aligned +-{self.align_result.stats['median_abs_shift_ms']:.0f}ms")
        return "  ".join(bits)


@dataclass
class SessionResult:
    """The whole session's outcome."""

    tracks: list[TrackResult] = field(default_factory=list)
    key: KeyEstimate | None = None
    lead: str | None = None
    output_dir: Path | None = None

    @property
    def vocals(self) -> list[TrackResult]:
        return [t for t in self.tracks if t.profile.is_vocal]

    def summary(self) -> str:
        head = []
        if self.key:
            head.append(
                f"key {self.key.name} (confidence {self.key.confidence:.2f}), "
                f"concert pitch {self.key.tuning_hz:.2f} Hz"
            )
        if self.lead:
            head.append(f"lead vocal: {self.lead}")
        lines = head + [t.summary() for t in self.tracks]
        return "\n".join(lines)


def _matches(name: str, patterns: list[str] | None) -> bool:
    if not patterns:
        return False
    low = name.lower()
    return any(p.lower() in low for p in patterns)


def process_session(
    target: str | Path,
    output_dir: str | Path | None = None,
    config: SessionConfig | None = None,
    progress=None,
) -> SessionResult:
    """Analyse, align and tune every vocal track in a session folder."""
    cfg = config or SessionConfig()
    files = scan_session(target)
    if not files:
        raise FileNotFoundError(f"no audio files found in {target}")

    out_dir = Path(output_dir) if output_dir else Path(target) / "vocalai_out"
    result = SessionResult(output_dir=out_dir)

    def say(msg: str) -> None:
        if progress:
            progress(msg)

    # --- 1. read and analyse every track -------------------------------------
    buffers: dict[str, AudioBuffer] = {}
    analyses: dict[str, TrackAnalysis] = {}
    profiles: dict[str, TrackProfile] = {}
    envelopes: dict[str, np.ndarray] = {}
    sequences: dict[str, NoteSequence] = {}

    for tf in files:
        if cfg.include and not _matches(tf.name, cfg.include):
            continue
        if _matches(tf.name, cfg.exclude):
            continue
        say(f"analysing {tf.name}")
        buf = read_wav(tf.path)
        analysis = analyze_track(buf, name=tf.name, hop=cfg.hop)
        profile = profile_track(
            tf.name, analysis.audio, analysis.sample_rate, analysis.pitch,
            analysis.spec, analysis.notes,
        )
        _apply_name_prior(profile, tf, cfg.vocal_threshold)

        buffers[tf.name] = buf
        analyses[tf.name] = analysis
        profiles[tf.name] = profile
        sequences[tf.name] = analysis.notes
        envelopes[tf.name] = np.sqrt(np.sum(analysis.spec.mag**2, axis=1))

    if not analyses:
        raise ValueError("every track was filtered out by include/exclude")

    # --- 2. concert pitch, preferring the instruments ------------------------
    if cfg.tuning_hz is not None:
        tuning = float(cfg.tuning_hz)
    else:
        tuning = _session_tuning(profiles, sequences)

    # --- 3. key, from everything that has pitch ------------------------------
    pitched = [sequences[n] for n, p in profiles.items() if p.median_f0 > 0]
    if cfg.key:
        key = key_from_name(cfg.key, tuning_hz=tuning)
    else:
        key = estimate_key(pitched or list(sequences.values()), tuning_hz=tuning)
    key.tuning_hz = tuning
    result.key = key
    say(f"key {key.name}, concert pitch {tuning:.2f} Hz")

    # --- 4. who is the lead, and who is stacked on it ------------------------
    name_roles = {tf.name: tf.name_role for tf in files if tf.name_role}
    group_vocals(profiles, envelopes, sequences, name_roles=name_roles)
    lead_name = next((n for n, p in profiles.items() if p.role == "lead"), None)
    result.lead = lead_name

    # --- 5. align the stacks, then tune everything ---------------------------
    for tf in files:
        if tf.name not in analyses:
            continue
        profile = profiles[tf.name]
        tr = TrackResult(name=tf.name, path=tf.path, profile=profile)

        if not profile.is_vocal:
            tr.skipped_reason = "not a vocal"
            result.tracks.append(tr)
            continue

        analysis = analyses[tf.name]
        buf = buffers[tf.name]
        audio = buf.samples.copy()
        align_result = None

        if (
            cfg.do_align
            and lead_name
            and tf.name != lead_name
            and profile.role in ("double", "harmony")
        ):
            say(f"aligning {tf.name} to {lead_name}")
            audio, align_result = _align_track(
                buf, buffers[lead_name], analyses[lead_name], cfg
            )
            # Re-read the aligned audio: its notes now sit at new times.
            analysis = analyze_track(
                AudioBuffer(audio, buf.sample_rate, source_bits=buf.source_bits,
                            source_float=buf.source_float),
                name=tf.name, hop=cfg.hop,
            )

        tune_result = None
        if cfg.do_tune:
            say(f"tuning {tf.name}")
            tune_result = tune(analysis.notes, analysis.pitch, key, cfg.tune)
            audio = render_correction(analysis, tune_result.ratio)

        tr.analysis = analysis
        tr.tune_result = tune_result
        tr.align_result = align_result
        tr.note_count = len(analysis.notes)
        tr.processed = True

        if cfg.write_audio:
            out_path = out_dir / f"{tf.name}{cfg.output_suffix}.wav"
            out_buf = AudioBuffer(
                audio, buf.sample_rate, source_bits=buf.source_bits,
                source_float=buf.source_float,
            )
            write_buffer(out_path, out_buf, bits=cfg.bit_depth)
            tr.output_path = out_path
        result.tracks.append(tr)

    return result


def _apply_name_prior(profile: TrackProfile, tf: TrackFile, threshold: float) -> None:
    """Let the filename break ties the audio left close to the line."""
    if tf.name_vocal_prior > 0:
        profile.score = min(1.0, profile.score + 0.08)
    elif tf.name_vocal_prior < 0:
        profile.score = max(0.0, profile.score - 0.08)
    profile.is_vocal = profile.score >= threshold
    profile.role = "unknown" if profile.is_vocal else "instrument"


def _session_tuning(
    profiles: dict[str, TrackProfile], sequences: dict[str, NoteSequence]
) -> float:
    """Concert pitch, read from the instruments where possible.

    A vocal that is uniformly flat looks exactly like a session tuned flat, and
    tuning the whole song to the singer's error is the wrong answer.  The band
    settles it; the voices are only consulted when nothing else is playing.
    """
    inst = [
        sequences[n]
        for n, p in profiles.items()
        if not p.is_vocal and p.median_f0 > 0 and len(sequences[n]) >= 6
    ]
    if inst:
        return estimate_tuning(inst)
    return estimate_tuning(list(sequences.values()))


def _align_track(
    dub: AudioBuffer, lead: AudioBuffer, lead_analysis: TrackAnalysis, cfg: SessionConfig
) -> tuple[np.ndarray, AlignResult]:
    """Warp one stacked vocal onto the lead, preserving its channel layout."""
    guide = lead_analysis.audio
    if dub.sample_rate != lead.sample_rate:
        guide = resample_poly(guide, lead.sample_rate, dub.sample_rate)

    mono = dub.mono()
    _, align_result = align_to_guide(mono, guide, dub.sample_rate, hop=cfg.hop, cfg=cfg.align)

    # Render every channel through the one warp so the image stays intact.
    out_len = guide.size
    time_map = np.interp(
        np.arange(out_len) / cfg.hop, np.arange(align_result.warp.size), align_result.warp
    ) * cfg.hop
    time_map = np.clip(time_map, 0, max(dub.frames - 1, 0))

    from ..analysis.features import onset_envelope, pick_onsets, spectrogram
    from ..process.psola import wsola_time_warp

    spec = spectrogram(mono, dub.sample_rate, hop=cfg.hop)
    transients = pick_onsets(onset_envelope(spec), spec.times, delta=0.05) * dub.sample_rate

    out = np.zeros((out_len, dub.channels))
    for ch in range(dub.channels):
        out[:, ch] = wsola_time_warp(
            dub.samples[:, ch], dub.sample_rate, time_map, transients=transients
        )
    return out, align_result
