"""Pitch tracking, note segmentation, key detection - the reading stage."""

import unittest

import numpy as np

from helpers import SR, steady, sung
from vocalai.analysis.key import estimate_key, estimate_tuning, key_from_name
from vocalai.analysis.notes import segment_notes
from vocalai.analysis.pitch import track_f0
from vocalai.audio.dsp import interpolate_gaps, nan_runs, resample_poly


class TestDsp(unittest.TestCase):
    def test_interpolate_gaps_bridges_short_runs_only(self):
        x = np.array([1.0, np.nan, 3.0, np.nan, np.nan, np.nan, 7.0])
        out = interpolate_gaps(x, max_gap=1)
        self.assertAlmostEqual(out[1], 2.0)
        self.assertTrue(np.isnan(out[3:6]).all())

    def test_interpolate_gaps_never_extrapolates(self):
        x = np.array([np.nan, 2.0, 3.0, np.nan])
        out = interpolate_gaps(x)
        self.assertTrue(np.isnan(out[0]) and np.isnan(out[-1]))

    def test_nan_runs(self):
        self.assertEqual(nan_runs(np.array([1.0, np.nan, np.nan, 4.0])), [(1, 3)])

    def test_resample_preserves_duration(self):
        x = np.sin(2 * np.pi * 440 * np.arange(SR) / SR)
        y = resample_poly(x, SR, 48000)
        self.assertAlmostEqual(len(y) / 48000, len(x) / SR, places=3)


class TestPitchTracking(unittest.TestCase):
    def test_tracks_a_steady_note_to_within_a_few_cents(self):
        audio, truth = steady(midi=57.0, duration=1.0)
        pt = track_f0(audio, SR)
        idx = np.clip((pt.times * SR).astype(int), 0, truth.size - 1)
        ref = truth[idx]
        ok = np.isfinite(ref) & np.isfinite(pt.f0)
        self.assertGreater(ok.sum(), 50)
        err = np.abs(1200.0 * np.log2(pt.f0[ok] / ref[ok]))
        self.assertLess(float(np.median(err)), 5.0)
        self.assertLess(float(np.mean(err > 50.0)), 0.02)  # gross/octave errors

    def test_tracks_across_the_singing_range(self):
        # An octave error at one end of the range would be invisible in an
        # average, so every register is checked on its own.
        for midi in (45, 57, 69, 76):
            with self.subTest(midi=midi):
                audio, truth = steady(midi=float(midi), duration=0.7)
                pt = track_f0(audio, SR)
                voiced = pt.f0[np.isfinite(pt.f0)]
                self.assertGreater(voiced.size, 30)
                expected = 440.0 * 2.0 ** ((midi - 69) / 12.0)
                got = float(np.median(voiced))
                self.assertLess(abs(1200.0 * np.log2(got / expected)), 15.0)

    def test_silence_is_reported_unvoiced(self):
        pt = track_f0(np.zeros(SR // 2), SR)
        self.assertLess(pt.voiced_ratio(), 0.05)


class TestNoteSegmentation(unittest.TestCase):
    def test_finds_each_sung_note_once(self):
        midis = [60, 62, 64, 65, 67, 65, 64, 62]
        audio, _ = sung(midis, dur=0.45, detune=[0] * 8, vibrato=30.0)
        notes = segment_notes(track_f0(audio, SR), audio)
        self.assertEqual(len(notes), len(midis))
        self.assertEqual([n.nearest_midi for n in notes.notes], midis)

    def test_does_not_split_a_leap_into_a_phantom_note(self):
        # The glide across a big interval passes through intermediate pitches;
        # they must not be reported as notes that were never sung.
        midis = [60, 72, 60, 71]
        audio, _ = sung(midis, dur=0.4, detune=[0] * 4, vibrato=0.0)
        notes = segment_notes(track_f0(audio, SR), audio)
        self.assertEqual([n.nearest_midi for n in notes.notes], midis)

    def test_repeated_pitch_is_split_on_its_onset(self):
        midis = [64, 64, 64]
        audio, _ = sung(midis, dur=0.4, detune=[0] * 3, vibrato=0.0)
        notes = segment_notes(track_f0(audio, SR), audio)
        self.assertEqual(len(notes), 3)

    def test_vibrato_is_measured_only_when_present(self):
        with_vib, _ = sung([60, 62, 64], dur=0.6, detune=[0] * 3, vibrato=45.0)
        without, _ = sung([60, 62, 64], dur=0.6, detune=[0] * 3, vibrato=0.0)
        a = segment_notes(track_f0(with_vib, SR), with_vib)
        b = segment_notes(track_f0(without, SR), without)
        self.assertGreater(np.mean([n.vibrato_cents for n in a.notes]), 12.0)
        self.assertEqual(max(n.vibrato_cents for n in b.notes), 0.0)
        rates = [n.vibrato_hz for n in a.notes if n.vibrato_hz > 0]
        self.assertTrue(all(3.5 <= r <= 9.0 for r in rates))

    def test_core_lies_inside_the_note(self):
        audio, _ = sung([60, 64, 67], dur=0.5, detune=[0] * 3)
        for n in segment_notes(track_f0(audio, SR), audio).notes:
            self.assertGreaterEqual(n.core_start, n.start)
            self.assertLessEqual(n.core_end, n.end)
            self.assertGreater(n.core_duration, 0.0)


class TestKey(unittest.TestCase):
    def test_detects_major_and_minor(self):
        for midis, expected in [
            ([60, 64, 67, 65, 64, 62, 60, 67, 72, 71, 67, 64, 60], "C major"),
            ([57, 60, 64, 62, 60, 59, 57, 55, 57, 60, 64], "A minor"),
        ]:
            with self.subTest(expected=expected):
                audio, _ = sung(midis, dur=0.33, detune=[0] * len(midis))
                notes = segment_notes(track_f0(audio, SR), audio)
                self.assertEqual(estimate_key(notes).name, expected)

    def test_tuning_follows_a_consistently_offset_session(self):
        midis = [60, 62, 64, 65, 67, 69]
        audio, _ = sung(midis, dur=0.4, detune=[-20.0] * 6, vibrato=0.0)
        notes = segment_notes(track_f0(audio, SR), audio)
        got = estimate_tuning(notes)
        self.assertLess(abs(1200.0 * np.log2(got / 440.0) - (-20.0)), 12.0)

    def test_scattered_tuning_falls_back_to_concert_pitch(self):
        # Offsets spread evenly around the 100-cent circle carry no consistent
        # reference, so inventing one would rename every note in the song.
        # (Note that +45 and -48 would *not* be scattered: on a circle of 100
        # cents they sit almost on top of each other.)
        midis = [60, 62, 64, 65, 67, 69, 71, 72]
        audio, _ = sung(midis, dur=0.4, detune=[-50, -37, -25, -12, 0, 12, 25, 37], vibrato=0.0)
        notes = segment_notes(track_f0(audio, SR), audio)
        self.assertAlmostEqual(estimate_tuning(notes), 440.0, places=6)

    def test_key_names_parse(self):
        self.assertEqual(key_from_name("F# minor").pitch_classes, (1, 2, 4, 6, 8, 9, 11))
        self.assertEqual(key_from_name("C major").pitch_classes, (0, 2, 4, 5, 7, 9, 11))
        self.assertEqual(len(key_from_name("chromatic").pitch_classes), 12)
        with self.assertRaises(ValueError):
            key_from_name("H lydianish")


if __name__ == "__main__":
    unittest.main()
