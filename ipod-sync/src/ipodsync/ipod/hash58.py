"""hash58-checksum voor iTunesDB (iPod classic / nano 3G en 4G).

Python-port van ``itdb_hash58.c`` uit libgpod (Christophe Fergeau, BSD-licentie),
dat weer is gebaseerd op het proof-of-concept van "wtbw".
"""

from __future__ import annotations

import hashlib
import math

# Tabel 1 is de AES S-box, tabel 2 de inverse S-box.
_TABLE1 = bytes.fromhex(
    "637c777bf26b6fc53001672bfed7ab76"
    "ca82c97dfa5947f0add4a2af9ca472c0"
    "b7fd9326363ff7cc34a5e5f171d83115"
    "04c723c31896059a071280e2eb27b275"
    "09832c1a1b6e5aa0523bd6b329e32f84"
    "53d100ed20fcb15b6acbbe394a4c58cf"
    "d0efaafb434d338545f9027f503c9fa8"
    "51a3408f929d38f5bcb6da2110fff3d2"
    "cd0c13ec5f974417c4a77e3d645d1973"
    "60814fdc222a908846eeb814de5e0bdb"
    "e0323a0a4906245cc2d3ac629195e479"
    "e7c8376d8dd54ea96c56f4ea657aae08"
    "ba78252e1ca6b4c6e8dd741f4bbd8b8a"
    "703eb5664803f60e613557b986c11d9e"
    "e1f8981169d98e949b1e87e9ce5528df"
    "8ca1890dbfe6426841992d0fb054bb16"
)
_TABLE2 = bytes.fromhex(
    "52096ad53036a538bf40a39e81f3d7fb"
    "7ce339829b2fff87348e4344c4dee9cb"
    "547b9432a6c2233dee4c950b42fac34e"
    "082ea16628d924b2765ba2496d8bd125"
    "72f8f66486689816d4a45ccc5d65b692"
    "6c704850fdedb9da5e154657a78d9d84"
    "90d8ab008cbcd30af7e45805b8b34506"
    "d02c1e8fca3f0f02c1afbd0301138a6b"
    "3a9111414f67dcea97f2cfcef0b4e673"
    "96ac7422e7ad3585e2f937e81c75df6e"
    "47f11a711d29c5896fb7620eaa18be1b"
    "fc563e4bc6d279209adbc0fe78cd5af4"
    "1fdda8338807c731b11210592780ec5f"
    "60517fa919b54a0d2de57a9f93c99cef"
    "a0e03b4dae2af5b0c8ebbb3c83539961"
    "172b047eba77d626e169146355210c7d"
)
_FIXED = bytes.fromhex(
    "6723fe304533f890992107c1d012b2a1"
    "0781"
)

MHBD_DB_ID = slice(0x18, 0x20)
MHBD_HASHING_SCHEME = slice(0x30, 0x32)
MHBD_UNK_0X32 = slice(0x32, 0x46)
MHBD_HASH58 = slice(0x58, 0x6C)

HASH_SCHEME_HASH58 = 1


def parse_firewire_id(guid: str) -> bytes:
    """Zet een FireWire GUID ('0x000A27001234ABCD') om naar 20 bytes, rechts aangevuld met nullen."""
    s = guid.strip()
    if s[:2].lower() == "0x":
        s = s[2:]
    if not s or len(s) % 2 or len(s) > 40:
        raise ValueError(f"Ongeldige FireWire GUID: {guid!r}")
    return bytes.fromhex(s).ljust(20, b"\0")


def _lcm(a: int, b: int) -> int:
    if a == 0 or b == 0:
        return 1
    return a * b // math.gcd(a, b)


def _generate_key(firewire_id: bytes) -> bytes:
    y = bytearray(16)
    for i in range(4):
        cur = _lcm(firewire_id[i * 2], firewire_id[i * 2 + 1])
        hi, lo = (cur >> 8) & 0xFF, cur & 0xFF
        y[i * 4 : i * 4 + 4] = bytes((_TABLE1[hi], _TABLE2[hi], _TABLE1[lo], _TABLE2[lo]))
    return hashlib.sha1(_FIXED + bytes(y)).digest().ljust(64, b"\0")


def compute_hash58(firewire_id: bytes, data: bytes) -> bytes:
    """HMAC-SHA1-achtige hash over ``data`` (de velden moeten al op nul staan)."""
    key = _generate_key(firewire_id)
    inner = hashlib.sha1(bytes(k ^ 0x36 for k in key) + data).digest()
    return hashlib.sha1(bytes(k ^ 0x5C for k in key) + inner).digest()


def _hash_input(db: bytes) -> bytearray:
    buf = bytearray(db)
    buf[MHBD_DB_ID] = bytes(8)
    buf[MHBD_UNK_0X32] = bytes(20)
    buf[MHBD_HASH58] = bytes(20)
    buf[MHBD_HASHING_SCHEME] = HASH_SCHEME_HASH58.to_bytes(2, "little")
    return buf


def write_hash58(db: bytearray, firewire_id: bytes) -> None:
    """Zet schema en hash58 in een volledig opgebouwde iTunesDB (in-place)."""
    if len(db) < 0x6C or db[:4] != b"mhbd":
        raise ValueError("Geen geldige iTunesDB")
    digest = compute_hash58(firewire_id, bytes(_hash_input(db)))
    db[MHBD_HASHING_SCHEME] = HASH_SCHEME_HASH58.to_bytes(2, "little")
    db[MHBD_HASH58] = digest


def verify_hash58(db: bytes, firewire_id: bytes) -> bool:
    """True als de opgeslagen hash58 klopt voor deze FireWire GUID."""
    if len(db) < 0x6C or db[:4] != b"mhbd":
        return False
    return compute_hash58(firewire_id, bytes(_hash_input(db))) == bytes(db[MHBD_HASH58])


def has_hash58(db: bytes) -> bool:
    return len(db) >= 0x6C and any(db[MHBD_HASH58])
