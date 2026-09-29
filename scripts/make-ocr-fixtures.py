"""Generates the synthetic game-text images used by tests/integration_checks.cpp.

White text with a dark 1 px outline or a 1 px drop shadow on a noisy gradient, rendered with
Pillow/FreeType (greyscale anti-aliasing, as game engines usually draw text). No user content.
Run from the repository root on Windows:  python scripts/make-ocr-fixtures.py
"""
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONTS = {"en": r"C:\Windows\Fonts\segoeui.ttf", "ko": r"C:\Windows\Fonts\malgun.ttf", "ja": r"C:\Windows\Fonts\YuGothM.ttc"}
TEXT = {"en": "Press any key to continue.", "ko": "오늘은 날씨가 좋습니다.", "ja": "今日はいい天気です。"}
CASES = [("outlined", "en", 18, "tight"), ("outlined", "ko", 18, "tight"), ("outlined", "ja", 22, "tight"),
         ("shadow", "en", 14, "tight"), ("shadow", "ko", 14, "tight"), ("shadow", "ja", 18, "tight"),
         # Loose: the selection has wide margins around a small line, so the line must be found first.
         ("outlined", "en", 12, "loose"), ("outlined", "ko", 14, "loose"), ("shadow", "ja", 14, "loose")]
OUT = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "ocr"

def render(style, lang, size, layout="tight"):
    text = TEXT[lang]
    font = ImageFont.truetype(FONTS[lang], size)
    if layout == "tight": w, h, left = int(font.getlength(text)) + 16, int(size * 1.6) + 6, 8
    else: w, h, left = int(font.getlength(text)) + 160, size * 7 + 40, 60
    rng = np.random.default_rng(size * 7 + len(text))
    x = np.linspace(0, 1, w)[None, :, None]
    y = np.linspace(0, 1, h)[:, None, None]
    base = (np.array([90, 160, 220]) * (1 - x) + np.array([230, 190, 120]) * x) * (0.7 + 0.3 * y)
    base = base.astype(np.int16) + rng.integers(-35, 36, (h, w, 1))
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    draw = ImageDraw.Draw(img)
    top = (h - size) // 2 - 1 if layout == "tight" else size * 3
    if style == "outlined":
        draw.text((left, top), text, font=font, fill=(255, 255, 255), stroke_width=1, stroke_fill=(20, 20, 20))
    else:
        draw.text((left + 1, top + 1), text, font=font, fill=(10, 10, 10))
        draw.text((left, top), text, font=font, fill=(255, 250, 235))
    return img

if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for style, lang, size, layout in CASES:
        prefix = "" if layout == "tight" else "loose-"
        path = OUT / f"{prefix}{style}-{lang}-{size}px.png"
        render(style, lang, size, layout).save(path, optimize=True)
        print(path.name)
