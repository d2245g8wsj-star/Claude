"""Tags en albumhoes in een audiobestand schrijven."""

from __future__ import annotations

import io
import urllib.request
from pathlib import Path

from mutagen.id3 import APIC, ID3, ID3NoHeaderError
from mutagen.mp4 import MP4, MP4Cover

from .models import TrackInfo

COVER_SIZE = 300  # ruim voldoende voor het scherm van een nano (320x240)


def fetch_cover(url: str, size: int = COVER_SIZE) -> bytes | None:
    """Download een afbeelding en maak er een vierkante JPEG van."""
    if not url:
        return None
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 ipodsync"})
        with urllib.request.urlopen(req, timeout=20) as r:
            raw = r.read()
        return square_jpeg(raw, size)
    except Exception:
        return None


def square_jpeg(raw: bytes, size: int = COVER_SIZE) -> bytes:
    from PIL import Image

    img = Image.open(io.BytesIO(raw)).convert("RGB")
    w, h = img.size
    side = min(w, h)  # YouTube-thumbnails zijn 16:9: midden uitsnijden
    img = img.crop(((w - side) // 2, (h - side) // 2, (w + side) // 2, (h + side) // 2))
    img = img.resize((size, size), Image.LANCZOS)
    out = io.BytesIO()
    img.save(out, "JPEG", quality=88)
    return out.getvalue()


def write_tags(path: Path, t: TrackInfo, cover: bytes | None = None) -> None:
    ext = path.suffix.lower()
    if ext in (".m4a", ".m4b", ".mp4"):
        _write_mp4(path, t, cover)
    elif ext == ".mp3":
        _write_mp3(path, t, cover)


def _write_mp4(path: Path, t: TrackInfo, cover: bytes | None) -> None:
    f = MP4(path)
    if f.tags is None:
        f.add_tags()
    tags = f.tags
    tags["\xa9nam"] = [t.title]
    if t.artist:
        tags["\xa9ART"] = [t.artist]
    if t.album:
        tags["\xa9alb"] = [t.album]
    if t.albumartist:
        tags["aART"] = [t.albumartist]
    if t.genre:
        tags["\xa9gen"] = [t.genre]
    if t.year:
        tags["\xa9day"] = [str(t.year)]
    if t.track_nr:
        tags["trkn"] = [(t.track_nr, t.tracks_total or 0)]
    if t.cd_nr:
        tags["disk"] = [(t.cd_nr, 0)]
    if cover:
        tags["covr"] = [MP4Cover(cover, imageformat=MP4Cover.FORMAT_JPEG)]
    f.save()


def _write_mp3(path: Path, t: TrackInfo, cover: bytes | None) -> None:
    from mutagen.easyid3 import EasyID3

    try:
        tags = EasyID3(path)
    except ID3NoHeaderError:
        tags = EasyID3()
    tags["title"] = t.title
    if t.artist:
        tags["artist"] = t.artist
    if t.album:
        tags["album"] = t.album
    if t.albumartist:
        tags["albumartist"] = t.albumartist
    if t.genre:
        tags["genre"] = t.genre
    if t.year:
        tags["date"] = str(t.year)
    if t.track_nr:
        tags["tracknumber"] = f"{t.track_nr}/{t.tracks_total}" if t.tracks_total else str(t.track_nr)
    # ID3v2.3: het beste ondersteund door oudere iPods
    tags.save(path, v2_version=3)
    if cover:
        id3 = ID3(path)
        id3.delall("APIC")
        id3.add(APIC(encoding=3, mime="image/jpeg", type=3, desc="Cover", data=cover))
        id3.save(v2_version=3)
