"""Van invoer naar een bestand dat klaar is voor de iPod."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from ..audio import convert_to_aac, needs_conversion, probe
from ..config import Config
from ..models import TrackInfo
from ..tagger import fetch_cover, write_tags
from . import matcher, spotify, youtube
from .parse import ParsedInput, split_artist_title

# statussen in de wachtrij
PENDING, MATCHED, NO_MATCH, READY, ADDED, FAILED, SKIPPED = (
    "zoeken…", "gevonden", "geen match", "klaar", "toegevoegd", "mislukt", "overgeslagen",
)


@dataclass(eq=False)
class QueueItem:
    track: TrackInfo
    status: str = PENDING
    error: str = ""
    candidates: list[matcher.Candidate] = field(default_factory=list)
    prepared_path: Path | None = None
    use_file_tags: bool = False  # eigen bestanden: tags uit het bestand zelf gebruiken

    @property
    def needs_match(self) -> bool:
        return self.track.source in ("spotify", "text") and not self.track.youtube_url


def resolve(inputs: list[ParsedInput], cfg: Config) -> list[QueueItem]:
    """Zet invoerregels om naar wachtrij-items (haalt playlistinhoud op)."""
    items: list[QueueItem] = []
    for inp in inputs:
        if inp.kind == "spotify":
            _, tracks = spotify.fetch(
                inp.spotify_type, inp.spotify_id, cfg.spotify_client_id, cfg.spotify_client_secret
            )
            items += [QueueItem(t) for t in tracks]
        elif inp.kind == "youtube":
            _, tracks = youtube.expand(inp.value)
            items += [QueueItem(t, status=MATCHED) for t in tracks]
        elif inp.kind == "file":
            items.append(file_item(Path(inp.value)))
        else:
            artist, title = split_artist_title(inp.value)
            items.append(QueueItem(TrackInfo(title=title, artists=[artist] if artist else [], source="text")))
    return items


def file_item(path: Path) -> QueueItem:
    info = probe(path)
    t = TrackInfo(
        title=info.tags.get("title") or path.stem,
        artists=[info.tags["artist"]] if info.tags.get("artist") else [],
        album=info.tags.get("album", ""),
        duration_ms=info.length_ms,
        source="file",
        local_path=path,
    )
    return QueueItem(t, status=READY, use_file_tags=True)


def find_match(item: QueueItem) -> None:
    """Zoek de beste YouTube-video voor een Spotify- of tekstregel."""
    if not item.needs_match:
        return
    item.candidates = matcher.rank(youtube.search(matcher.search_query(item.track)), item.track)
    best = item.candidates[0] if item.candidates and item.candidates[0].score >= matcher.MIN_SCORE else None
    if item.track.source == "text" and item.candidates and not best:
        best = item.candidates[0]  # geen duur bekend: neem het beste resultaat
    if best:
        choose_candidate(item, best)
    else:
        item.status = NO_MATCH


def choose_candidate(item: QueueItem, c: matcher.Candidate) -> None:
    item.track.youtube_url = c.url
    item.track.youtube_title = c.title
    item.status = MATCHED
    item.prepared_path = None


def prepare(item: QueueItem, cfg: Config, workdir: Path) -> Path:
    """Download/converteer zodat er een iPod-geschikt bestand klaarstaat."""
    t = item.track
    workdir.mkdir(parents=True, exist_ok=True)
    if t.source == "file" and t.local_path:
        path = t.local_path
        if needs_conversion(path):
            path = convert_to_aac(path, workdir, cfg.aac_bitrate)
    elif t.youtube_url:
        path, info = youtube.download(t.youtube_url, workdir, cfg.aac_bitrate)
        if t.source == "text" and not t.album and info.get("album"):
            t.album = info["album"]
        cover = fetch_cover(t.cover_url or info.get("thumbnail") or "")
        write_tags(path, t, cover)
    else:
        raise RuntimeError("Geen audiobron gevonden voor dit nummer")
    item.prepared_path = path
    item.status = READY
    return path
