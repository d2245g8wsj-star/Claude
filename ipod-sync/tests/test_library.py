import pytest

from ipodsync.audio import convert_to_aac, needs_conversion
from ipodsync.ipod import hash58
from ipodsync.ipod.device import IPodDevice
from ipodsync.ipod.library import IPodLibrary, LibraryError

from .conftest import GUID


def _lib(mp, tmp_path, **kw):
    lib = IPodLibrary(IPodDevice(mp), backup_root=tmp_path / "backups", **kw)
    lib.load()
    return lib


def test_device_info(fake_ipod):
    dev = IPodDevice(fake_ipod)
    assert dev.model_number == "MA978"
    assert "nano 3G" in dev.model_name
    assert dev.firewire_guid() == GUID
    assert len(dev.music_subdirs()) == 20


def test_add_edit_delete_and_playlists(fake_ipod, tmp_path, mp3_file, flac_file):
    lib = _lib(fake_ipod, tmp_path)
    t = lib.add_file(mp3_file)
    assert (t.title, t.artist, t.album, t.track_nr, t.tracks_total) == ("Testlied", "Tester", "Proef", 2, 9)
    assert needs_conversion(flac_file)
    t2 = lib.add_file(convert_to_aac(flac_file, tmp_path), {"album": "Gekozen album"})
    assert t2.filetype == "AAC audio file" and t2.album == "Gekozen album"

    lib.update_track(lib.tracks[0], title="Hernoemd", year="1999")
    doomed = lib.tracks[1]
    doomed_path = lib.track_path(doomed)
    doomed_path.parent.mkdir(parents=True, exist_ok=True)
    doomed_path.write_bytes(b"x")
    lib.delete_tracks([doomed])
    pl = lib.create_playlist("Nieuw")
    lib.add_to_playlist(pl, [t, t2])
    lib.add_to_playlist(lib.playlists[0], [t2])
    backup = lib.save()

    assert backup and backup.exists()
    assert not doomed_path.exists()
    assert lib.track_path(t).exists() and lib.track_path(t2).exists()
    data = lib.device.itunesdb_path.read_bytes()
    assert hash58.verify_hash58(data, hash58.parse_firewire_id(GUID))

    again = _lib(fake_ipod, tmp_path)
    titles = [x.title for x in again.tracks]
    assert titles[0] == "Hernoemd" and again.tracks[0].year == 1999
    assert "Nummer 2" not in titles and titles[-2:] == ["Testlied", "FLAC lied"]
    by_name = {p.name: p for p in again.playlists}
    assert [x.title for x in again.playlist_tracks(by_name["Nieuw"])] == ["Testlied", "FLAC lied"]
    assert [x.title for x in again.playlist_tracks(by_name["Favorieten"])][-1] == "FLAC lied"
    assert again.orphan_files() == []


def test_wrong_guid_refuses_to_write(fake_ipod, tmp_path):
    before = (fake_ipod / "iPod_Control/iTunes/iTunesDB").read_bytes()
    lib = _lib(fake_ipod, tmp_path, firewire_guid="0x000A270000000001")
    lib.update_track(lib.tracks[0], title="X")
    with pytest.raises(LibraryError, match="past niet"):
        lib.save()
    assert (fake_ipod / "iPod_Control/iTunes/iTunesDB").read_bytes() == before


def test_missing_guid_refuses_to_write(fake_ipod, tmp_path):
    (fake_ipod / "iPod_Control/Device/SysInfo").write_text("ModelNumStr: MA978\n")
    lib = _lib(fake_ipod, tmp_path)
    lib.update_track(lib.tracks[0], title="X")
    with pytest.raises(LibraryError, match="FireWire"):
        lib.save()


def test_backup_and_restore(fake_ipod, tmp_path):
    lib = _lib(fake_ipod, tmp_path)
    lib.delete_tracks(lib.tracks[:3])
    lib.save()
    assert len(_lib(fake_ipod, tmp_path).tracks) == 4
    original = next(p for p in lib.list_backups() if p.name == "iTunesDB-origineel")
    lib.restore_backup(original)
    assert len(lib.tracks) == 7


def test_play_counts_are_merged(fake_ipod, tmp_path):
    import struct

    header = b"mhdp" + struct.pack("<III", 0x60, 0x10, 2) + bytes(0x60 - 16)
    entries = struct.pack("<IIII", 5, 3_000_000_000, 0, 80) + struct.pack("<IIII", 0, 0, 0, 0)
    pc = fake_ipod / "iPod_Control/iTunes/Play Counts"
    pc.write_bytes(header + entries)
    lib = _lib(fake_ipod, tmp_path)
    assert lib.tracks[0].playcount == 0 + 5 and lib.tracks[0].rating == 80
    assert lib.tracks[1].playcount == 1
    lib.save()
    assert not pc.exists()
    assert _lib(fake_ipod, tmp_path).tracks[0].playcount == 5


def test_find_duplicate(fake_ipod, tmp_path):
    lib = _lib(fake_ipod, tmp_path)
    assert lib.find_duplicate("nummer 3", "ólafur arnalds", 180_500).title == "Nummer 3"
    assert lib.find_duplicate("Nummer 3", "Ólafur Arnalds", 240_000) is None
    assert lib.find_duplicate("Onbekend", "Niemand") is None
