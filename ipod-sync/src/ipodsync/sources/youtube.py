"""Zoeken en downloaden van audio via yt-dlp."""

from __future__ import annotations

from pathlib import Path

from ..audio import convert_to_aac, ffmpeg_path, needs_conversion
from ..models import TrackInfo
from .matcher import Candidate
from .parse import clean_video_title, split_artist_title


class YouTubeError(Exception):
    pass


def _ydl(opts: dict):
    import yt_dlp

    base = {"quiet": True, "no_warnings": True, "noprogress": True}
    ff = ffmpeg_path()
    if ff:
        base["ffmpeg_location"] = ff
    base.update(opts)
    return yt_dlp.YoutubeDL(base)


def search(query: str, n: int = 6) -> list[Candidate]:
    with _ydl({"extract_flat": "in_playlist", "skip_download": True}) as ydl:
        try:
            res = ydl.extract_info(f"ytsearch{n}:{query}", download=False)
        except Exception as e:
            raise YouTubeError(f"Zoeken op YouTube mislukt: {e}") from e
    out = []
    for e in (res or {}).get("entries") or []:
        if not e:
            continue
        vid = e.get("id")
        out.append(
            Candidate(
                url=e.get("url") or f"https://www.youtube.com/watch?v={vid}",
                title=e.get("title") or "",
                channel=e.get("channel") or e.get("uploader") or "",
                duration_s=float(e.get("duration") or 0),
            )
        )
    return out


def info_to_track(info: dict) -> TrackInfo:
    """Metadata van een YouTube-video omzetten; YouTube Music levert vaak track/artist/album."""
    title = info.get("track") or ""
    artists: list[str] = []
    if info.get("artists"):
        artists = [a for a in info["artists"] if a]
    elif info.get("artist"):
        artists = [a.strip() for a in str(info["artist"]).split(",") if a.strip()]
    if not title:
        artist_guess, title = split_artist_title(clean_video_title(info.get("title") or ""))
        if not artists:
            if artist_guess:
                artists = [artist_guess]
            else:
                channel = (info.get("channel") or info.get("uploader") or "").removesuffix(" - Topic")
                artists = [channel] if channel else []
    year = info.get("release_year") or 0  # uploaddatum is geen releasejaar, dus niet gebruiken
    url = info.get("webpage_url") or info.get("url") or ""
    return TrackInfo(
        title=title,
        artists=artists,
        album=info.get("album") or "",
        year=int(year or 0),
        duration_ms=int((info.get("duration") or 0) * 1000),
        cover_url=info.get("thumbnail") or "",
        source="youtube",
        source_url=url,
        youtube_url=url,
        youtube_title=info.get("title") or "",
    )


def expand(url: str) -> tuple[str, list[TrackInfo]]:
    """Een video- of playlistlink omzetten naar een lijst nummers."""
    with _ydl({"extract_flat": "in_playlist", "skip_download": True}) as ydl:
        try:
            info = ydl.extract_info(url, download=False)
        except Exception as e:
            raise YouTubeError(f"Kan deze YouTube-link niet lezen: {e}") from e
    if info.get("_type") == "playlist" or info.get("entries"):
        tracks = []
        for e in info.get("entries") or []:
            if not e:
                continue
            vid_url = e.get("url") or f"https://www.youtube.com/watch?v={e.get('id')}"
            t = info_to_track({**e, "webpage_url": vid_url})
            tracks.append(t)
        return info.get("title") or "YouTube-playlist", tracks
    t = info_to_track(info)
    return t.title, [t]


def download(url: str, dest_dir: Path, aac_bitrate: str = "256k") -> tuple[Path, dict]:
    """Download de audio. Geeft (pad naar iPod-geschikt bestand, yt-dlp info) terug."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    opts = {
        # AAC (m4a) direct van YouTube: speelt native op de iPod, geen kwaliteitsverlies
        "format": "bestaudio[ext=m4a]/bestaudio[acodec^=mp4a]/bestaudio/best",
        "outtmpl": str(dest_dir / "%(id)s.%(ext)s"),
        "noplaylist": True,
        "overwrites": True,
    }
    with _ydl(opts) as ydl:
        try:
            info = ydl.extract_info(url, download=True)
        except Exception as e:
            raise YouTubeError(f"Downloaden mislukt: {e}") from e
        path = Path(ydl.prepare_filename(info))
    if not path.exists():
        matches = list(dest_dir.glob(f"{info.get('id')}.*"))
        if not matches:
            raise YouTubeError("Download leek te lukken, maar het bestand is niet gevonden")
        path = matches[0]
    if needs_conversion(path):
        converted = convert_to_aac(path, dest_dir, aac_bitrate)
        path.unlink(missing_ok=True)
        path = converted
    return path, info
