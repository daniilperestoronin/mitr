#!/usr/bin/env python3
"""Generate assets/icons/mitr.png (512x512 RGBA) from geometry only.

No third-party libraries. Used when the committed PNG needs a refresh:
  python3 scripts/gen_icon.py
"""

from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path

WIDTH = 512
HEIGHT = 512


def write_png(path: Path, width: int, height: int, rgba: bytearray) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * stride : (y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def sd_round_box(px: float, py: float, cx: float, cy: float, hw: float, hh: float, r: float) -> float:
    dx = abs(px - cx) - hw + r
    dy = abs(py - cy) - hh + r
    ax = max(dx, 0.0)
    ay = max(dy, 0.0)
    return math.hypot(ax, ay) + min(max(dx, dy), 0.0) - r


def cover(dist: float) -> float:
    return max(0.0, min(1.0, 0.5 - dist))


def blend(dst: list[float], r: float, g: float, b: float, a: float) -> None:
    if a <= 0.0:
        return
    ia = 1.0 - a
    dst[0] = r * a + dst[0] * ia
    dst[1] = g * a + dst[1] * ia
    dst[2] = b * a + dst[2] * ia
    dst[3] = a + dst[3] * ia


def fill_round_box(
    buf: list[list[float]],
    cx: float,
    cy: float,
    hw: float,
    hh: float,
    radius: float,
    color: tuple[float, float, float],
    alpha: float = 1.0,
) -> None:
    r, g, b = color
    x0 = max(0, int(cx - hw - radius - 2))
    x1 = min(WIDTH, int(cx + hw + radius + 3))
    y0 = max(0, int(cy - hh - radius - 2))
    y1 = min(HEIGHT, int(cy + hh + radius + 3))
    for y in range(y0, y1):
        py = y + 0.5
        row = buf[y]
        for x in range(x0, x1):
            px = x + 0.5
            a = cover(sd_round_box(px, py, cx, cy, hw, hh, radius)) * alpha
            if a > 0.0:
                blend(row[x], r, g, b, a)


def fill_circle(
    buf: list[list[float]],
    cx: float,
    cy: float,
    radius: float,
    color: tuple[float, float, float],
) -> None:
    fill_round_box(buf, cx, cy, radius, radius, radius, color)


def fill_rect(
    buf: list[list[float]],
    x: float,
    y: float,
    w: float,
    h: float,
    color: tuple[float, float, float],
) -> None:
    fill_round_box(buf, x + w / 2.0, y + h / 2.0, w / 2.0, h / 2.0, 0.5, color)


def stroke_polyline(
    buf: list[list[float]],
    points: list[tuple[float, float]],
    width: float,
    color: tuple[float, float, float],
) -> None:
    r, g, b = color
    half = width / 2.0
    for i in range(len(points) - 1):
        x0, y0 = points[i]
        x1, y1 = points[i + 1]
        dx = x1 - x0
        dy = y1 - y0
        length = math.hypot(dx, dy)
        if length < 1e-6:
            continue
        ux, uy = dx / length, dy / length
        pad = half + 2.0
        minx = max(0, int(min(x0, x1) - pad))
        maxx = min(WIDTH, int(max(x0, x1) + pad + 1))
        miny = max(0, int(min(y0, y1) - pad))
        maxy = min(HEIGHT, int(max(y0, y1) + pad + 1))
        for y in range(miny, maxy):
            py = y + 0.5
            row = buf[y]
            for x in range(minx, maxx):
                px = x + 0.5
                vx, vy = px - x0, py - y0
                t = max(0.0, min(1.0, (vx * ux + vy * uy) / length))
                qx, qy = x0 + ux * t * length, y0 + uy * t * length
                dist = math.hypot(px - qx, py - qy) - half
                a = cover(dist)
                if a > 0.0:
                    blend(row[x], r, g, b, a)
                # square caps are implicit: t is clamped to the segment


def hex_rgb(s: str) -> tuple[float, float, float]:
    s = s.lstrip("#")
    return (int(s[0:2], 16) / 255.0, int(s[2:4], 16) / 255.0, int(s[4:6], 16) / 255.0)


def render() -> bytearray:
    buf = [[[0.0, 0.0, 0.0, 0.0] for _ in range(WIDTH)] for _ in range(HEIGHT)]
    shell = hex_rgb("#21262d")
    screen = hex_rgb("#0d1117")
    title = hex_rgb("#161b22")
    green = hex_rgb("#3fb950")
    red = hex_rgb("#ff5f56")
    yellow = hex_rgb("#ffbd2e")
    traffic = hex_rgb("#27c93f")

    fill_round_box(buf, 256, 256, 224, 224, 96, shell)
    fill_round_box(buf, 256, 256, 184, 168, 28, screen)
    fill_round_box(buf, 256, 112, 184, 24, 28, title)
    fill_rect(buf, 72, 112, 368, 24, title)

    fill_circle(buf, 108, 112, 10, red)
    fill_circle(buf, 140, 112, 10, yellow)
    fill_circle(buf, 172, 112, 10, traffic)

    stroke_polyline(buf, [(140, 220), (200, 270), (140, 320)], 28, green)
    fill_round_box(buf, 280, 306, 60, 14, 4, green)

    out = bytearray(WIDTH * HEIGHT * 4)
    i = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b, a = buf[y][x]
            a = max(0.0, min(1.0, a))
            out[i] = int(max(0.0, min(1.0, r)) * 255.0 + 0.5)
            out[i + 1] = int(max(0.0, min(1.0, g)) * 255.0 + 0.5)
            out[i + 2] = int(max(0.0, min(1.0, b)) * 255.0 + 0.5)
            out[i + 3] = int(a * 255.0 + 0.5)
            i += 4
    return out


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    dest = root / "assets" / "icons" / "mitr.png"
    dest.parent.mkdir(parents=True, exist_ok=True)
    write_png(dest, WIDTH, HEIGHT, render())
    print(f"Wrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
