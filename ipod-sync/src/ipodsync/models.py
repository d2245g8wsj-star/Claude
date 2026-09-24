"""Gedeelde datatypes."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class TrackInfo:
    """Een nummer dat we op de iPod willen zetten, met de metadata die we kennen."""

    title: str
    artists: list[str] = field(default_factory=list)
    album: str = ""
    albumartist: str = ""
    track_nr: int = 0
    tracks_total: int = 0
    cd_nr: int = 0
    year: int = 0
    genre: str = ""
    duration_ms: int = 0
    cover_url: str = ""
    source: str = "text"  # 'spotify' | 'youtube' | 'text' | 'file'
    source_url: str = ""
    youtube_url: str = ""  # gekozen audiobron
    youtube_title: str = ""
    local_path: Path | None = None

    @property
    def artist(self) -> str:
        return ", ".join(self.artists)

    @property
    def display(self) -> str:
        return f"{self.artist} - {self.title}" if self.artists else self.title

    def tag_overrides(self) -> dict[str, str]:
        """Velden voor IPodLibrary.add_file(); lege waarden laten de tags uit het bestand staan."""
        out = {
            "title": self.title,
            "artist": self.artist,
            "album": self.album,
            "albumartist": self.albumartist,
            "genre": self.genre,
            "year": str(self.year or ""),
            "track_nr": str(self.track_nr or ""),
            "tracks_total": str(self.tracks_total or ""),
            "cd_nr": str(self.cd_nr or ""),
        }
        return {k: v for k, v in out.items() if v}
