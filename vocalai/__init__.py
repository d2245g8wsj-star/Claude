"""VocalAI - automatic, musically-aware vocal tuning and alignment.

A note-based pitch editor (in the spirit of Melodyne) and a guide-based time
aligner (in the spirit of VocALign), driven by an engine that decides for
itself which tracks in a session are vocals, which one is the lead, and what
each note was meant to be.

Typical use::

    from vocalai import process_session
    result = process_session("my_session/", "my_session/tuned/")
    print(result.summary())
"""

from .analysis.key import KeyEstimate, estimate_key, key_from_name
from .model import Note, NoteSequence
from .pipeline import TrackAnalysis, analyze_track, tune_track
from .process.align import AlignConfig, align_to_guide
from .process.tuner import TuneConfig
from .session.engine import SessionConfig, SessionResult, process_session
from .session.report import to_dict, to_html, write_reports

__version__ = "1.0.0"

__all__ = [
    "AlignConfig",
    "KeyEstimate",
    "Note",
    "NoteSequence",
    "SessionConfig",
    "SessionResult",
    "TrackAnalysis",
    "TuneConfig",
    "align_to_guide",
    "analyze_track",
    "estimate_key",
    "key_from_name",
    "process_session",
    "to_dict",
    "to_html",
    "tune_track",
    "write_reports",
]
