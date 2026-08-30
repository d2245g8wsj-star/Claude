"""Dependency-free RIFF/WAVE reading and writing.

Only numpy is required.  Supports the formats a DAW session realistically
contains: 8/16/24/32-bit PCM, 32/64-bit IEEE float, and WAVE_FORMAT_EXTENSIBLE
wrappers around either.  Audio is always handed back as float64 in the range
[-1, 1] with shape ``(frames, channels)``.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

WAVE_FORMAT_PCM = 0x0001
WAVE_FORMAT_IEEE_FLOAT = 0x0003
WAVE_FORMAT_EXTENSIBLE = 0xFFFE


class WavError(ValueError):
    """Raised when a file is not a WAVE stream we can decode."""


@dataclass
class AudioBuffer:
    """A block of audio plus the metadata needed to write it back out."""

    samples: np.ndarray  # (frames, channels) float64
    sample_rate: int
    path: Path | None = None
    source_bits: int = 24
    source_float: bool = False

    def __post_init__(self) -> None:
        self.samples = np.atleast_2d(np.asarray(self.samples, dtype=np.float64))
        if self.samples.shape[0] < self.samples.shape[1] and self.samples.shape[0] <= 8:
            # Someone handed us (channels, frames); normalise the orientation.
            self.samples = self.samples.T

    @property
    def frames(self) -> int:
        return int(self.samples.shape[0])

    @property
    def channels(self) -> int:
        return int(self.samples.shape[1])

    @property
    def duration(self) -> float:
        return self.frames / float(self.sample_rate)

    def mono(self) -> np.ndarray:
        """Mono sum used for every analysis stage."""
        return self.samples.mean(axis=1)

    def copy_with(self, samples: np.ndarray) -> "AudioBuffer":
        return AudioBuffer(
            samples=samples,
            sample_rate=self.sample_rate,
            path=self.path,
            source_bits=self.source_bits,
            source_float=self.source_float,
        )


def _iter_chunks(data: bytes, start: int, end: int):
    pos = start
    while pos + 8 <= end:
        cid = data[pos : pos + 4]
        (size,) = struct.unpack_from("<I", data, pos + 4)
        body = pos + 8
        yield cid, body, min(body + size, end)
        pos = body + size + (size & 1)  # chunks are word aligned


def _decode_pcm(raw: bytes, bits: int, channels: int) -> np.ndarray:
    if bits == 8:
        flat = np.frombuffer(raw, dtype=np.uint8).astype(np.float64)
        flat = (flat - 128.0) / 128.0
    elif bits == 16:
        flat = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 24:
        usable = len(raw) - (len(raw) % 3)
        b = np.frombuffer(raw[:usable], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        val = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        val = np.where(val & 0x800000, val - 0x1000000, val)
        flat = val.astype(np.float64) / 8388608.0
    elif bits == 32:
        flat = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise WavError(f"unsupported PCM bit depth: {bits}")
    return _deinterleave(flat, channels)


def _decode_float(raw: bytes, bits: int, channels: int) -> np.ndarray:
    dtype = "<f4" if bits == 32 else "<f8"
    if bits not in (32, 64):
        raise WavError(f"unsupported float bit depth: {bits}")
    flat = np.frombuffer(raw, dtype=dtype).astype(np.float64)
    return _deinterleave(flat, channels)


def _deinterleave(flat: np.ndarray, channels: int) -> np.ndarray:
    usable = len(flat) - (len(flat) % channels)
    return flat[:usable].reshape(-1, channels)


def read_wav(path: str | Path) -> AudioBuffer:
    """Read a WAVE file into an :class:`AudioBuffer`."""
    path = Path(path)
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise WavError(f"{path} is not a RIFF/WAVE file")
    (riff_size,) = struct.unpack_from("<I", data, 4)
    end = min(len(data), 8 + riff_size) if riff_size else len(data)

    fmt = None
    payload = None
    for cid, body, stop in _iter_chunks(data, 12, end):
        if cid == b"fmt ":
            fmt = data[body:stop]
        elif cid == b"data":
            payload = data[body:stop]
    if fmt is None or payload is None:
        raise WavError(f"{path} is missing a fmt or data chunk")

    tag, channels, rate, _bps, _align, bits = struct.unpack_from("<HHIIHH", fmt, 0)
    if tag == WAVE_FORMAT_EXTENSIBLE and len(fmt) >= 40:
        (valid_bits,) = struct.unpack_from("<H", fmt, 18)
        (sub_tag,) = struct.unpack_from("<H", fmt, 24)
        tag = sub_tag
        if valid_bits:
            bits = max(bits, valid_bits)
    if channels < 1:
        raise WavError(f"{path} declares {channels} channels")

    if tag == WAVE_FORMAT_PCM:
        samples = _decode_pcm(payload, bits, channels)
        is_float = False
    elif tag == WAVE_FORMAT_IEEE_FLOAT:
        samples = _decode_float(payload, bits, channels)
        is_float = True
    else:
        raise WavError(f"{path} uses unsupported format tag 0x{tag:04X}")

    return AudioBuffer(
        samples=samples,
        sample_rate=int(rate),
        path=path,
        source_bits=int(bits),
        source_float=is_float,
    )


def _encode_pcm(samples: np.ndarray, bits: int) -> bytes:
    # Round-then-clip so that full-scale input survives as full-scale output.
    if bits == 16:
        peak = 32767.0
        q = np.clip(np.rint(samples * 32768.0), -32768.0, peak).astype("<i2")
        return q.tobytes()
    if bits == 24:
        q = np.clip(np.rint(samples * 8388608.0), -8388608.0, 8388607.0).astype(np.int32)
        b = np.empty((q.size, 3), dtype=np.uint8)
        u = q.astype(np.uint32) & 0xFFFFFF
        b[:, 0] = u & 0xFF
        b[:, 1] = (u >> 8) & 0xFF
        b[:, 2] = (u >> 16) & 0xFF
        return b.tobytes()
    if bits == 32:
        q = np.clip(np.rint(samples * 2147483648.0), -2147483648.0, 2147483647.0)
        return q.astype("<i4").tobytes()
    if bits == 8:
        q = np.clip(np.rint(samples * 128.0) + 128.0, 0, 255).astype(np.uint8)
        return q.tobytes()
    raise WavError(f"unsupported PCM bit depth for writing: {bits}")


def write_wav(
    path: str | Path,
    samples: np.ndarray,
    sample_rate: int,
    bits: int = 24,
    float_format: bool = False,
) -> Path:
    """Write ``samples`` (``(frames, channels)`` or 1-D) as a WAVE file."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = np.atleast_2d(np.asarray(samples, dtype=np.float64))
    if data.shape[0] < data.shape[1] and data.shape[0] <= 8:
        data = data.T
    channels = data.shape[1]
    flat = data.reshape(-1)

    if float_format:
        tag = WAVE_FORMAT_IEEE_FLOAT
        bits = 32
        payload = flat.astype("<f4").tobytes()
    else:
        tag = WAVE_FORMAT_PCM
        payload = _encode_pcm(flat, bits)

    block_align = channels * bits // 8
    fmt = struct.pack(
        "<HHIIHH", tag, channels, sample_rate, sample_rate * block_align, block_align, bits
    )
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"data" + struct.pack("<I", len(payload)) + payload
    if len(payload) & 1:
        chunks += b"\x00"
    header = b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE"
    path.write_bytes(header + chunks)
    return path


def write_buffer(path: str | Path, buf: AudioBuffer, bits: int | None = None) -> Path:
    """Write an :class:`AudioBuffer`, defaulting to the source's word length."""
    return write_wav(
        path,
        buf.samples,
        buf.sample_rate,
        bits=bits or (32 if buf.source_float else buf.source_bits),
        float_format=buf.source_float and bits is None,
    )
