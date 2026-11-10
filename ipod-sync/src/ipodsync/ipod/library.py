"""Hoog niveau: muziek op de iPod bekijken, toevoegen, wijzigen en verwijderen.

Wijzigingen worden eerst in het geheugen gedaan en pas bij :meth:`IPodLibrary.save`
naar de iPod geschreven. Voor elke save wordt een back-up van de iTunesDB gemaakt,
en het nieuwe bestand wordt eerst gecontroleerd voordat het het oude vervangt.
"""

from __future__ import annotations

import os
import random
import shutil
import string
import time
from dataclasses import dataclass
from pathlib import Path

from ..audio import AudioInfo, probe
from . import hash58
from .device import IPodDevice
from .itunesdb import (
    ITunesDB,
    ITunesDBError,
    Playlist,
    PlaylistItem,
    Track,
    O_BOOKMARK,
    O_LAST_PLAYED,
    O_PLAYCOUNT,
    O_RATING,
    put_u32,
    u32,
)

EDITABLE_FIELDS = (
    "title", "artist", "album", "albumartist", "genre", "composer",
    "year", "track_nr", "tracks_total", "cd_nr", "cds",
)
_INT_FIELDS = {"year", "track_nr", "tracks_total", "cd_nr", "cds"}
MAX_BACKUPS = 10


class LibraryError(Exception):
    pass


@dataclass
class _PendingCopy:
    src: Path
    dest: Path


class IPodLibrary:
    def __init__(self, device: IPodDevice, *, firewire_guid: str | None = None, backup_root: Path | None = None):
        self.device = device
        self._guid_override = firewire_guid
        self.backup_root = backup_root
        self.db: ITunesDB | None = None
        self._original: bytes = b""
        self._pending_copies: list[_PendingCopy] = []
        self._pending_deletes: list[Path] = []
        self.dirty = False
        self.playcounts_merged = False

    # ------------------------------------------------------------------ laden

    def load(self) -> None:
        path = self.device.itunesdb_path
        if not path.exists():
            raise LibraryError(
                "Geen iTunesDB gevonden op de iPod. Zet de iPod eerst één keer terug of "
                "synchroniseer hem één keer met Finder/iTunes, zodat er een database is."
            )
        data = path.read_bytes()
        try:
            self.db = ITunesDB.parse(data)
        except ITunesDBError as e:
            raise LibraryError(f"Kan de iTunesDB niet lezen: {e}") from e
        self._original = data
        self._merge_playcounts()
        self._pending_copies.clear()
        self._pending_deletes.clear()
        self.dirty = False

    def _merge_playcounts(self) -> None:
        """Verwerk het bestand 'Play Counts' dat de iPod na het afspelen bijhoudt."""
        assert self.db
        pc = self.device.control_dir / "iTunes" / "Play Counts"
        self.playcounts_merged = False
        try:
            data = pc.read_bytes()
        except OSError:
            return
        if data[:4] != b"mhdp" or len(data) < 16:
            return
        hlen, elen, n = u32(data, 4), u32(data, 8), u32(data, 12)
        if elen < 12:
            return
        for i, t in enumerate(self.db.tracks[:n]):
            off = hlen + i * elen
            if off + elen > len(data):
                break
            plays, last, bookmark = u32(data, off), u32(data, off + 4), u32(data, off + 8)
            if plays:
                put_u32(t.header, O_PLAYCOUNT, u32(t.header, O_PLAYCOUNT) + plays)
            if last:
                put_u32(t.header, O_LAST_PLAYED, last)
            if bookmark:
                put_u32(t.header, O_BOOKMARK, bookmark)
            if elen >= 16:
                rating = u32(data, off + 12)
                if rating <= 100 and rating != t.header[O_RATING]:
                    t.header[O_RATING] = rating
        self.playcounts_merged = True

    # ------------------------------------------------------------------ lezen

    @property
    def tracks(self) -> list[Track]:
        assert self.db
        return list(self.db.tracks)

    @property
    def playlists(self) -> list[Playlist]:
        """Gewone playlists (zonder de master-playlist)."""
        assert self.db
        return [p for p in self.db.playlists if not p.is_master]

    def playlist_tracks(self, pl: Playlist) -> list[Track]:
        assert self.db
        by_id = {t.id: t for t in self.db.tracks}
        return [by_id[i] for i in pl.track_ids if i in by_id]

    def find_duplicate(self, title: str, artist: str, length_ms: int = 0) -> Track | None:
        """Een nummer met dezelfde titel en artiest (en ongeveer dezelfde duur)."""
        key = (title or "").strip().casefold(), (artist or "").strip().casefold()
        if not key[0]:
            return None
        for t in self.tracks:
            if ((t.title or "").strip().casefold(), (t.artist or "").strip().casefold()) == key:
                if not length_ms or not t.length_ms or abs(t.length_ms - length_ms) < 5000:
                    return t
        return None

    def track_path(self, t: Track) -> Path | None:
        rel = t.fs_path
        return self.device.mountpoint / rel if rel else None

    # ------------------------------------------------------------------ wijzigen

    def update_track(self, t: Track, **fields) -> None:
        for key, value in fields.items():
            if key not in EDITABLE_FIELDS:
                raise ValueError(f"Onbekend veld: {key}")
            if key in _INT_FIELDS:
                try:
                    setattr(t, key, int(value) if str(value).strip() else 0)
                except ValueError as e:
                    raise LibraryError(f"'{value}' is geen geldig getal voor {key}") from e
            else:
                setattr(t, key, (value or "").strip() or None)
        self.dirty = True

    def add_file(self, src: str | Path, overrides: dict[str, str] | None = None) -> Track:
        """Voeg een (iPod-compatibel) audiobestand toe. Kopiëren gebeurt bij save()."""
        assert self.db
        info = probe(src)
        dest = self._new_music_path(Path(src).suffix.lower())
        rel = dest.relative_to(self.device.mountpoint)
        track = Track.new(
            ipod_location=":" + ":".join(rel.parts),
            size=Path(src).stat().st_size,
            length_ms=info.length_ms,
            bitrate=info.bitrate_kbps,
            samplerate=info.samplerate,
            vbr=info.vbr,
            is_mp3=info.is_mp3,
            suffix=dest.suffix,
        )
        track.id = self.db.next_free_id()
        track.filetype = info.filetype_name
        self._apply_tags(track, info, overrides or {})
        self.db.tracks.append(track)
        self._pending_copies.append(_PendingCopy(Path(src), dest))
        self.dirty = True
        return track

    def _apply_tags(self, track: Track, info: AudioInfo, overrides: dict[str, str]) -> None:
        tags = dict(info.tags)
        tags.update({k: v for k, v in overrides.items() if v not in (None, "")})
        self.update_track(track, **{k: tags.get(k, "") for k in EDITABLE_FIELDS})

    def _new_music_path(self, suffix: str) -> Path:
        dirs = self.device.music_subdirs()
        if not dirs:
            dirs = [self.device.music_dir / f"F{i:02d}" for i in range(20)]
        taken = {c.dest for c in self._pending_copies}
        for _ in range(1000):
            d = random.choice(dirs)
            name = "".join(random.choices(string.ascii_uppercase, k=4)) + suffix
            p = d / name
            if not p.exists() and p not in taken:
                return p
        raise LibraryError("Kon geen vrije bestandsnaam op de iPod vinden")

    def delete_tracks(self, tracks: list[Track]) -> None:
        assert self.db
        doomed = {id(t) for t in tracks}
        for t in tracks:
            pending = next((c for c in self._pending_copies if self._track_dest(t) == c.dest), None)
            if pending:
                self._pending_copies.remove(pending)
            else:
                p = self.track_path(t)
                if p:
                    self._pending_deletes.append(p)
        self.db.tracks = [t for t in self.db.tracks if id(t) not in doomed]
        self.db.renumber()  # ruimt verwijzingen in playlists op
        self.dirty = True

    def _track_dest(self, t: Track) -> Path | None:
        return self.track_path(t)

    def create_playlist(self, name: str) -> Playlist:
        assert self.db
        pl = Playlist.new(name)
        self.db.playlists.append(pl)
        self.dirty = True
        return pl

    def rename_playlist(self, pl: Playlist, name: str) -> None:
        pl.name = name
        self.dirty = True

    def delete_playlist(self, pl: Playlist) -> None:
        assert self.db
        if pl.is_master:
            raise LibraryError("De hoofdbibliotheek kan niet worden verwijderd")
        self.db.playlists.remove(pl)
        self.dirty = True

    def add_to_playlist(self, pl: Playlist, tracks: list[Track]) -> None:
        if pl.is_smart:
            raise LibraryError("Aan een slimme playlist kun je niet handmatig nummers toevoegen")
        existing = set(pl.track_ids)
        for t in tracks:
            if t.id not in existing:
                pl.items.append(PlaylistItem.new(t.id))
                existing.add(t.id)
        self.dirty = True

    def remove_from_playlist(self, pl: Playlist, tracks: list[Track]) -> None:
        ids = {t.id for t in tracks}
        pl.items = [i for i in pl.items if i.track_id not in ids]
        self.dirty = True

    def move_in_playlist(self, pl: Playlist, track: Track, delta: int) -> None:
        idx = next((i for i, it in enumerate(pl.items) if it.track_id == track.id), None)
        if idx is None:
            return
        j = max(0, min(len(pl.items) - 1, idx + delta))
        pl.items.insert(j, pl.items.pop(idx))
        self.dirty = True

    # ------------------------------------------------------------------ opslaan

    def firewire_guid(self) -> str | None:
        return self._guid_override or self.device.firewire_guid()

    def needs_hash(self) -> bool:
        return hash58.has_hash58(self._original) or (
            len(self._original) > 0x32 and int.from_bytes(self._original[0x30:0x32], "little") == 1
        )

    def check_guid(self) -> None:
        """Controleer vóór het schrijven of we een geldige hash kunnen maken."""
        if not self.needs_hash():
            return
        guid = self.firewire_guid()
        if not guid:
            raise LibraryError(
                "De FireWire-ID van deze iPod is onbekend, dus de database kan niet veilig "
                "worden ondertekend. Vul hem in bij Instellingen (zie README)."
            )
        fw = hash58.parse_firewire_id(guid)
        if hash58.has_hash58(self._original) and not hash58.verify_hash58(self._original, fw):
            raise LibraryError(
                f"De FireWire-ID {guid} past niet bij de database op deze iPod. "
                "Er is niets geschreven. Controleer de ID bij Instellingen."
            )

    def save(self, progress=None) -> Path | None:
        """Schrijf alle wijzigingen naar de iPod. Geeft het pad van de back-up terug."""
        assert self.db
        self.check_guid()
        backup = self._backup()

        # 1. bestanden kopiëren (de oude database verwijst er nog niet naar)
        total = len(self._pending_copies)
        for n, c in enumerate(self._pending_copies, 1):
            if progress:
                progress(f"Kopiëren {n}/{total}: {c.src.name}", n / max(total, 1))
            c.dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(c.src, c.dest)

        # 2. database opbouwen en controleren
        data = bytearray(self.db.to_bytes())
        if self.needs_hash():
            hash58.write_hash58(data, hash58.parse_firewire_id(self.firewire_guid() or ""))
        check = ITunesDB.parse(bytes(data))
        if len(check.tracks) != len(self.db.tracks):
            raise LibraryError("Controle van de nieuwe database mislukt; er is niets vervangen")

        # 3. atomisch vervangen
        path = self.device.itunesdb_path
        tmp = path.with_name("iTunesDB.ipodsync-tmp")
        with open(tmp, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)

        # 4. opruimen: afspeeltellingen zijn verwerkt, oude bestanden weg
        itunes_dir = self.device.control_dir / "iTunes"
        stale = ["Play Counts", "iTunesStats", "PlayCounts.plist"]
        stale += [p.name for p in itunes_dir.glob("OTGPlaylistInfo*")]
        for name in stale:
            try:
                (itunes_dir / name).unlink()
            except OSError:
                pass
        for p in self._pending_deletes:
            try:
                p.unlink()
            except OSError:
                pass
        if hasattr(os, "sync"):
            os.sync()

        self._original = bytes(data)
        self._pending_copies.clear()
        self._pending_deletes.clear()
        self.dirty = False
        return backup

    # ------------------------------------------------------------------ back-ups

    def _backup_dir(self) -> Path:
        if self.backup_root is None:
            from ..config import backup_dir

            self.backup_root = backup_dir()
        d = self.backup_root / _safe_name(self.device.name)
        d.mkdir(parents=True, exist_ok=True)
        return d

    def _backup(self) -> Path | None:
        if not self._original:
            return None
        d = self._backup_dir()
        original = d / "iTunesDB-origineel"
        if not original.exists():
            original.write_bytes(self._original)  # allereerste versie: nooit weggooien
        dest = d / f"iTunesDB-{time.strftime('%Y%m%d-%H%M%S')}"
        dest.write_bytes(self._original)
        backups = sorted(p for p in d.glob("iTunesDB-2*"))
        for old in backups[:-MAX_BACKUPS]:
            old.unlink()
        return dest

    def list_backups(self) -> list[Path]:
        d = self._backup_dir()
        return sorted(d.glob("iTunesDB-*"), reverse=True)

    def restore_backup(self, backup: Path) -> None:
        data = backup.read_bytes()
        ITunesDB.parse(data)  # moet leesbaar zijn
        self._backup()
        path = self.device.itunesdb_path
        tmp = path.with_name("iTunesDB.ipodsync-tmp")
        tmp.write_bytes(data)
        os.replace(tmp, path)
        self.load()

    # ------------------------------------------------------------------ onderhoud

    def orphan_files(self) -> list[Path]:
        """Audiobestanden op de iPod waar de database niet naar verwijst."""
        referenced = {str(self.track_path(t)).lower() for t in self.tracks if t.fs_path}
        out = []
        for d in self.device.music_subdirs():
            for p in d.iterdir():
                if p.is_file() and not p.name.startswith(".") and str(p).lower() not in referenced:
                    out.append(p)
        return out


def _safe_name(s: str) -> str:
    return "".join(c if c.isalnum() or c in " -_" else "_" for c in s).strip() or "iPod"
