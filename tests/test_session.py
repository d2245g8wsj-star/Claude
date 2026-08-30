"""Track classification, session orchestration, the note model and the CLI."""

import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from helpers import SR
from vocalai.analysis.voice import group_vocals, profile_track
from vocalai.audio.io import read_wav, write_wav
from vocalai.model import Note, NoteSequence
from vocalai.pipeline import analyze_track
from vocalai.session.engine import SessionConfig, process_session
from vocalai.session.report import to_dict, to_html, write_reports
from vocalai.session.scan import name_prior, scan_session
from vocalai.testing.synth import (
    melody,
    render_bass,
    render_drums,
    render_plucked,
    render_voice,
)

MELODY = [69, 71, 72, 74, 72, 71, 69]


class TestVocalDetection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tracks = {
            "vocal": render_voice(melody(MELODY, dur=0.45, vibrato=35.0, seed=1))[0],
            "guitar": render_plucked(MELODY, dur=0.45, seed=1),
            "bass": render_bass([m - 24 for m in MELODY], dur=0.45),
            "drums": render_drums(3),
        }
        cls.profiles = {}
        for name, audio in cls.tracks.items():
            an = analyze_track(audio, SR, name=name)
            cls.profiles[name] = profile_track(name, audio, SR, an.pitch, an.spec, an.notes)

    def test_vocal_is_recognised(self):
        self.assertTrue(self.profiles["vocal"].is_vocal)

    def test_instruments_are_not_mistaken_for_vocals(self):
        for name in ("guitar", "bass", "drums"):
            with self.subTest(track=name):
                self.assertFalse(self.profiles[name].is_vocal)

    def test_the_margin_is_not_marginal(self):
        # A threshold that only just separates them would not survive real
        # material, so the gap itself is part of what is being tested.
        worst_vocal = self.profiles["vocal"].score
        best_other = max(self.profiles[n].score for n in ("guitar", "bass", "drums"))
        self.assertGreater(worst_vocal - best_other, 0.25)

    def test_evidence_is_reported(self):
        self.assertIn("expression", self.profiles["vocal"].evidence)
        self.assertIn("vocal", self.profiles["vocal"].explain())


class TestGrouping(unittest.TestCase):
    def test_named_lead_outranks_the_audio_guess(self):
        profiles = {}
        seqs = {}
        envs = {}
        for name, midis, seed in [
            ("quiet_lead", MELODY, 1),
            ("loud_double", MELODY, 5),
            ("third_up", [m + 3 for m in MELODY], 9),
        ]:
            audio, _ = render_voice(melody(midis, dur=0.45, vibrato=30.0, seed=seed))[0], None
            an = analyze_track(audio, SR, name=name)
            p = profile_track(name, audio, SR, an.pitch, an.spec, an.notes)
            p.is_vocal = True
            profiles[name] = p
            seqs[name] = an.notes
            envs[name] = np.sqrt(np.sum(an.spec.mag**2, axis=1))
        group_vocals(profiles, envs, seqs, name_roles={"quiet_lead": "lead"})
        self.assertEqual(profiles["quiet_lead"].role, "lead")
        self.assertEqual(profiles["third_up"].role, "harmony")
        self.assertEqual(profiles["loud_double"].role, "double")


class TestNoteModel(unittest.TestCase):
    def test_json_round_trip_keeps_edits(self):
        seq = NoteSequence(
            notes=[Note(index=0, start=0.0, end=0.5, core_start=0.1, core_end=0.4,
                        center_hz=262.0, target_midi=60.0, locked=True,
                        pitch_offset_cents=-12.5)],
            key="C major",
        )
        back = NoteSequence.from_json(seq.to_json())
        self.assertEqual(len(back), 1)
        self.assertTrue(back.notes[0].locked)
        self.assertAlmostEqual(back.notes[0].pitch_offset_cents, -12.5)
        self.assertEqual(back.key, "C major")

    def test_target_midi_overrides_a_stale_cached_hz(self):
        # Editing the musical value by hand must win over the cached rendering.
        n = Note(index=0, start=0.0, end=1.0, core_start=0.1, core_end=0.9,
                 center_hz=262.0, target_midi=72.0, target_hz=262.0)
        self.assertAlmostEqual(n.target_frequency(440.0), 523.25, places=1)

    def test_note_names_and_offsets(self):
        n = Note(index=0, start=0.0, end=1.0, core_start=0.0, core_end=1.0, center_hz=440.0)
        self.assertEqual(n.name, "A4")
        self.assertAlmostEqual(n.cents_off, 0.0, places=3)


class TestScan(unittest.TestCase):
    def test_name_hints(self):
        self.assertEqual(name_prior("01_Lead_Vox"), (1.0, "lead"))
        self.assertEqual(name_prior("BV_stack_3rd"), (1.0, "support"))
        self.assertEqual(name_prior("kick_in")[0], -1.0)

    def test_scan_finds_wavs(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            write_wav(root / "a.wav", np.zeros(100), SR)
            (root / "notes.txt").write_text("ignore me")
            found = scan_session(root)
            self.assertEqual([t.name for t in found], ["a"])


class TestSessionEndToEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name) / "session"
        cls.root.mkdir(parents=True)
        rng = np.random.default_rng(11)
        n = len(MELODY)
        lead = render_voice(
            melody(MELODY, dur=0.45, detune=list(rng.normal(0, 20, n)), vibrato=35.0, seed=1)
        )[0]
        dbl = render_voice(
            melody(MELODY, dur=0.45, detune=list(rng.normal(0, 24, n)), vibrato=30.0, seed=5)
        )[0]
        write_wav(cls.root / "01_Lead_Vox.wav", lead, SR, bits=24)
        write_wav(cls.root / "02_Vox_Double.wav", dbl, SR, bits=24)
        write_wav(cls.root / "03_Gtr.wav", render_plucked(MELODY, dur=0.45, seed=2), SR, bits=24)
        write_wav(cls.root / "04_Drums.wav", render_drums(3), SR, bits=24)

        cls.out = Path(cls.tmp.name) / "out"
        cls.result = process_session(cls.root, cls.out, SessionConfig.from_style("natural"))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_only_vocals_are_processed(self):
        processed = {t.name for t in self.result.tracks if t.processed}
        self.assertEqual(processed, {"01_Lead_Vox", "02_Vox_Double"})

    def test_lead_is_identified_and_not_warped_to_a_double(self):
        self.assertEqual(self.result.lead, "01_Lead_Vox")
        lead = next(t for t in self.result.tracks if t.name == "01_Lead_Vox")
        self.assertIsNone(lead.align_result)

    def test_key_and_concert_pitch_are_detected(self):
        self.assertEqual(self.result.key.name, "A minor")
        self.assertLess(abs(self.result.key.tuning_hz - 440.0), 6.0)

    def test_tuning_improves_every_vocal(self):
        for tr in self.result.tracks:
            if not tr.processed:
                continue
            with self.subTest(track=tr.name):
                s = tr.tune_result.stats
                self.assertLess(s["mean_error_after_cents"], s["mean_error_before_cents"] * 0.5)
                self.assertLess(s["mean_error_after_cents"], 6.0)

    def test_output_audio_is_written_and_readable(self):
        for tr in self.result.tracks:
            if not tr.processed:
                continue
            with self.subTest(track=tr.name):
                self.assertTrue(tr.output_path.exists())
                buf = read_wav(tr.output_path)
                self.assertEqual(buf.sample_rate, SR)
                self.assertGreater(buf.frames, SR // 2)
                peak = float(np.abs(buf.samples).max())
                self.assertGreater(peak, 0.05)
                self.assertLessEqual(peak, 1.0)
                self.assertTrue(np.isfinite(buf.samples).all())

    def test_reports_are_written(self):
        paths = write_reports(self.result, self.out)
        self.assertTrue(paths["json"].exists() and paths["html"].exists())
        data = json.loads(paths["json"].read_text())
        self.assertEqual(data["lead"], "01_Lead_Vox")
        self.assertEqual(data["key"]["name"], "A minor")
        page = paths["html"].read_text()
        self.assertIn("<svg", page)
        self.assertIn("01_Lead_Vox", page)

    def test_report_dict_is_json_serialisable(self):
        json.dumps(to_dict(self.result))
        self.assertIn("<!doctype html>", to_html(self.result))


class TestCli(unittest.TestCase):
    def test_end_to_end_through_the_command_line(self):
        import contextlib
        import io

        from vocalai.cli import main

        run = lambda argv: main(argv)  # noqa: E731

        with contextlib.redirect_stdout(io.StringIO()), tempfile.TemporaryDirectory() as d:
            root = Path(d)
            sess = root / "sess"
            sess.mkdir()
            audio = render_voice(
                melody(MELODY, dur=0.45, detune=[22, -20, 18, -24, 20, -18, 25], seed=1)
            )[0]
            write_wav(sess / "Lead_Vox.wav", audio, SR, bits=24)

            self.assertEqual(main(["tune", str(sess), "-o", str(root / "out"), "-q"]), 0)
            self.assertTrue((root / "out" / "Lead_Vox_tuned.wav").exists())
            self.assertTrue((root / "out" / "vocalai_report.html").exists())

            notes = root / "notes.json"
            self.assertEqual(
                main(["notes", str(sess / "Lead_Vox.wav"), "-o", str(notes)]), 0
            )
            data = json.loads(notes.read_text())
            self.assertGreater(len(data["notes"]), 3)

            # Hand-edit a target, re-render, and confirm the edit took effect.
            data["notes"][1]["target_midi"] = data["notes"][1]["target_midi"] + 2
            data["notes"][1]["strength"] = 1.0
            notes.write_text(json.dumps(data))
            out = root / "edited.wav"
            self.assertEqual(
                main(["render", str(sess / "Lead_Vox.wav"), "--notes", str(notes),
                      "-o", str(out)]), 0
            )
            edited = analyze_track(read_wav(out), name="e")
            self.assertAlmostEqual(edited.notes.notes[1].midi, data["notes"][1]["target_midi"],
                                   delta=0.35)

    def test_missing_path_reports_an_error(self):
        import contextlib
        import io

        from vocalai.cli import main

        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(["tune", "/nonexistent/session/path"]), 2)


if __name__ == "__main__":
    unittest.main()
