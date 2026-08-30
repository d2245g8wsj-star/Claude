"""Find the tracks that make up a session and read what their names imply."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

AUDIO_SUFFIXES = {".wav", ".wave"}

# Naming conventions engineers actually use.  These only ever nudge a decision
# the audio has already made - a file called "gtr_vox_bus" is not a vocal
# because of its name, and a mislabelled take is still classified on its sound.
_VOCAL_HINTS = re.compile(
    r"(?:^|[^a-z])(vox|vocal|vocals|voc|lead|ld|sing|verse|chorus|bv|bvs|"
    r"harm|harmony|harmonies|double|dbl|dub|stack|adlib|ad_lib)(?:[^a-z]|$)",
    re.IGNORECASE,
)
_INSTRUMENT_HINTS = re.compile(
    r"(?:^|[^a-z])(drum|drums|kick|snare|hat|hats|tom|perc|bass|808|gtr|guitar|"
    r"keys|piano|pno|synth|pad|strings|horn|brass|fx|room|oh|overhead|mix|inst)"
    r"(?:[^a-z]|$)",
    re.IGNORECASE,
)
_LEAD_HINTS = re.compile(r"(?:^|[^a-z])(lead|ld|main|vox1|vox_1|v1)(?:[^a-z]|$)", re.IGNORECASE)
_SUPPORT_HINTS = re.compile(
    r"(?:^|[^a-z])(bv|bvs|harm|harmony|harmonies|double|dbl|dub|stack|adlib|ad_lib)"
    r"(?:[^a-z]|$)",
    re.IGNORECASE,
)


@dataclass
class TrackFile:
    """One audio file in the session, plus whatever its name suggests."""

    path: Path
    name: str
    name_vocal_prior: float = 0.0     # -1 instrument .. +1 vocal
    name_role: str | None = None      # "lead" | "support" | None

    @property
    def stem(self) -> str:
        return self.path.stem


def name_prior(name: str) -> tuple[float, str | None]:
    """Read a filename for hints about what the track is."""
    vocal = bool(_VOCAL_HINTS.search(name))
    inst = bool(_INSTRUMENT_HINTS.search(name))
    prior = 0.0
    if vocal and not inst:
        prior = 1.0
    elif inst and not vocal:
        prior = -1.0
    role = None
    if _LEAD_HINTS.search(name):
        role = "lead"
    elif _SUPPORT_HINTS.search(name):
        role = "support"
    return prior, role


def scan_session(target: str | Path, recursive: bool = True) -> list[TrackFile]:
    """Collect the audio files of a session folder (or a single file)."""
    target = Path(target)
    if target.is_file():
        paths = [target]
    elif target.is_dir():
        globber = target.rglob if recursive else target.glob
        paths = sorted(p for p in globber("*") if p.suffix.lower() in AUDIO_SUFFIXES)
    else:
        raise FileNotFoundError(f"no such session path: {target}")

    tracks = []
    for p in paths:
        if p.name.startswith("."):
            continue
        prior, role = name_prior(p.stem)
        tracks.append(TrackFile(path=p, name=p.stem, name_vocal_prior=prior, name_role=role))
    return tracks
