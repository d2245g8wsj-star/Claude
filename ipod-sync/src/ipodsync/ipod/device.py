"""Een aangesloten iPod vinden en apparaatinformatie uitlezen."""

from __future__ import annotations

import os
import plistlib
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

CONTROL_DIR = "iPod_Control"

# Een greep uit de modelnummers, alleen voor weergave.
_MODELS = {
    "MA978": "iPod nano 3G (4 GB, zilver)",
    "MA980": "iPod nano 3G (8 GB, zilver)",
    "MB261": "iPod nano 3G (8 GB, zwart)",
    "MB249": "iPod nano 3G (8 GB, blauw)",
    "MB253": "iPod nano 3G (8 GB, groen)",
    "MB257": "iPod nano 3G (8 GB, rood)",
    "MB029": "iPod classic (80 GB, zilver)",
    "MB147": "iPod classic (80 GB, zwart)",
    "MB145": "iPod classic (160 GB, zilver)",
    "MB150": "iPod classic (160 GB, zwart)",
}


@dataclass
class IPodDevice:
    mountpoint: Path

    @property
    def control_dir(self) -> Path:
        return self.mountpoint / CONTROL_DIR

    @property
    def itunesdb_path(self) -> Path:
        return self.control_dir / "iTunes" / "iTunesDB"

    @property
    def music_dir(self) -> Path:
        return self.control_dir / "Music"

    @property
    def name(self) -> str:
        return self.mountpoint.name

    def sysinfo(self) -> dict[str, str]:
        path = self.control_dir / "Device" / "SysInfo"
        info: dict[str, str] = {}
        try:
            for line in path.read_text("utf-8", "replace").splitlines():
                if ":" in line:
                    k, v = line.split(":", 1)
                    if k.strip():
                        info[k.strip()] = v.strip()
        except OSError:
            pass
        return info

    def sysinfo_extended(self) -> dict:
        path = self.control_dir / "Device" / "SysInfoExtended"
        try:
            with open(path, "rb") as f:
                return plistlib.load(f)
        except Exception:
            return {}

    @property
    def model_number(self) -> str | None:
        m = self.sysinfo().get("ModelNumStr", "").strip()
        if not m:
            return None
        # iTunes schrijft soms 'xA978' in plaats van 'MA978'
        return ("M" + m[1:5]) if m[0] in "xXM" else m[:5]

    @property
    def model_name(self) -> str:
        num = self.model_number
        if num and num in _MODELS:
            return _MODELS[num]
        return f"iPod ({num})" if num else "iPod"

    def firewire_guid(self) -> str | None:
        """FireWire GUID uit SysInfo, SysInfoExtended of (macOS) system_profiler."""
        guid = self.sysinfo().get("FirewireGuid")
        if guid and _valid_guid(guid):
            return _norm_guid(guid)
        ext = self.sysinfo_extended().get("FireWireGUID")
        if isinstance(ext, str) and _valid_guid(ext):
            return _norm_guid(ext)
        if sys.platform == "darwin":
            return _guid_from_system_profiler()
        return None

    def disk_usage(self) -> tuple[int, int]:
        """(totaal, vrij) in bytes."""
        u = shutil.disk_usage(self.mountpoint)
        return u.total, u.free

    def music_subdirs(self) -> list[Path]:
        if not self.music_dir.is_dir():
            return []
        dirs = [p for p in self.music_dir.iterdir() if p.is_dir() and re.fullmatch(r"F\d\d", p.name)]
        return sorted(dirs)

    def eject(self) -> None:
        if sys.platform == "darwin":
            subprocess.run(["diskutil", "eject", str(self.mountpoint)], check=True, capture_output=True)
        else:
            os.sync()
            subprocess.run(["umount", str(self.mountpoint)], check=True, capture_output=True)


def _valid_guid(s: str) -> bool:
    s = s.strip()
    if s[:2].lower() == "0x":
        s = s[2:]
    return bool(re.fullmatch(r"[0-9A-Fa-f]{16}", s))


def _norm_guid(s: str) -> str:
    s = s.strip()
    if s[:2].lower() == "0x":
        s = s[2:]
    return "0x" + s.upper()


def _guid_from_system_profiler() -> str | None:
    """Op macOS is het USB-serienummer van een iPod gelijk aan de FireWire GUID."""
    try:
        out = subprocess.run(
            ["system_profiler", "SPUSBDataType", "-xml"], capture_output=True, check=True, timeout=30
        ).stdout
        data = plistlib.loads(out)
    except Exception:
        return None

    found: list[str] = []

    def walk(items):
        for it in items or []:
            name = str(it.get("_name", ""))
            serial = str(it.get("serial_num", ""))
            if "ipod" in name.lower() and _valid_guid(serial):
                found.append(_norm_guid(serial))
            walk(it.get("_items"))

    for bus in data:
        walk(bus.get("_items"))
    return found[0] if len(found) == 1 else None


def find_ipods(extra_roots: list[str] | None = None) -> list[IPodDevice]:
    roots = ["/Volumes"] if sys.platform == "darwin" else ["/media", "/run/media", "/mnt"]
    roots += extra_roots or []
    found: list[IPodDevice] = []
    for root in roots:
        base = Path(root)
        if not base.is_dir():
            continue
        candidates = list(base.iterdir())
        # /media/<gebruiker>/<volume> op Linux
        for c in list(candidates):
            try:
                if c.is_dir() and not (c / CONTROL_DIR).exists():
                    candidates.extend(p for p in c.iterdir() if p.is_dir())
            except OSError:
                continue
        for c in candidates:
            try:
                if (c / CONTROL_DIR).is_dir():
                    found.append(IPodDevice(c))
            except OSError:
                continue
    return found
