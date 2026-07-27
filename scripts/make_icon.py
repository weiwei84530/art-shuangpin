"""Generates the IME profile icon: white 特 on a red tile.

Renders each size separately (crisper than downscaling one image) and
packs them into ime/SampleIME/image/SampleIme.ico, the ICON resource the
TIP registers as its language-profile icon (TEXTSERVICE_ICON_INDEX).

Usage: python scripts/make_icon.py
"""

import os

from PIL import Image, ImageDraw, ImageFont

RED = (196, 43, 28, 255)  # Windows-style red (#C42B1C)
WHITE = (255, 255, 255, 255)
GLYPH = "特"
FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msjhbd.ttc",  # Microsoft JhengHei Bold
    r"C:\Windows\Fonts\msjh.ttc",
]
SIZES = [16, 20, 24, 32, 48, 64, 128, 256]


def find_font() -> str:
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return path
    raise FileNotFoundError("Microsoft JhengHei not found")


def render(size: int, font_path: str) -> Image.Image:
    img = Image.new("RGBA", (size, size), RED)
    draw = ImageDraw.Draw(img)
    # Binary-search the largest font size whose glyph box fits ~90% of the
    # tile, then center it optically using the actual ink box.
    target = size * 0.90
    lo, hi = 4, size * 2
    while lo < hi:
        mid = (lo + hi + 1) // 2
        font = ImageFont.truetype(font_path, mid)
        left, top, right, bottom = draw.textbbox((0, 0), GLYPH, font=font)
        if max(right - left, bottom - top) <= target:
            lo = mid
        else:
            hi = mid - 1
    font = ImageFont.truetype(font_path, lo)
    left, top, right, bottom = draw.textbbox((0, 0), GLYPH, font=font)
    x = (size - (right - left)) / 2 - left
    y = (size - (bottom - top)) / 2 - top
    draw.text((x, y), GLYPH, font=font, fill=WHITE)
    return img


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "ime", "SampleIME", "image", "SampleIme.ico")
    font_path = find_font()
    images = [render(s, font_path) for s in SIZES]
    images[-1].save(
        out,
        format="ICO",
        append_images=images[:-1],
        sizes=[(s, s) for s in SIZES],
    )
    print(f"wrote {out} ({os.path.getsize(out)} bytes)")


if __name__ == "__main__":
    main()
