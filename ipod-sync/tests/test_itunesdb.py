from ipodsync.ipod.itunesdb import ITunesDB, Track, u32

from .conftest import FIXTURES


def _sections(data: bytes) -> dict[int, bytes]:
    out, pos = {}, u32(data, 4)
    while pos < len(data):
        length = u32(data, pos + 8)
        out[u32(data, pos + 12)] = data[pos : pos + length]
        pos += length
    return out


def test_parse_libgpod_database():
    db = ITunesDB.parse((FIXTURES / "nano3g/iPod_Control/iTunes/iTunesDB").read_bytes())
    assert len(db.tracks) == 7
    assert db.tracks[2].artist == "Ólafur Arnalds"
    assert db.tracks[0].location == ":iPod_Control:Music:F00:TEST0.mp3"
    assert db.tracks[0].fs_path == "iPod_Control/Music/F00/TEST0.mp3"
    assert db.master.is_master and len(db.master.items) == 7
    fav = db.playlists[1]
    assert fav.name == "Favorieten"
    assert [db.track_by_id(i).title for i in fav.track_ids] == ["Nummer 1", "Nummer 3", "Nummer 5", "Nummer 7"]


def test_roundtrip_matches_libgpod_byte_for_byte():
    """Tracks, playlists en bibliotheekindexen moeten exact gelijk zijn aan libgpod.

    Alleen de album-/artiestenlijsten (mhsd 4 en 8) verschillen, door willekeurige
    SQL-id's en de volgorde van libgpod's hashtabel.
    """
    original = (FIXTURES / "nano3g/iPod_Control/iTunes/iTunesDB").read_bytes()
    out = ITunesDB.parse(original).to_bytes()
    assert len(out) == len(original)
    a, b = _sections(original), _sections(out)
    assert a.keys() == b.keys()
    for t in (1, 2, 3, 5, 6, 10):
        assert a[t] == b[t], f"mhsd {t} wijkt af"
    for t in (4, 8):
        assert len(a[t]) == len(b[t])


def test_roundtrip_empty_old_database():
    original = (FIXTURES / "empty-v0d.iTunesDB").read_bytes()
    db = ITunesDB.parse(original)
    assert db.tracks == [] and db.master.name == "Nicks iPod"
    assert db.to_bytes() == original


def test_string_edit_and_new_track():
    db = ITunesDB.parse((FIXTURES / "nano3g/iPod_Control/iTunes/iTunesDB").read_bytes())
    db.tracks[0].title = "Een titel met ëmoji 🎵"
    db.tracks[0].genre = None
    t = Track.new(
        ipod_location=":iPod_Control:Music:F01:ABCD.m4a", size=10, length_ms=1000,
        bitrate=256, samplerate=44100, is_mp3=False, suffix=".m4a",
    )
    t.id = db.next_free_id()
    t.title, t.artist = "Nieuw", "Iemand"
    db.tracks.append(t)
    db2 = ITunesDB.parse(db.to_bytes())
    assert db2.tracks[0].title == "Een titel met ëmoji 🎵"
    assert db2.tracks[0].genre is None
    assert db2.tracks[-1].title == "Nieuw" and db2.tracks[-1].id == 52 + 7
    assert len(db2.master.items) == 8
    # track-ids zijn opnieuw vanaf 52 genummerd en de playlist verwijst er nog goed naar
    assert [t.id for t in db2.tracks] == list(range(52, 60))
    assert [db2.track_by_id(i).title for i in db2.playlists[1].track_ids][0] == "Een titel met ëmoji 🎵"
