from ipodsync.ipod.hash58 import compute_hash58, parse_firewire_id, verify_hash58, write_hash58

from .conftest import FIXTURES, GUID

BLOB = bytes(range(256)) * 20


def test_matches_libgpod_vectors():
    # verwachte waarden berekend met itdb_hash58.c uit libgpod
    assert compute_hash58(parse_firewire_id("0x000A27001234ABCD"), BLOB).hex() == (
        "fbca770e1e813d67868b3fac3a7f3b83e039f6d0"
    )
    assert compute_hash58(parse_firewire_id("000A2700FF00EE11"), BLOB).hex() == (
        "a63768f5d37ff24d0a5ca5c1e3f30b7abbe836bd"
    )


def test_verifies_libgpod_database():
    db = (FIXTURES / "nano3g/iPod_Control/iTunes/iTunesDB").read_bytes()
    assert verify_hash58(db, parse_firewire_id(GUID))
    assert not verify_hash58(db, parse_firewire_id("0x000A270000000001"))


def test_write_then_verify():
    db = bytearray((FIXTURES / "nano3g/iPod_Control/iTunes/iTunesDB").read_bytes())
    db[0x58:0x6C] = bytes(20)
    fw = parse_firewire_id(GUID)
    write_hash58(db, fw)
    assert verify_hash58(bytes(db), fw)


def test_parse_firewire_id_rejects_garbage():
    import pytest

    with pytest.raises(ValueError):
        parse_firewire_id("xyz")
