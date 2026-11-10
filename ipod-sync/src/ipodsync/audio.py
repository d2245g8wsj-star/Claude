"""Audiobestanden inspecteren en (indien nodig) omzetten naar een iPod-formaat."""

from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

import mutagen
from mutagen.mp3 import BitrateMode

# Formaten die een iPod nano 3G zelf kan afspelen.
NATIVE_EXTENSIONS = {".mp3", ".m4a", ".m4b", ".wav", ".aif", ".aiff"}
# Formaten die we eerst met ffmpeg omzetten naar AAC.
CONVERT_EXTENSIONS = {".flac", ".ogg", ".oga", ".opus", ".webm", ".wma", ".aac", ".mka", ".mp4", ".alac"}
SUPPORTED_EXTENSIONS = NATIVE_EXTENSIONS | CONVERT_EXTENSIONS

FILETYPE_NAMES = {
    ".mp3": "MPEG audio file",
    ".m4a": "AAC audio file",
    ".m4b": "AAC audio book file",
    ".wav": "WAV audio file",
    ".aif": "AIFF audio file",
    ".aiff": "AIFF audio file",
}


@dataclass
class AudioInfo:
    path: Path
    length_ms: int = 0
    bitrate_kbps: int = 0
    samplerate: int = 44100
    vbr: bool = False
    codec: str = ""
    tags: dict[str, str] = field(default_factory=dict)

    @property
    def is_mp3(self) -> bool:
        return self.path.suffix.lower() == ".mp3"

    @property
    def filetype_name(self) -> str:
        if self.codec == "alac":
            return "Apple Lossless audio file"
        return FILETYPE_NAMES.get(self.path.suffix.lower(), "Audio file")


def _first(tags, *keys) -> str:
    for k in keys:
        v = tags.get(k) if tags else None
        if v:
            if isinstance(v, list):
                v = v[0]
            return str(v).strip()
    return ""


def _num(s: str) -> tuple[int, int]:
    """'3/12' -> (3, 12)."""
    if not s:
        return 0, 0
    parts = s.split("/")
    try:
        a = int(parts[0]) if parts[0].strip() else 0
    except ValueError:
        a = 0
    try:
        b = int(parts[1]) if len(parts) > 1 and parts[1].strip() else 0
    except ValueError:
        b = 0
    return a, b


def probe(path: str | Path) -> AudioInfo:
    path = Path(path)
    f = mutagen.File(path, easy=True)
    if f is None:
        raise ValueError(f"Onbekend audioformaat: {path.name}")
    info = AudioInfo(path)
    i = f.info
    info.length_ms = int(round(getattr(i, "length", 0) * 1000))
    info.bitrate_kbps = int(round((getattr(i, "bitrate", 0) or 0) / 1000))
    info.samplerate = int(getattr(i, "sample_rate", 44100) or 44100)
    info.vbr = getattr(i, "bitrate_mode", None) == BitrateMode.VBR
    info.codec = str(getattr(i, "codec", "") or "")
    if not info.bitrate_kbps and info.length_ms:
        info.bitrate_kbps = int(path.stat().st_size * 8 / info.length_ms)
    tags = f.tags or {}
    track, tracks = _num(_first(tags, "tracknumber"))
    disc, discs = _num(_first(tags, "discnumber"))
    year = _first(tags, "date", "year")[:4]
    info.tags = {
        "title": _first(tags, "title") or path.stem,
        "artist": _first(tags, "artist"),
        "album": _first(tags, "album"),
        "albumartist": _first(tags, "albumartist"),
        "genre": _first(tags, "genre"),
        "composer": _first(tags, "composer"),
        "year": year if year.isdigit() else "",
        "track_nr": str(track or ""),
        "tracks_total": str(tracks or ""),
        "cd_nr": str(disc or ""),
        "cds": str(discs or ""),
    }
    return info


def ffmpeg_path() -> str | None:
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    for candidate in ("/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"):
        if Path(candidate).exists():
            return candidate
    try:  # optionele fallback
        import imageio_ffmpeg

        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return None


def needs_conversion(path: str | Path) -> bool:
    p = Path(path)
    ext = p.suffix.lower()
    if ext in CONVERT_EXTENSIONS:
        return True
    if ext in (".m4a", ".m4b"):
        # m4a kan ook Opus/FLAC bevatten; de nano speelt alleen AAC en ALAC
        try:
            codec = str(getattr(mutagen.File(p).info, "codec", "") or "")
        except Exception:
            return True
        return not (codec.startswith("mp4a") or codec == "alac")
    return ext not in NATIVE_EXTENSIONS


def convert_to_aac(src: str | Path, dest_dir: str | Path, bitrate: str = "256k") -> Path:
    """Zet een bestand om naar AAC (.m4a), met behoud van tags. Geeft het nieuwe pad terug."""
    exe = ffmpeg_path()
    if not exe:
        raise RuntimeError("ffmpeg niet gevonden. Installeer het met: brew install ffmpeg")
    src = Path(src)
    dest = Path(dest_dir) / (src.stem + ".m4a")
    n = 1
    while dest.exists():
        dest = Path(dest_dir) / f"{src.stem} ({n}).m4a"
        n += 1
    cmd = [
        exe, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(src),
        "-map", "0:a:0", "-map_metadata", "0",
        "-c:a", "aac", "-b:a", bitrate, "-ar", "44100",
        "-movflags", "+faststart",
        str(dest),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Omzetten mislukt voor {src.name}: {res.stderr.strip()[-400:]}")
    return dest
