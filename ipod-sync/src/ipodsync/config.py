"""Instellingen en mappen van de app."""

from __future__ import annotations

import json
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path


def app_dir() -> Path:
    if sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support" / "iPodSync"
    else:
        base = Path.home() / ".config" / "ipodsync"
    base.mkdir(parents=True, exist_ok=True)
    return base


def backup_dir() -> Path:
    d = app_dir() / "backups"
    d.mkdir(parents=True, exist_ok=True)
    return d


def download_dir() -> Path:
    d = app_dir() / "downloads"
    d.mkdir(parents=True, exist_ok=True)
    return d


@dataclass
class Config:
    spotify_client_id: str = ""
    spotify_client_secret: str = ""
    aac_bitrate: str = "256k"
    # handmatig ingevoerde FireWire GUID per iPod (volumenaam -> GUID)
    firewire_guids: dict[str, str] = field(default_factory=dict)
    keep_downloads: bool = False

    @classmethod
    def path(cls) -> Path:
        return app_dir() / "config.json"

    @classmethod
    def load(cls) -> "Config":
        try:
            data = json.loads(cls.path().read_text("utf-8"))
        except (OSError, ValueError):
            return cls()
        known = {k: v for k, v in data.items() if k in cls.__dataclass_fields__}
        return cls(**known)

    def save(self) -> None:
        p = self.path()
        tmp = p.with_suffix(".tmp")
        tmp.write_text(json.dumps(asdict(self), indent=2), "utf-8")
        tmp.replace(p)
        try:
            p.chmod(0o600)  # bevat het Spotify-secret
        except OSError:
            pass
