"""De beste YouTube-video kiezen voor een nummer (bijvoorbeeld uit een Spotify-playlist)."""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass

from ..models import TrackInfo

# Woorden die op een andere versie wijzen dan de studio-opname.
PENALTY_WORDS = {
    "live": 35, "cover": 45, "remix": 35, "karaoke": 60, "instrumental": 45,
    "sped up": 50, "speed up": 50, "slowed": 50, "reverb": 35, "8d": 50,
    "nightcore": 60, "acoustic": 25, "reaction": 70, "tutorial": 70,
    "extended": 20, "mashup": 45, "edit": 10, "piano version": 40, "1 hour": 70,
}
MIN_SCORE = 35


@dataclass
class Candidate:
    url: str
    title: str
    channel: str = ""
    duration_s: float = 0.0
    score: float = 0.0


def _norm(s: str) -> str:
    s = unicodedata.normalize("NFKD", s)
    s = "".join(c for c in s if not unicodedata.combining(c)).lower()
    return re.sub(r"[^\w\s]", " ", s)


def _words(s: str) -> set[str]:
    return {w for w in _norm(s).split() if w not in {"the", "a", "de", "het", "feat", "ft", "and", "en"}}


def score(c: Candidate, target: TrackInfo) -> float:
    s = 0.0
    ctitle, cchan = _norm(c.title), _norm(c.channel)
    tt = _norm(target.title)

    # duur is het sterkste signaal
    if target.duration_ms and c.duration_s:
        diff = abs(c.duration_s - target.duration_ms / 1000)
        if diff <= 3:
            s += 40
        elif diff <= 7:
            s += 25
        elif diff <= 15:
            s += 5
        elif diff <= 30:
            s -= 25
        else:
            s -= 60

    # titelwoorden terug in de videotitel
    tw = _words(target.title)
    if tw:
        s += 30 * len(tw & _words(c.title)) / len(tw)

    # artiest in kanaal of titel
    for artist in target.artists[:2]:
        aw = _words(artist)
        if aw and (aw <= _words(c.channel) or aw <= _words(c.title)):
            s += 15
            break

    if c.channel.endswith(" - Topic"):
        s += 20  # automatisch gegenereerde YouTube Music-upload = studioversie
    if "official audio" in ctitle:
        s += 10
    elif "official video" in ctitle or "official music video" in ctitle:
        s += 3
    if cchan.endswith("vevo"):
        s += 5

    for word, penalty in PENALTY_WORDS.items():
        if re.search(rf"\b{re.escape(word)}\b", ctitle) and not re.search(rf"\b{re.escape(word)}\b", tt):
            s -= penalty
    return s


def rank(candidates: list[Candidate], target: TrackInfo) -> list[Candidate]:
    for c in candidates:
        c.score = score(c, target)
    return sorted(candidates, key=lambda c: c.score, reverse=True)


def best_match(candidates: list[Candidate], target: TrackInfo) -> Candidate | None:
    ranked = rank(candidates, target)
    if ranked and ranked[0].score >= MIN_SCORE:
        return ranked[0]
    return None


def search_query(target: TrackInfo) -> str:
    artist = target.artists[0] if target.artists else ""
    return f"{artist} - {target.title}".strip(" -") + " audio"
