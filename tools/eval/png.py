"""Enough PNG for review sheets, with no imaging library (tools/eval): read
8-bit, non-interlaced greyscale, RGB, grey+alpha and RGBA pictures (what
headless Chromium and mlibs.image write), place pictures side by side on
white, letter a label, write RGB. Pure Python over zlib: the materials are
small (a strip of four 440×168 cells), so speed does not matter here."""
from __future__ import annotations

import struct
import zlib
from pathlib import Path


class Picture:
    def __init__(self, width: int, height: int, rgb: bytearray | None = None):
        self.width, self.height = width, height
        self.rgb = rgb if rgb is not None else bytearray(b"\xff" * (width * height * 3))

    def paste(self, other: "Picture", x: int, y: int) -> None:
        for row in range(min(other.height, self.height - y)):
            source = other.rgb[row * other.width * 3:(row + 1) * other.width * 3]
            width = min(other.width, self.width - x)
            at = ((y + row) * self.width + x) * 3
            self.rgb[at:at + width * 3] = source[:width * 3]

    def rect(self, x: int, y: int, width: int, height: int, colour: tuple[int, int, int]) -> None:
        line = bytes(colour) * width
        for row in range(y, min(y + height, self.height)):
            at = (row * self.width + x) * 3
            self.rgb[at:at + width * 3] = line


def _unfilter(data: bytes, width: int, height: int, channels: int) -> bytearray:
    stride = width * channels
    out = bytearray(height * stride)
    previous = bytearray(stride)
    at = 0
    for row in range(height):
        kind = data[at]
        line = bytearray(data[at + 1:at + 1 + stride])
        at += 1 + stride
        if kind == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 255
        elif kind == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return out


def read(path: str | Path) -> Picture:
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    at, idat = 8, bytearray()
    width = height = depth = kind = interlace = 0
    while at < len(data):
        length, name = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if name == b"IHDR":
            width, height, depth, kind, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif name == b"IDAT":
            idat += body
        elif name == b"IEND":
            break
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(kind)
    if depth != 8 or channels is None or interlace:
        raise ValueError(f"{path}: only 8-bit, non-interlaced grey/RGB(A) pictures")
    pixels = _unfilter(zlib.decompress(bytes(idat)), width, height, channels)
    rgb = bytearray(width * height * 3)
    for i in range(width * height):
        if channels >= 3:
            r, g, b = pixels[i * channels:i * channels + 3]
        else:
            r = g = b = pixels[i * channels]
        alpha = pixels[i * channels + channels - 1] if channels in (2, 4) else 255
        if alpha != 255:  # over white, as the sheets are drawn
            r, g, b = (v * alpha // 255 + 255 - alpha for v in (r, g, b))
        rgb[i * 3:i * 3 + 3] = bytes((r, g, b))
    return Picture(width, height, rgb)


def write(picture: Picture, path: str | Path) -> None:
    stride = picture.width * 3
    raw = b"".join(b"\x00" + bytes(picture.rgb[row * stride:(row + 1) * stride]) for row in range(picture.height))

    def chunk(name: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + name + body + struct.pack(">I", zlib.crc32(name + body) & 0xFFFFFFFF)

    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", picture.width, picture.height, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def row(pictures: list[Picture], gap: int = 0) -> Picture:
    out = Picture(sum(p.width for p in pictures) + gap * (len(pictures) - 1), max(p.height for p in pictures))
    x = 0
    for picture in pictures:
        out.paste(picture, x, 0)
        x += picture.width + gap
    return out


# A and B, 5 × 7 cells: the only words a blinded sheet carries.
_GLYPHS = {"A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
           "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"]}


def letter(picture: Picture, text: str, x: int, y: int, scale: int = 4) -> None:
    for index, character in enumerate(text):
        for line, bits in enumerate(_GLYPHS[character]):
            for column, bit in enumerate(bits):
                if bit == "1":
                    picture.rect(x + (index * 6 + column) * scale, y + line * scale, scale, scale, (0, 0, 0))
