"""PSOLA, WSOLA, alignment and the tuner - the stages that change audio."""

import unittest

import numpy as np
from scipy import signal as sg

from helpers import SR, steady, sung
from vocalai.analysis.features import onset_envelope, pick_onsets, spectrogram
from vocalai.analysis.key import key_from_name
from vocalai.analysis.pitch import track_f0
from vocalai.pipeline import analyze_track, tune_track
from vocalai.process.align import AlignConfig, align_to_guide
from vocalai.process.psola import detect_epochs, psola_modify, wsola_time_warp
from vocalai.process.tuner import TuneConfig


def onsets(x, delta=0.06):
    spec = spectrogram(x, SR, hop=256)
    return pick_onsets(onset_envelope(spec), spec.times, delta=delta)


def envelope_peaks(y, n=2):
    """Formant frequencies, via a cepstrally smoothed spectral envelope."""
    _, power = sg.welch(y, SR, nperseg=4096)
    log = np.log(power + 1e-12)
    ceps = np.fft.irfft(log)
    ceps[40:-40] = 0.0
    env = np.real(np.fft.rfft(ceps))
    freqs = np.fft.rfftfreq(4096, 1.0 / SR)[: env.size]
    peaks, _ = sg.find_peaks(env, distance=8)
    return freqs[peaks][:n]


class TestEpochs(unittest.TestCase):
    def test_one_mark_per_cycle_across_the_range(self):
        # Marks spaced 2T apart on any note would render that note an octave
        # down, so the spacing is checked against the period at each pitch.
        for midi in (45, 57, 69, 76):
            with self.subTest(midi=midi):
                audio, _ = steady(midi=float(midi), duration=0.8)
                pt = track_f0(audio, SR)
                ep = detect_epochs(audio, SR, pt.f0, pt.hop)
                self.assertGreater(ep.marks.size, 20)
                spacing = np.diff(ep.marks)
                expected = SR / (440.0 * 2.0 ** ((midi - 69) / 12.0))
                self.assertLess(abs(float(np.median(spacing)) - expected), 0.03 * expected)
                spread = np.percentile(spacing, 95) - np.percentile(spacing, 5)
                self.assertLess(spread, 0.12 * expected)


class TestPsola(unittest.TestCase):
    def test_pitch_shift_is_accurate(self):
        audio, _ = steady(midi=57.0, duration=1.2)
        pt = track_f0(audio, SR)
        ep = detect_epochs(audio, SR, pt.f0, pt.hop)
        for cents in (-200, -100, -50, 50, 100, 200, 700):
            with self.subTest(cents=cents):
                ratio = np.full(pt.f0.size, 2.0 ** (cents / 1200.0))
                out = psola_modify(audio, SR, ep, ratio, pt.hop)
                got = track_f0(out, SR)
                ok = np.isfinite(got.f0) & np.isfinite(pt.f0)
                shift = float(np.median(1200.0 * np.log2(got.f0[ok] / pt.f0[ok])))
                self.assertLess(abs(shift - cents), 8.0)

    def test_length_is_unchanged(self):
        audio, _ = steady(duration=0.8)
        pt = track_f0(audio, SR)
        ep = detect_epochs(audio, SR, pt.f0, pt.hop)
        out = psola_modify(audio, SR, ep, np.full(pt.f0.size, 1.06), pt.hop)
        self.assertEqual(out.size, audio.size)

    def test_formants_stay_put_unlike_resampling(self):
        # This is the whole reason for using PSOLA rather than a speed change.
        audio, _ = steady(midi=57.0, duration=1.5)
        pt = track_f0(audio, SR)
        ep = detect_epochs(audio, SR, pt.f0, pt.hop)
        shifted = psola_modify(audio, SR, ep, np.full(pt.f0.size, 2.0 ** (7 / 12)), pt.hop)
        resampled = sg.resample(audio, int(audio.size / 2.0 ** (7 / 12)))

        base = envelope_peaks(audio[SR // 2 : SR])[0]
        psola_f1 = envelope_peaks(shifted[SR // 2 : SR])[0]
        naive_f1 = envelope_peaks(resampled[SR // 2 : SR])[0]
        self.assertLess(abs(psola_f1 / base - 1.0), 0.15)
        self.assertGreater(abs(naive_f1 / base - 1.0), 0.25)


class TestWsola(unittest.TestCase):
    def test_identity_warp_reproduces_the_input(self):
        # A no-op must actually be a no-op, at any search tolerance: if the
        # similarity search may drift, alignment silently loses accuracy.
        audio, _ = sung([60, 62, 64], dur=0.4, detune=[0] * 3)
        identity = np.arange(audio.size, dtype=float)
        for tol in (0, 8, 100, 352):
            with self.subTest(tolerance=tol):
                out = wsola_time_warp(audio, SR, identity, tolerance=tol)
                self.assertGreater(float(np.corrcoef(out, audio)[0, 1]), 0.999)

    def test_stretch_changes_length_but_not_pitch(self):
        audio, _ = steady(duration=1.0)
        out = wsola_time_warp(audio, SR, np.arange(int(audio.size * 1.5)) / 1.5)
        self.assertAlmostEqual(out.size / audio.size, 1.5, places=2)
        before = np.nanmedian(track_f0(audio, SR).f0)
        after = np.nanmedian(track_f0(out, SR).f0)
        self.assertLess(abs(1200.0 * np.log2(after / before)), 10.0)


class TestAlignment(unittest.TestCase):
    def _pair(self, jitter=0.06, seed=4):
        import copy

        from vocalai.testing.synth import melody, render_voice

        spec = melody([60, 62, 64, 65, 67, 65, 64, 60], dur=0.5, detune=[0] * 8, vibrato=25.0, seed=1)
        guide, _, _ = render_voice(spec)
        dub_spec = copy.deepcopy(spec)
        rng = np.random.default_rng(seed)
        for n in dub_spec.notes:
            n.start += float(rng.uniform(-jitter, jitter))
        dub, _, _ = render_voice(dub_spec)
        return guide, dub

    def test_aligning_a_track_to_itself_changes_nothing(self):
        guide, _ = self._pair()
        out, result = align_to_guide(guide, guide, SR)
        self.assertLess(result.stats["max_shift_ms"], 1.0)
        self.assertGreater(float(np.corrcoef(out, guide)[0, 1]), 0.999)

    def test_alignment_pulls_onsets_onto_the_guide(self):
        guide, dub = self._pair(jitter=0.06)
        g_on = onsets(guide)
        before = np.array([np.min(np.abs(onsets(dub) - t)) for t in g_on]) * 1000.0
        out, _ = align_to_guide(dub, guide, SR)
        after = np.array([np.min(np.abs(onsets(out) - t)) for t in g_on]) * 1000.0
        self.assertGreater(before.mean(), 15.0)
        self.assertLess(after.mean(), 12.0)
        self.assertLess(after.mean(), before.mean() * 0.6)
        self.assertEqual(out.size, guide.size)

    def test_strength_scales_the_correction(self):
        guide, dub = self._pair(jitter=0.06)
        g_on = onsets(guide)

        def err(x):
            return float(np.mean([np.min(np.abs(onsets(x) - t)) for t in g_on])) * 1000.0

        full, _ = align_to_guide(dub, guide, SR, cfg=AlignConfig(strength=1.0))
        half, _ = align_to_guide(dub, guide, SR, cfg=AlignConfig(strength=0.5))
        none, _ = align_to_guide(dub, guide, SR, cfg=AlignConfig(strength=0.0))
        self.assertLess(err(full), err(half))
        self.assertLess(err(half), err(none))


class TestTuner(unittest.TestCase):
    def test_correction_reduces_pitch_error(self):
        midis = [60, 62, 64, 65, 67, 65, 64, 62, 60]
        detune = [30, -28, 22, -35, 26, -24, 31, -27, 19]
        audio, _ = sung(midis, dur=0.5, detune=detune, vibrato=35.0, seed=7)
        an = analyze_track(audio, SR)
        out, result = tune_track(an, TuneConfig.style("natural"), key="C major")
        self.assertLess(result.stats["mean_error_after_cents"], 6.0)
        self.assertLess(
            result.stats["mean_error_after_cents"],
            result.stats["mean_error_before_cents"] * 0.4,
        )
        after = analyze_track(out, SR)
        residual = [abs(n.cents_off) for n in after.notes]
        self.assertLess(float(np.mean(residual)), 12.0)

    def test_natural_keeps_vibrato_and_hard_removes_it(self):
        midis = [60, 62, 64, 65, 67]
        audio, _ = sung(midis, dur=0.6, detune=[25, -30, 20, -28, 22], vibrato=45.0, seed=3)
        an = analyze_track(audio, SR)
        source_vib = float(np.mean([n.vibrato_cents for n in an.notes]))
        self.assertGreater(source_vib, 10.0)

        natural, _ = tune_track(analyze_track(audio, SR), TuneConfig.style("natural"), key="C major")
        hard, _ = tune_track(analyze_track(audio, SR), TuneConfig.style("hard"), key="C major")
        nat_vib = float(np.mean([n.vibrato_cents for n in analyze_track(natural, SR).notes]))
        hard_vib = float(np.mean([n.vibrato_cents for n in analyze_track(hard, SR).notes]))
        self.assertGreater(nat_vib, source_vib * 0.6)
        self.assertLess(hard_vib, nat_vib * 0.6)

    def test_a_uniformly_flat_phrase_keeps_its_melody(self):
        # Correcting each note toward the nearest semitone independently would
        # be fine here; the risk is a decoder that "explains" the offset by
        # rewriting the tune.
        midis = [62, 64, 65, 67, 69, 67, 65, 64, 62]
        audio, _ = sung(midis, dur=0.45, detune=[-42.0] * 9, vibrato=25.0, seed=3)
        an = analyze_track(audio, SR)
        _, result = tune_track(an, TuneConfig.style("natural"), key="D minor", render=False)
        self.assertEqual([int(n.target_midi) for n in an.notes], midis)

    def test_locked_notes_are_left_alone(self):
        midis = [60, 62, 64]
        audio, _ = sung(midis, dur=0.5, detune=[40, -40, 40], vibrato=0.0, seed=2)
        an = analyze_track(audio, SR)
        an.notes.notes[1].locked = True
        out, _ = tune_track(an, TuneConfig.style("tight"), key="C major")
        after = analyze_track(out, SR)
        self.assertEqual(len(after.notes), 3)
        # The locked note keeps its original error; its neighbours do not.
        self.assertGreater(abs(after.notes[1].cents_off), 25.0)
        self.assertLess(abs(after.notes[0].cents_off), 15.0)

    def test_styles_differ_in_how_much_they_correct(self):
        midis = [60, 62, 64, 65, 67]
        audio, _ = sung(midis, dur=0.5, detune=[35, -32, 28, -30, 33], vibrato=30.0, seed=5)
        residual = {}
        for style in ("transparent", "natural", "tight"):
            an = analyze_track(audio, SR)
            _, res = tune_track(an, TuneConfig.style(style), key="C major", render=False)
            residual[style] = res.stats["mean_error_after_cents"]
        self.assertGreater(residual["transparent"], residual["natural"])
        self.assertGreater(residual["natural"], residual["tight"])


if __name__ == "__main__":
    unittest.main()
