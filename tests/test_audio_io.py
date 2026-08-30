"""WAV reading and writing must be exact enough to be transparent."""

import tempfile
import unittest
from pathlib import Path

import numpy as np

from helpers import SR  # noqa: F401
from vocalai.audio.io import AudioBuffer, WavError, read_wav, write_buffer, write_wav


class TestWavRoundTrip(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        t = np.arange(SR) / SR
        self.mono = 0.5 * np.sin(2 * np.pi * 440.0 * t)

    def test_round_trip_precision_per_depth(self):
        # Each format must survive a round trip to within its own quantisation.
        for bits, is_float, tol in [(16, False, 2e-5), (24, False, 1e-7), (32, False, 1e-9),
                                    (32, True, 1e-7)]:
            with self.subTest(bits=bits, float=is_float):
                p = self.dir / f"t{bits}{is_float}.wav"
                write_wav(p, self.mono, SR, bits=bits, float_format=is_float)
                buf = read_wav(p)
                self.assertEqual(buf.sample_rate, SR)
                self.assertEqual(buf.channels, 1)
                self.assertEqual(buf.frames, SR)
                self.assertLess(float(np.abs(buf.mono() - self.mono).max()), tol)

    def test_stereo_channels_are_not_swapped(self):
        stereo = np.stack([self.mono, -0.25 * self.mono], axis=1)
        p = self.dir / "stereo.wav"
        write_wav(p, stereo, SR, bits=24)
        buf = read_wav(p)
        self.assertEqual(buf.channels, 2)
        self.assertLess(float(np.abs(buf.samples - stereo).max()), 1e-6)
        # The mono sum is the average, not the first channel.
        self.assertLess(float(np.abs(buf.mono() - stereo.mean(axis=1)).max()), 1e-6)

    def test_full_scale_survives_without_wrapping(self):
        loud = np.array([1.0, -1.0, 0.999, -0.999])
        p = self.dir / "loud.wav"
        write_wav(p, loud, SR, bits=16)
        got = read_wav(p).mono()
        self.assertTrue(np.all(np.abs(got) <= 1.0))
        self.assertLess(float(np.abs(got - loud).max()), 1e-4)

    def test_buffer_preserves_source_format(self):
        p = self.dir / "src.wav"
        write_wav(p, self.mono, SR, bits=24)
        buf = read_wav(p)
        out = self.dir / "out.wav"
        write_buffer(out, buf)
        self.assertEqual(read_wav(out).source_bits, 24)

    def test_rejects_non_wave_file(self):
        p = self.dir / "bad.wav"
        p.write_bytes(b"not a wave file at all")
        with self.assertRaises(WavError):
            read_wav(p)

    def test_buffer_orients_channels_first_input(self):
        # (channels, frames) input is transposed to (frames, channels).
        buf = AudioBuffer(np.zeros((2, 100)), SR)
        self.assertEqual((buf.frames, buf.channels), (100, 2))


if __name__ == "__main__":
    unittest.main()
