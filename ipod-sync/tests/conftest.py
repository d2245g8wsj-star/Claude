import shutil
import subprocess
from pathlib import Path

import pytest

from ipodsync.audio import ffmpeg_path

FIXTURES = Path(__file__).parent / "fixtures"
GUID = "0x000A27001234ABCD"


@pytest.fixture
def fake_ipod(tmp_path) -> Path:
    """Kopie van een door libgpod aangemaakte nano 3G met 7 nummers en 1 playlist."""
    dest = tmp_path / "IPOD"
    shutil.copytree(FIXTURES / "nano3g", dest)
    return dest


def _make_audio(path: Path, *args: str) -> Path:
    exe = ffmpeg_path()
    if not exe:
        pytest.skip("ffmpeg niet beschikbaar")
    subprocess.run([exe, "-loglevel", "error", "-y", *args, str(path)], check=True)
    return path


@pytest.fixture
def mp3_file(tmp_path) -> Path:
    return _make_audio(
        tmp_path / "lied.mp3",
        "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
        "-metadata", "title=Testlied", "-metadata", "artist=Tester",
        "-metadata", "album=Proef", "-metadata", "track=2/9",
        "-c:a", "libmp3lame", "-b:a", "128k",
    )


@pytest.fixture
def flac_file(tmp_path) -> Path:
    return _make_audio(
        tmp_path / "lied.flac",
        "-f", "lavfi", "-i", "sine=frequency=660:duration=1",
        "-metadata", "title=FLAC lied", "-metadata", "artist=The Tester",
    )
