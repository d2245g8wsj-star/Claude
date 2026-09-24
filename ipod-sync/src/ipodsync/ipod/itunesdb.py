"""Lezen en schrijven van de iTunesDB van een iPod (little-endian, ongecomprimeerd).

Opzet: alles wat we niet begrijpen of niet hoeven te wijzigen, bewaren we als ruwe
bytes (onbekende headervelden, onbekende mhod's, mhsd-secties van onbekend type).
Alleen de onderdelen die door wijzigingen ongeldig worden, bouwen we opnieuw op:

* mhsd 1  - tracklijst (mhlt/mhit/mhod)
* mhsd 2  - playlists, inclusief de indexen (mhod 52/53) van de master-playlist
* mhsd 3  - playlists in "podcast-formaat"
* mhsd 4  - albumlijst (mhla/mhia)
* mhsd 8  - artiestenlijst (mhli/mhii)

De layout volgt ``itdb_itunesdb.c`` uit libgpod (LGPL).
"""

from __future__ import annotations

import os
import struct
import time
import unicodedata
from dataclasses import dataclass, field
from typing import Callable, Iterable

MAC_EPOCH_OFFSET = 2082844800
FIRST_TRACK_ID = 52

# mhod-types
MHOD_TITLE = 1
MHOD_LOCATION = 2
MHOD_ALBUM = 3
MHOD_ARTIST = 4
MHOD_GENRE = 5
MHOD_FILETYPE = 6
MHOD_COMMENT = 8
MHOD_COMPOSER = 12
MHOD_GROUPING = 13
MHOD_PODCAST_URL = 15
MHOD_PODCAST_RSS = 16
MHOD_CHAPTERDATA = 17
MHOD_ALBUMARTIST = 22
MHOD_SORT_ARTIST = 23
MHOD_SORT_TITLE = 27
MHOD_SORT_ALBUM = 28
MHOD_SORT_ALBUMARTIST = 29
MHOD_SORT_COMPOSER = 30
MHOD_SPL_PREF = 50
MHOD_SPL_RULES = 51
MHOD_LIB_INDEX = 52
MHOD_LIB_JUMPTABLE = 53
MHOD_PLAYLIST = 100
MHOD_ALBUM_ALBUM = 200
MHOD_ALBUM_ARTIST = 201
MHOD_ALBUM_SORT_ARTIST = 202
MHOD_ARTIST_MHII = 300

_STRING_TYPES = set(range(1, 15)) | set(range(18, 32)) | {200, 201, 202, 300}
_RAW_STRING_TYPES = {MHOD_PODCAST_URL, MHOD_PODCAST_RSS}

SORT_TITLE, SORT_ALBUM, SORT_ARTIST, SORT_GENRE, SORT_COMPOSER = 0x03, 0x04, 0x05, 0x07, 0x12

MEDIATYPE_AUDIO = 0x01
MEDIATYPE_PODCAST = 0x04

# mhit-offsets (zie mk_mhit in libgpod)
O_ID = 0x10
O_VISIBLE = 0x14
O_FILETYPE = 0x18
O_TYPE1 = 0x1C
O_TYPE2 = 0x1D
O_COMPILATION = 0x1E
O_RATING = 0x1F
O_MODIFIED = 0x20
O_SIZE = 0x24
O_LENGTH = 0x28
O_TRACK_NR = 0x2C
O_TRACKS = 0x30
O_YEAR = 0x34
O_BITRATE = 0x38
O_SAMPLERATE = 0x3C
O_PLAYCOUNT = 0x50
O_PLAYCOUNT2 = 0x54
O_LAST_PLAYED = 0x58
O_CD_NR = 0x5C
O_CDS = 0x60
O_ADDED = 0x68
O_BOOKMARK = 0x6C
O_DBID = 0x70
O_SAMPLERATE2 = 0x88
O_DBID2 = 0xA8
O_MEDIATYPE = 0xD0
O_ALBUM_ID = 0x120
O_ID_0X124 = 0x124
O_SIZE2 = 0x12C
O_ARTIST_ID = 0x1E0
O_COMPOSER_ID = 0x1F4

MHIT_HEADER_LEN = 0x248


class ITunesDBError(Exception):
    pass


def u16(b, off: int) -> int:
    return struct.unpack_from("<H", b, off)[0]


def u32(b, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]


def u64(b, off: int) -> int:
    return struct.unpack_from("<Q", b, off)[0]


def put_u16(b: bytearray, off: int, v: int) -> None:
    struct.pack_into("<H", b, off, v & 0xFFFF)


def put_u32(b: bytearray, off: int, v: int) -> None:
    struct.pack_into("<I", b, off, v & 0xFFFFFFFF)


def put_u64(b: bytearray, off: int, v: int) -> None:
    struct.pack_into("<Q", b, off, v & 0xFFFFFFFFFFFFFFFF)


def mac_now() -> int:
    """Huidige tijd in Mac-formaat, in lokale tijd (zoals de iPod verwacht)."""
    return int(time.time()) + time.localtime().tm_gmtoff + MAC_EPOCH_OFFSET


def mac_to_unix(t: int) -> int:
    return t - MAC_EPOCH_OFFSET if t else 0


def random_id64() -> int:
    return int.from_bytes(os.urandom(8), "little") or 1


def _check_tag(buf: bytes, off: int, tag: bytes) -> None:
    if buf[off : off + 4] != tag:
        raise ITunesDBError(
            f"Verwacht {tag.decode()} op offset {off:#x}, gevonden {bytes(buf[off:off+4])!r}"
        )


# --------------------------------------------------------------------------- mhod


@dataclass
class Mhod:
    raw: bytes

    @property
    def type(self) -> int:
        return u32(self.raw, 12)

    @property
    def is_string(self) -> bool:
        return self.type in _STRING_TYPES or self.type in _RAW_STRING_TYPES

    @property
    def string(self) -> str | None:
        t = self.type
        hlen = u32(self.raw, 4)
        if t in _RAW_STRING_TYPES:
            return self.raw[hlen:].decode("utf-8", "replace")
        if t not in _STRING_TYPES or len(self.raw) < hlen + 16:
            return None
        enc = u32(self.raw, hlen)
        slen = u32(self.raw, hlen + 4)
        data = self.raw[hlen + 16 : hlen + 16 + slen]
        if enc == 2:
            return data.decode("utf-8", "replace")
        return data.decode("utf-16-le", "replace")

    @classmethod
    def make_string(cls, mtype: int, text: str) -> "Mhod":
        if mtype in _RAW_STRING_TYPES:
            data = text.encode("utf-8")
            raw = bytearray(24)
            raw[0:4] = b"mhod"
            put_u32(raw, 4, 24)
            put_u32(raw, 8, 24 + len(data))
            put_u32(raw, 12, mtype)
            return cls(bytes(raw) + data)
        data = text.encode("utf-16-le")
        raw = bytearray(40)
        raw[0:4] = b"mhod"
        put_u32(raw, 4, 24)
        put_u32(raw, 8, 40 + len(data))
        put_u32(raw, 12, mtype)
        put_u32(raw, 24, 1)  # UTF-16
        put_u32(raw, 28, len(data))
        put_u32(raw, 32, 1)
        return cls(bytes(raw) + data)

    @classmethod
    def make_position(cls, position: int) -> "Mhod":
        raw = bytearray(44)
        raw[0:4] = b"mhod"
        put_u32(raw, 4, 24)
        put_u32(raw, 8, 44)
        put_u32(raw, 12, MHOD_PLAYLIST)
        put_u32(raw, 24, position)
        return cls(bytes(raw))


def _parse_mhods(buf: bytes, pos: int, end: int) -> tuple[list[Mhod], int]:
    """Lees opeenvolgende mhod's tot ``end`` of tot een ander chunktype."""
    out: list[Mhod] = []
    while pos + 12 <= end and buf[pos : pos + 4] == b"mhod":
        tlen = u32(buf, pos + 8)
        if tlen < 24 or pos + tlen > end:
            raise ITunesDBError(f"Ongeldige mhod-lengte op {pos:#x}")
        out.append(Mhod(bytes(buf[pos : pos + tlen])))
        pos += tlen
    return out, pos


class _StringFields:
    """Mixin voor objecten met een lijst mhod's."""

    mhods: list[Mhod]

    def get_string(self, mtype: int) -> str | None:
        for m in self.mhods:
            if m.type == mtype:
                return m.string
        return None

    def set_string(self, mtype: int, value: str | None) -> None:
        idx = next((i for i, m in enumerate(self.mhods) if m.type == mtype), None)
        if not value:
            if idx is not None:
                del self.mhods[idx]
            return
        new = Mhod.make_string(mtype, value)
        if idx is None:
            self.mhods.append(new)
        else:
            self.mhods[idx] = new


# --------------------------------------------------------------------------- track


def _field(off: int, fmt: str = "<I"):
    size = struct.calcsize(fmt)

    def getter(self):
        if len(self.header) < off + size:
            return 0
        return struct.unpack_from(fmt, self.header, off)[0]

    def setter(self, v):
        if len(self.header) >= off + size:
            struct.pack_into(fmt, self.header, off, v)

    return property(getter, setter)


def _sfield(mtype: int):
    return property(lambda self: self.get_string(mtype), lambda self, v: self.set_string(mtype, v))


@dataclass(eq=False)
class Track(_StringFields):
    header: bytearray
    mhods: list[Mhod] = field(default_factory=list)

    id = _field(O_ID)
    visible = _field(O_VISIBLE)
    rating = _field(O_RATING, "<B")
    size = _field(O_SIZE)
    length_ms = _field(O_LENGTH)
    track_nr = _field(O_TRACK_NR)
    tracks_total = _field(O_TRACKS)
    year = _field(O_YEAR)
    bitrate = _field(O_BITRATE)
    playcount = _field(O_PLAYCOUNT)
    last_played = _field(O_LAST_PLAYED)
    cd_nr = _field(O_CD_NR)
    cds = _field(O_CDS)
    time_added = _field(O_ADDED)
    bookmark = _field(O_BOOKMARK)
    dbid = _field(O_DBID, "<Q")
    mediatype = _field(O_MEDIATYPE)

    title = _sfield(MHOD_TITLE)
    location = _sfield(MHOD_LOCATION)
    album = _sfield(MHOD_ALBUM)
    artist = _sfield(MHOD_ARTIST)
    genre = _sfield(MHOD_GENRE)
    filetype = _sfield(MHOD_FILETYPE)
    comment = _sfield(MHOD_COMMENT)
    composer = _sfield(MHOD_COMPOSER)
    albumartist = _sfield(MHOD_ALBUMARTIST)
    sort_artist = _sfield(MHOD_SORT_ARTIST)
    sort_title = _sfield(MHOD_SORT_TITLE)
    sort_album = _sfield(MHOD_SORT_ALBUM)
    sort_composer = _sfield(MHOD_SORT_COMPOSER)

    @property
    def samplerate(self) -> int:
        return (u32(self.header, O_SAMPLERATE) >> 16) if len(self.header) >= 0x40 else 0

    @property
    def is_podcast(self) -> bool:
        return bool(self.mediatype & MEDIATYPE_PODCAST)

    @property
    def fs_path(self) -> str | None:
        """Pad relatief aan het koppelpunt, bijv. 'iPod_Control/Music/F03/ABCD.mp3'."""
        loc = self.location
        if not loc:
            return None
        return loc.replace(":", "/").lstrip("/")

    @classmethod
    def new(
        cls,
        *,
        ipod_location: str,
        size: int,
        length_ms: int,
        bitrate: int,
        samplerate: int,
        vbr: bool = False,
        is_mp3: bool,
        suffix: str,
    ) -> "Track":
        h = bytearray(MHIT_HEADER_LEN)
        h[0:4] = b"mhit"
        put_u32(h, 4, MHIT_HEADER_LEN)
        put_u32(h, O_VISIBLE, 1)
        marker = 0
        ext = suffix.lstrip(".").upper()[:4].ljust(4)
        for ch in ext:
            marker = (marker << 8) | ord(ch)
        put_u32(h, O_FILETYPE, marker)
        h[O_TYPE1] = 1 if (vbr and is_mp3) else 0
        h[O_TYPE2] = 1 if is_mp3 else 0
        now = mac_now()
        put_u32(h, O_MODIFIED, now)
        put_u32(h, O_SIZE, size)
        put_u32(h, O_LENGTH, length_ms)
        put_u32(h, O_BITRATE, bitrate)
        put_u32(h, O_SAMPLERATE, (samplerate & 0xFFFF) << 16)
        put_u32(h, O_ADDED, now)
        dbid = random_id64()
        put_u64(h, O_DBID, dbid)
        struct.pack_into("<f", h, O_SAMPLERATE2, float(samplerate))
        put_u64(h, O_DBID2, dbid)
        put_u32(h, O_MEDIATYPE, MEDIATYPE_AUDIO)
        put_u32(h, O_SIZE2, size)
        put_u64(h, 0x134, 0x808080808080)
        put_u32(h, 0x168, 1)
        t = cls(h, [])
        t.location = ipod_location
        return t

    def to_bytes(self) -> bytes:
        body = b"".join(m.raw for m in self.mhods)
        h = bytearray(self.header)
        put_u32(h, 8, len(h) + len(body))
        put_u32(h, 12, len(self.mhods))
        return bytes(h) + body


# --------------------------------------------------------------------------- playlists


@dataclass(eq=False)
class PlaylistItem:
    header: bytearray
    mhods: list[Mhod] = field(default_factory=list)

    @property
    def track_id(self) -> int:
        return u32(self.header, 24)

    @property
    def is_podcast_group(self) -> bool:
        return u32(self.header, 16) == 256

    @classmethod
    def new(cls, track_id: int) -> "PlaylistItem":
        h = bytearray(76)
        h[0:4] = b"mhip"
        put_u32(h, 4, 76)
        put_u32(h, 12, 1)
        put_u32(h, 24, track_id)
        return cls(h, [])

    def to_bytes(self, track_id: int, position: int | None) -> bytes:
        h = bytearray(self.header)
        put_u32(h, 24, track_id)
        mhods = list(self.mhods)
        if position is not None:
            mhods = [m for m in mhods if m.type != MHOD_PLAYLIST]
            mhods.insert(0, Mhod.make_position(position))
        body = b"".join(m.raw for m in mhods)
        put_u32(h, 8, len(h) + len(body))
        put_u32(h, 12, len(mhods))
        return bytes(h) + body


def _long_playlist_mhod() -> Mhod:
    """Het 'lange' mhod-100-blok dat libgpod in elke playlist schrijft."""
    words = [0] * 6 + [
        0x010084, 0x05, 0x09, 0x03, 0x120001, 0, 0, 0,
        0xC80002, 0, 0, 0, 0x3C000D, 0, 0, 0, 0x7D0004, 0, 0, 0,
        0x7D0003, 0, 0, 0, 0x640008, 0, 0, 0, 0x640017, 0x01, 0, 0,
        0x500014, 0x01, 0, 0, 0x7D0015, 0x01,
    ] + [0] * 114
    raw = bytearray(b"mhod") + struct.pack("<III", 0x18, 0x288, MHOD_PLAYLIST)
    raw += struct.pack(f"<{len(words)}I", *words)
    assert len(raw) == 0x288
    return Mhod(bytes(raw))


@dataclass(eq=False)
class Playlist(_StringFields):
    header: bytearray
    mhods: list[Mhod] = field(default_factory=list)
    items: list[PlaylistItem] = field(default_factory=list)

    name = _sfield(MHOD_TITLE)

    @property
    def is_master(self) -> bool:
        return self.header[20] == 1

    @property
    def playlist_id(self) -> int:
        return u64(self.header, 28)

    @property
    def is_podcast(self) -> bool:
        return u16(self.header, 42) == 1

    @property
    def is_smart(self) -> bool:
        return any(m.type in (MHOD_SPL_PREF, MHOD_SPL_RULES) for m in self.mhods)

    @property
    def track_ids(self) -> list[int]:
        return [i.track_id for i in self.items if not i.is_podcast_group and i.track_id]

    @classmethod
    def new(cls, name: str, *, master: bool = False) -> "Playlist":
        h = bytearray(108)
        h[0:4] = b"mhyp"
        put_u32(h, 4, 108)
        h[20] = 1 if master else 0
        put_u32(h, 24, mac_now())
        put_u64(h, 28, random_id64())
        put_u16(h, 40, 1)
        pl = cls(h, [], [])
        pl.name = name
        pl.mhods.append(_long_playlist_mhod())
        return pl


# --------------------------------------------------------------------------- database


@dataclass(eq=False)
class Section:
    """Een mhsd-sectie. Voor type 1/2/3 houden we een model bij, anders ruwe bytes."""

    type: int
    header: bytearray
    list_header: bytearray | None = None
    raw_body: bytes = b""


@dataclass(eq=False)
class ITunesDB:
    header: bytearray
    sections: list[Section]
    tracks: list[Track]
    playlists: list[Playlist]
    podcast_playlists: list[Playlist] | None = None  # model van mhsd 3 (alleen podcast-lijsten gebruikt)

    # ---------------------------------------------------------------- parsing

    @classmethod
    def parse(cls, data: bytes) -> "ITunesDB":
        _check_tag(data, 0, b"mhbd")
        hlen = u32(data, 4)
        total = u32(data, 8)
        if total != len(data):
            raise ITunesDBError(
                f"Lengte klopt niet (header zegt {total}, bestand is {len(data)} bytes); "
                "mogelijk een gecomprimeerde of beschadigde database"
            )
        nsec = u32(data, 20)
        db = cls(bytearray(data[:hlen]), [], [], [])
        pos = hlen
        for _ in range(nsec):
            _check_tag(data, pos, b"mhsd")
            shlen = u32(data, pos + 4)
            stlen = u32(data, pos + 8)
            stype = u32(data, pos + 12)
            send = pos + stlen
            sec = Section(stype, bytearray(data[pos : pos + shlen]))
            body_start = pos + shlen
            if stype == 1:
                sec.list_header, db.tracks = _parse_tracks(data, body_start, send)
            elif stype == 2:
                sec.list_header, db.playlists = _parse_playlists(data, body_start, send)
            elif stype == 3:
                sec.list_header, db.podcast_playlists = _parse_playlists(data, body_start, send)
            else:
                sec.raw_body = bytes(data[body_start:send])
            db.sections.append(sec)
            pos = send
        if not any(s.type == 1 for s in db.sections) or not any(s.type == 2 for s in db.sections):
            raise ITunesDBError("Database mist de track- of playlistsectie")
        if not db.playlists or not db.playlists[0].is_master:
            master = next((p for p in db.playlists if p.is_master), None)
            if master is None:
                raise ITunesDBError("Geen master-playlist gevonden")
            db.playlists.remove(master)
            db.playlists.insert(0, master)
        return db

    @property
    def version(self) -> int:
        return u32(self.header, 16)

    @property
    def master(self) -> Playlist:
        return self.playlists[0]

    def track_by_id(self, tid: int) -> Track | None:
        for t in self.tracks:
            if t.id == tid:
                return t
        return None

    # ---------------------------------------------------------------- writing

    def to_bytes(self) -> bytes:
        self.renumber()
        tracks = self.tracks
        types = {sec.type for sec in self.sections}
        albums, artists = [], []
        if types & {4, 8}:
            # alleen als de database album-/artiestenlijsten heeft; anders blijven de id's staan
            albums, artists, _ = _assign_group_ids(tracks)

        out = bytearray()
        for sec in self.sections:
            if sec.type == 1:
                body = self._tracks_body(sec, tracks)
            elif sec.type == 2:
                body = self._playlists_body(sec, self.playlists, tracks, podcast_format=False)
            elif sec.type == 3:
                body = self._playlists_body(sec, self._mhsd3_playlists(), tracks, podcast_format=True)
            elif sec.type == 4:
                body = _mhla_body(sec.raw_body, albums)
            elif sec.type == 8:
                body = _mhli_body(sec.raw_body, artists)
            else:
                body = sec.raw_body
            h = bytearray(sec.header)
            put_u32(h, 8, len(h) + len(body))
            out += h + body

        header = bytearray(self.header)
        put_u32(header, 8, len(header) + len(out))
        put_u32(header, 20, len(self.sections))
        # hash-velden worden (indien nodig) door write_hash58 gezet
        return bytes(header + out)

    def next_free_id(self) -> int:
        """Tijdelijk uniek id voor een nieuwe track (wordt bij schrijven hernummerd)."""
        return max([t.id for t in self.tracks] + [FIRST_TRACK_ID - 1, 0x7FFFFF]) + 1

    def renumber(self) -> None:
        """Zet tracks in master-volgorde, nummer ze vanaf 52 en werk playlistverwijzingen bij.

        Playlist-items die naar een niet-bestaande track verwijzen, worden verwijderd.
        """
        tracks = self._ordered_tracks()
        id_map: dict[int, int] = {}
        for new_id, t in enumerate(tracks, start=FIRST_TRACK_ID):
            id_map[t.id] = new_id
        for t in tracks:
            t.id = id_map[t.id]
        self.tracks = tracks
        for pl in self.playlists + (self.podcast_playlists or []):
            kept = []
            for item in pl.items:
                if item.is_podcast_group or item.track_id == 0:
                    kept.append(item)
                elif item.track_id in id_map:
                    put_u32(item.header, 24, id_map[item.track_id])
                    kept.append(item)
            pl.items = kept

    def _ordered_tracks(self) -> list[Track]:
        """Tracks in de volgorde van de master-playlist, gevolgd door de rest (podcasts)."""
        by_id = {t.id: t for t in self.tracks}
        seen: set[int] = set()
        ordered: list[Track] = []
        for item in self.master.items:
            t = by_id.get(item.track_id)
            if t is not None and id(t) not in seen:
                ordered.append(t)
                seen.add(id(t))
        for t in self.tracks:
            if id(t) not in seen:
                ordered.append(t)
                seen.add(id(t))
        return ordered

    def _tracks_body(self, sec: Section, tracks: list[Track]) -> bytes:
        lh = bytearray(sec.list_header or _list_header(b"mhlt", 0))
        put_u32(lh, 8, len(tracks))
        parts = [bytes(lh)]
        parts.extend(t.to_bytes() for t in tracks)
        return b"".join(parts)

    def _mhsd3_playlists(self) -> list[Playlist]:
        podcast_versions = {p.playlist_id: p for p in (self.podcast_playlists or []) if p.is_podcast}
        return [podcast_versions.get(p.playlist_id, p) if p.is_podcast else p for p in self.playlists]

    def _playlists_body(
        self,
        sec: Section,
        playlists: list[Playlist],
        tracks: list[Track],
        *,
        podcast_format: bool,
    ) -> bytes:
        lh = bytearray(sec.list_header or _list_header(b"mhlp", 0))
        put_u32(lh, 8, len(playlists))
        parts = [bytes(lh)]
        for pl in playlists:
            parts.append(_playlist_bytes(pl, tracks, grouped=podcast_format and pl.is_podcast))
        return b"".join(parts)

    # ---------------------------------------------------------------- creation

    @classmethod
    def new_empty(cls, *, version: int = 0x19) -> "ITunesDB":
        """Lege database (alleen voor tests en nood-herstel)."""
        h = bytearray(0xBC)
        h[0:4] = b"mhbd"
        put_u32(h, 4, 0xBC)
        put_u32(h, 12, 1)
        put_u32(h, 16, version)
        put_u64(h, 24, random_id64())
        put_u16(h, 32, 2)
        put_u64(h, 0x24, random_id64())
        h[0x46:0x48] = b"en"
        put_u64(h, 0x48, random_id64())

        def mhsd(t: int) -> bytearray:
            s = bytearray(96)
            s[0:4] = b"mhsd"
            put_u32(s, 4, 96)
            put_u32(s, 12, t)
            return s

        sections = [
            Section(1, mhsd(1), _list_header(b"mhlt", 0)),
            Section(3, mhsd(3), _list_header(b"mhlp", 0)),
            Section(2, mhsd(2), _list_header(b"mhlp", 0)),
            Section(4, mhsd(4), raw_body=bytes(_list_header(b"mhla", 0))),
        ]
        return cls(h, sections, [], [Playlist.new("iPod", master=True)], [])


# --------------------------------------------------------------------------- helpers


def _list_header(tag: bytes, count: int) -> bytearray:
    h = bytearray(92)
    h[0:4] = tag
    put_u32(h, 4, 92)
    put_u32(h, 8, count)
    return h


def _parse_tracks(data: bytes, pos: int, end: int) -> tuple[bytearray, list[Track]]:
    _check_tag(data, pos, b"mhlt")
    lhlen = u32(data, pos + 4)
    count = u32(data, pos + 8)
    lh = bytearray(data[pos : pos + lhlen])
    pos += lhlen
    tracks: list[Track] = []
    for _ in range(count):
        _check_tag(data, pos, b"mhit")
        hlen = u32(data, pos + 4)
        tlen = u32(data, pos + 8)
        mhods, _ = _parse_mhods(data, pos + hlen, pos + tlen)
        tracks.append(Track(bytearray(data[pos : pos + hlen]), mhods))
        pos += tlen
    if pos > end:
        raise ITunesDBError("Tracklijst loopt voorbij het einde van de sectie")
    return lh, tracks


def _parse_playlists(data: bytes, pos: int, end: int) -> tuple[bytearray, list[Playlist]]:
    _check_tag(data, pos, b"mhlp")
    lhlen = u32(data, pos + 4)
    count = u32(data, pos + 8)
    lh = bytearray(data[pos : pos + lhlen])
    pos += lhlen
    playlists: list[Playlist] = []
    for _ in range(count):
        _check_tag(data, pos, b"mhyp")
        hlen = u32(data, pos + 4)
        tlen = u32(data, pos + 8)
        pend = pos + tlen
        pl = Playlist(bytearray(data[pos : pos + hlen]), [], [])
        p = pos + hlen
        while p < pend:
            tag = data[p : p + 4]
            if tag == b"mhod":
                mh, p = _parse_mhods(data, p, pend)
                if pl.items and all(m.type == MHOD_PLAYLIST for m in mh):
                    # oude stijl: mhod 100 staat na de mhip in plaats van erin
                    pl.items[-1].mhods.extend(mh)
                else:
                    pl.mhods.extend(mh)
            elif tag == b"mhip":
                ihlen = u32(data, p + 4)
                itlen = u32(data, p + 8)
                children, _ = _parse_mhods(data, p + ihlen, p + itlen)
                pl.items.append(PlaylistItem(bytearray(data[p : p + ihlen]), children))
                p += itlen
            else:
                raise ITunesDBError(f"Onverwacht chunktype {tag!r} in playlist op {p:#x}")
        playlists.append(pl)
        pos = pend
    return lh, playlists


def _playlist_bytes(pl: Playlist, tracks: list[Track], *, grouped: bool) -> bytes:
    items: list[bytes] = []
    if pl.is_master:
        # De master-playlist bevat altijd alle (niet-podcast) tracks in trackvolgorde.
        members = [t for t in tracks if not t.is_podcast]
        old_items = {it.track_id: it for it in pl.items}
        for pos, t in enumerate(members):
            item = old_items.get(t.id) or PlaylistItem.new(t.id)
            items.append(item.to_bytes(t.id, pos))
        mhods = [m for m in pl.mhods if m.type not in (MHOD_LIB_INDEX, MHOD_LIB_JUMPTABLE)]
        if members:
            mhods += _library_indexes(members)
    else:
        pos = 0
        for item in pl.items:
            if grouped:
                items.append(item.to_bytes(item.track_id, None))
                continue
            items.append(item.to_bytes(item.track_id, pos))
            pos += 1
        mhods = list(pl.mhods)
    body = b"".join(m.raw for m in mhods) + b"".join(items)
    h = bytearray(pl.header)
    put_u32(h, 8, len(h) + len(body))
    put_u32(h, 12, len(mhods))
    put_u32(h, 16, len(items))
    return bytes(h) + body


# ---- mhod 52/53 (bibliotheekindexen van de master-playlist)


def _collate(s: str) -> str:
    s = unicodedata.normalize("NFKD", s)
    return "".join(c for c in s if not unicodedata.combining(c)).casefold()


def _jump_letter(s: str) -> int:
    for ch in s:
        if ch.isalnum():
            if ch.isalpha():
                up = ch.upper()[0]
                return ord(up) if ord(up) < 0x10000 else ord("0")
            return ord("0")
    return ord("0")


def _sort_artist(t: Track) -> str | None:
    if t.sort_artist:
        return t.sort_artist
    a = t.artist
    if a and a[:4].lower() == "the ":
        return f"{a[4:]}, The" + "\x01" * 5
    return None


def _library_indexes(members: list[Track]) -> list[Mhod]:
    rows = []
    for idx, t in enumerate(members):
        title = t.sort_title or t.title or ""
        album = t.sort_album or t.album or ""
        artist = _sort_artist(t) or t.artist or ""
        genre = t.genre or ""
        composer = t.sort_composer or t.composer or ""
        rows.append(
            {
                "index": idx,
                "cd": t.cd_nr,
                "nr": t.track_nr,
                SORT_TITLE: (_collate(title), _jump_letter(title) if title else ord("0")),
                SORT_ALBUM: (_collate(album), _jump_letter(album) if album else ord("0")),
                SORT_ARTIST: (_collate(artist), _jump_letter(artist) if artist else ord("0")),
                SORT_GENRE: (_collate(genre), _jump_letter(genre) if genre else ord("0")),
                SORT_COMPOSER: (_collate(composer), _jump_letter(composer) if composer else ord("0")),
            }
        )

    def tail(r):
        return (r[SORT_ALBUM][0], r["cd"], r["nr"], r[SORT_TITLE][0])

    keys: dict[int, Callable] = {
        SORT_TITLE: lambda r: (r[SORT_TITLE][0],),
        SORT_ALBUM: tail,
        SORT_ARTIST: lambda r: (r[SORT_ARTIST][0],) + tail(r),
        SORT_GENRE: lambda r: (r[SORT_GENRE][0], r[SORT_ARTIST][0]) + tail(r),
        SORT_COMPOSER: lambda r: (r[SORT_COMPOSER][0],) + tail(r),
    }
    out: list[Mhod] = []
    for sort_type in (SORT_TITLE, SORT_ARTIST, SORT_ALBUM, SORT_GENRE, SORT_COMPOSER):
        ordered = sorted(rows, key=keys[sort_type])
        n = len(ordered)
        raw = bytearray(b"mhod") + struct.pack("<III", 24, 4 * n + 72, MHOD_LIB_INDEX) + bytes(8)
        raw += struct.pack("<II", sort_type, n) + bytes(40)
        raw += struct.pack(f"<{n}I", *(r["index"] for r in ordered))
        out.append(Mhod(bytes(raw)))

        jumps: list[list[int]] = []
        last = None
        for i, r in enumerate(ordered):
            letter = r[sort_type][1]
            if letter != last:
                jumps.append([letter, i, 0])
                last = letter
            jumps[-1][2] += 1
        j = bytearray(b"mhod") + struct.pack("<III", 24, 12 * len(jumps) + 40, MHOD_LIB_JUMPTABLE) + bytes(8)
        j += struct.pack("<II", sort_type, len(jumps)) + bytes(8)
        for letter, start, count in jumps:
            j += struct.pack("<HHII", letter, 0, start, count)
        out.append(Mhod(bytes(j)))
    return out


# ---- albums / artiesten (mhsd 4 en 8)


def _assign_group_ids(tracks: list[Track]):
    albums: dict[tuple, tuple[int, Track]] = {}
    artists: dict[str, tuple[int, Track]] = {}
    composers: dict[str, int] = {}
    for t in tracks:
        album = t.album
        if album is not None:
            key = (album, t.albumartist) if t.albumartist is not None else (album, None, t.artist)
            if key not in albums:
                albums[key] = (len(albums) + 1, t)
            t_album_id = albums[key][0]
        else:
            t_album_id = 0
        _set_if_room(t, O_ALBUM_ID, t_album_id)
        artist = t.artist
        if artist is not None:
            if artist not in artists:
                artists[artist] = (len(artists) + 1, t)
            _set_if_room(t, O_ARTIST_ID, artists[artist][0])
        else:
            _set_if_room(t, O_ARTIST_ID, 0)
        composer = t.composer
        if composer is not None:
            composers.setdefault(composer, len(composers) + 1)
            _set_if_room(t, O_COMPOSER_ID, composers[composer])
        else:
            _set_if_room(t, O_COMPOSER_ID, 0)
    return list(albums.values()), list(artists.values()), composers


def _set_if_room(t: Track, off: int, v: int) -> None:
    if len(t.header) >= off + 4:
        put_u32(t.header, off, v)


def _group_list_body(raw_body: bytes, tag: bytes, entries: Iterable[bytes], count: int) -> bytes:
    if raw_body[:4] == tag:
        lh = bytearray(raw_body[: u32(raw_body, 4)])
    else:
        lh = _list_header(tag, 0)
    put_u32(lh, 8, count)
    return bytes(lh) + b"".join(entries)


def _mhla_body(raw_body: bytes, albums: list[tuple[int, Track]]) -> bytes:
    entries = []
    for album_id, t in albums:
        mhods = [Mhod.make_string(MHOD_ALBUM_ALBUM, t.album or "")] if t.album else []
        artist = t.albumartist or t.artist
        if artist:
            mhods.append(Mhod.make_string(MHOD_ALBUM_ARTIST, artist))
        sort_artist = t.get_string(MHOD_SORT_ALBUMARTIST) or t.sort_artist
        if sort_artist:
            mhods.append(Mhod.make_string(MHOD_ALBUM_SORT_ARTIST, sort_artist))
        body = b"".join(m.raw for m in mhods)
        h = bytearray(88)
        h[0:4] = b"mhia"
        put_u32(h, 4, 88)
        put_u32(h, 8, 88 + len(body))
        put_u32(h, 12, len(mhods))
        put_u32(h, 16, album_id)
        put_u64(h, 20, random_id64())
        put_u32(h, 28, 2)
        entries.append(bytes(h) + body)
    return _group_list_body(raw_body, b"mhla", entries, len(entries))


def _mhli_body(raw_body: bytes, artists: list[tuple[int, Track]]) -> bytes:
    entries = []
    for artist_id, t in artists:
        mhods = [Mhod.make_string(MHOD_ARTIST_MHII, t.artist)] if t.artist else []
        body = b"".join(m.raw for m in mhods)
        h = bytearray(80)
        h[0:4] = b"mhii"
        put_u32(h, 4, 80)
        put_u32(h, 8, 80 + len(body))
        put_u32(h, 12, len(mhods))
        put_u32(h, 16, artist_id)
        put_u64(h, 20, random_id64())
        put_u32(h, 28, 2)
        entries.append(bytes(h) + body)
    return _group_list_body(raw_body, b"mhli", entries, len(entries))
