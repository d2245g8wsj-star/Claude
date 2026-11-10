"""Invoer van de gebruiker herkennen: Spotify-, YouTube-links, bestanden of tekstregels."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from ..audio import SUPPORTED_EXTENSIONS

_SPOTIFY_URL = re.compile(
    r"(?:https?://)?open\.spotify\.com/(?:intl-[a-z]{2}/)?(?:embed/)?"
    r"(track|album|playlist)/([A-Za-z0-9]{22})"
)
_SPOTIFY_URI = re.compile(r"spotify:(track|album|playlist):([A-Za-z0-9]{22})")
_YOUTUBE = re.compile(
    r"(?:https?://)?(?:www\.|m\.|music\.)?(?:youtube\.com|youtu\.be)/\S+", re.IGNORECASE
)


@dataclass
class ParsedInput:
    kind: str  # 'spotify' | 'youtube' | 'file' | 'text'
    value: str
    spotify_type: str = ""
    spotify_id: str = ""


def parse_line(line: str) -> ParsedInput | None:
    s = line.strip().strip('"').strip("'")
    if not s or s.startswith("#"):
        return None
    m = _SPOTIFY_URL.search(s) or _SPOTIFY_URI.search(s)
    if m:
        return ParsedInput("spotify", s, m.group(1), m.group(2))
    if _YOUTUBE.match(s):
        return ParsedInput("youtube", s)
    p = Path(s).expanduser()
    if p.suffix.lower() in SUPPORTED_EXTENSIONS and p.exists():
        return ParsedInput("file", str(p))
    return ParsedInput("text", s)


def parse_input(text: str) -> list[ParsedInput]:
    out = []
    for line in text.splitlines():
        p = parse_line(line)
        if p:
            out.append(p)
    return out


def split_artist_title(text: str) -> tuple[str, str]:
    """'Artiest - Titel' -> ('Artiest', 'Titel'). Zonder scheidingsteken: ('', tekst)."""
    for sep in (" - ", " – ", " — ", " | "):
        if sep in text:
            a, t = text.split(sep, 1)
            return a.strip(), t.strip()
    return "", text.strip()


_JUNK = re.compile(
    r"\s*[\(\[](?:official\s*(?:music\s*)?(?:video|audio|lyric(?:s)?\s*video|visuali[sz]er)?|"
    r"lyrics?|lyric video|audio|hd|hq|4k|video|clip officiel|videoclip|visuali[sz]er|"
    r"m/?v|official)[\)\]]",
    re.IGNORECASE,
)


def clean_video_title(title: str) -> str:
    """Haal '(Official Video)', '[Lyrics]' en dergelijke uit een YouTube-titel."""
    t = _JUNK.sub("", title)
    return re.sub(r"\s{2,}", " ", t).strip(" -")
