"""Generate game-like OCR fixtures: a whole dialog box selected over a busy scene.

The user selects the dialog box, not a single text line, so each image also holds a name tag,
ruled lines, frames, UI buttons and scenery around the box. Python/Pillow/numpy are test tools only.
"""
import argparse
import importlib.util
import json
import os
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # importing the multilang generator must not leave __pycache__ in scripts/

import numpy as np
from PIL import Image, ImageDraw, ImageFont, features

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("multilang", HERE / "make-multilang-fixtures.py")
multilang = importlib.util.module_from_spec(spec)
spec.loader.exec_module(multilang)
F, LANGS, OCR = multilang.F, multilang.LANGS, multilang.OCR

NAMES = {"en": "Alice", "ko": "채주아", "ja": "さくら", "zh-Hans": "林小雨", "zh-Hant": "林小雨",
         "fr": "Camille", "de": "Lukas", "es": "Lucía", "pt": "João", "vi": "Minh", "ru": "Анна",
         "th": "สมชาย", "ar": "ليلى", "hi": "राहुल"}
BUTTONS = {"en": "Log Auto Save", "ko": "로그 자동 저장", "ja": "ログ オート セーブ", "zh-Hans": "记录 自动 保存",
           "zh-Hant": "記錄 自動 儲存", "fr": "Journal Auto Sauver", "de": "Log Auto Speichern",
           "es": "Registro Auto Guardar", "pt": "Registro Auto Salvar", "vi": "Nhật ký Tự động Lưu",
           "ru": "Журнал Авто Сохранить", "th": "บันทึก อัตโนมัติ เซฟ", "ar": "السجل تلقائي حفظ",
           "hi": "लॉग ऑटो सेव"}
STYLES = ["notebook", "dark-window", "bare-outline", "framed"]
SIZES = [16, 22, 30]


def scene(w, h, rng):
    """Sky, buildings with windows, foliage and a character outline: many strong edges."""
    y = np.linspace(0, 1, h)[:, None, None]
    sky = np.array([110, 170, 235]) * (1 - y) + np.array([205, 225, 250]) * y
    img = Image.fromarray(np.broadcast_to(sky, (h, w, 3)).astype(np.uint8))
    d = ImageDraw.Draw(img)
    for _ in range(6):  # buildings
        bx, bw = int(rng.integers(-40, w)), int(rng.integers(80, 260))
        top = int(rng.integers(0, h // 2))
        grey = int(rng.integers(90, 220))
        d.rectangle([bx, top, bx + bw, h], fill=(grey, grey, grey + 8), outline=(40, 40, 50), width=2)
        for wy in range(top + 12, h, 34):
            for wx in range(bx + 10, bx + bw - 20, 30):
                d.rectangle([wx, wy, wx + 16, wy + 20], fill=(60, 80, 110), outline=(230, 230, 230))
    for _ in range(10):  # foliage
        cx, cy, r = int(rng.integers(0, w)), int(rng.integers(h // 3, h)), int(rng.integers(20, 70))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(40, int(rng.integers(100, 170)), 50), outline=(20, 60, 20), width=2)
    cx = int(rng.integers(w // 4, 3 * w // 4))  # a character standing behind the box
    d.ellipse([cx - 90, 10, cx + 90, 210], fill=(245, 215, 200), outline=(60, 30, 30), width=3)
    d.polygon([(cx - 150, h), (cx - 80, 180), (cx + 80, 180), (cx + 150, h)], fill=(240, 240, 245), outline=(60, 30, 30))
    d.line([(cx, 190), (cx - 10, h)], fill=(150, 20, 30), width=14)
    noise = rng.integers(-12, 13, (h, w, 1))
    return Image.fromarray(np.clip(np.asarray(img, np.int16) + noise, 0, 255).astype(np.uint8))


def text_width(draw, text, font):
    return int(draw.textlength(text, font=font))


def render(lang, style, size, seed):
    font_file, sentences, _ = LANGS[lang]
    font = ImageFont.truetype(os.path.join(F, font_file), size)
    small = ImageFont.truetype(os.path.join(F, font_file), max(11, int(size * 0.6)))
    probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
    lines = sentences[:2]
    step = int(size * 1.9)
    pad = int(size * 2.5)
    tw = max(text_width(probe, line, font) for line in lines)
    bw = max(tw + 2 * pad, 22 * size)
    bh = step * 4 if style == "notebook" else step * 3 + (size if style == "framed" else 0)
    margin = 40
    top_extra = size * 2  # room for a name tag above the box
    w, h = bw + 2 * margin, bh + 2 * margin + top_extra
    rng = np.random.default_rng(seed)
    base = scene(w, h, rng).convert("RGBA")
    over = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    bx0, by0 = margin, margin + top_extra
    bx1, by1 = bx0 + bw, by0 + bh
    truth = []
    name = NAMES[lang]
    nw = text_width(probe, name, font)
    if style == "notebook":
        d.rounded_rectangle([bx0, by0, bx1, by1], radius=size, fill=(255, 245, 245, 205))
        for k in range(1, 5):  # ruled lines across the whole box, like a notebook page
            ly = by0 + int(size * 0.9) + k * step - step // 4
            if ly < by1 - 4:
                d.line([(bx0 + 12, ly), (bx1 - 12, ly)], fill=(205, 175, 175, 255), width=2)
        d.line([(bx0 + pad // 2, by0 + 6), (bx0 + pad // 2, by1 - 6)], fill=(210, 80, 80, 255), width=3)
        tag = [bx0 + 10, by0 - size, bx0 + 10 + nw + size * 2, by0 + int(size * 0.9)]
        d.rectangle(tag, fill=(240, 130, 120, 255))
        d.text((tag[0] + size, tag[1] + size * 0.3), name, font=font, fill=(40, 20, 20, 255))
        truth.append(name)
        for i, line in enumerate(lines):
            d.text((bx0 + pad, by0 + int(size * 0.9) + i * step), line, font=font, fill=(60, 40, 40, 255))
            truth.append(line)
        buttons = BUTTONS[lang]
        spaced = "      ".join(buttons.split(" "))
        sw = text_width(probe, spaced, small)
        d.text(((bx0 + bx1 - sw) // 2, by1 - int(size * 1.3)), spaced, font=small, fill=(130, 120, 120, 255))
        truth.append(buttons)
    elif style == "dark-window":
        tag = [bx0, by0 - int(size * 1.7), bx0 + nw + size * 2, by0 - 4]
        d.rounded_rectangle(tag, radius=6, fill=(10, 15, 40, 200), outline=(200, 200, 255, 255), width=2)
        d.text((tag[0] + size, tag[1] + size * 0.25), name, font=font, fill=(255, 225, 150, 255))
        truth.append(name)
        d.rounded_rectangle([bx0, by0, bx1, by1], radius=12, fill=(10, 15, 40, 170), outline=(200, 200, 255, 255), width=2)
        for i, line in enumerate(lines):
            d.text((bx0 + pad, by0 + size + i * step), line, font=font, fill=(245, 245, 245, 255))
            truth.append(line)
    elif style == "bare-outline":
        for i, line in enumerate(lines):
            lw = text_width(probe, line, font)
            d.text(((w - lw) // 2, by0 + size + i * step), line, font=font, fill=(255, 255, 255, 255),
                   stroke_width=max(2, size // 10), stroke_fill=(10, 10, 10, 255))
            truth.append(line)
    else:  # framed: gradient panel, double gold border, name inside the box
        gy = np.linspace(0, 1, bh)[:, None, None]
        grad = np.array([40, 60, 120]) * (1 - gy) + np.array([10, 20, 50]) * gy
        panel = np.concatenate([np.broadcast_to(grad, (bh, bw, 3)), np.full((bh, bw, 1), 225)], axis=2)
        over.paste(Image.fromarray(panel.astype(np.uint8), "RGBA"), (bx0, by0))
        d.rectangle([bx0, by0, bx1, by1], outline=(220, 180, 90, 255), width=3)
        d.rectangle([bx0 + 7, by0 + 7, bx1 - 7, by1 - 7], outline=(220, 180, 90, 255), width=1)
        d.text((bx0 + pad, by0 + int(size * 0.5)), name, font=font, fill=(235, 195, 100, 255))
        truth.append(name)
        for i, line in enumerate(lines):
            x, y = bx0 + pad, by0 + int(size * 0.5) + (i + 1) * step
            d.text((x + 2, y + 2), line, font=font, fill=(0, 0, 0, 255))
            d.text((x, y), line, font=font, fill=(255, 250, 230, 255))
            truth.append(line)
    return Image.alpha_composite(base, over).convert("RGB"), truth


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("build/scene-benchmark"))
    parser.add_argument("--smoke", action="store_true", help="two images for committed tests (tests/fixtures/ocr/scene)")
    args = parser.parse_args()
    if not features.check_feature("raqm"):
        raise RuntimeError("Pillow with libraqm is required for Arabic and Hindi shaping")
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    combos = ([("ja", "notebook", 22), ("ko", "dark-window", 16)] if args.smoke
              else [(lang, style, size) for lang in LANGS for style in STYLES for size in SIZES])
    for lang, style, size in combos:
        file = f"scene-{lang}-{style}-{size}.png"
        image, truth = render(lang, style, size, seed=size * 31 + STYLES.index(style))
        image.save(args.output / file)
        rows.append(dict(file=file, lang=lang, ocr=OCR[lang], truth="\n".join(truth),
                         size=size, style=style, layout="scene"))
    (args.output / "manifest.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("Generated", len(rows), "images in", args.output)


if __name__ == "__main__":
    main()
