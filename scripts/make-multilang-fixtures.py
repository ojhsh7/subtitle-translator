"""Generate reproducible synthetic OCR fixtures. Python/Pillow/numpy are test tools only."""
import os,json,argparse
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont,features
F = r"C:\Windows\Fonts"

LANGS = {  # language: (font, sentences, model that should read it)
    "en": ("segoeui.ttf", ["Press any key to continue.", "Save your progress before quitting?"], "ch"),
    "ko": ("malgun.ttf", ["오늘은 날씨가 좋습니다.", "저장하지 않은 데이터가 있습니다."], "korean"),
    "ja": ("YuGothM.ttc", ["今日はいい天気です。", "セーブデータを読み込みますか？"], "ch"),
    "zh-Hans": ("msyh.ttc", ["今天天气很好。", "要保存游戏进度吗？"], "ch"),
    "zh-Hant": ("msjh.ttc", ["今天天氣很好。", "要儲存遊戲進度嗎？"], "ch"),
    "fr": ("segoeui.ttf", ["Voulez-vous sauvegarder la partie ?", "Je t'avais dit de ne pas ouvrir cette porte."], "latin"),
    "de": ("segoeui.ttf", ["Möchtest du den Spielstand speichern?", "Ich habe dir gesagt, öffne die Tür nicht."], "latin"),
    "es": ("segoeui.ttf", ["¿Quieres guardar la partida?", "Te dije que no abrieras esa puerta."], "latin"),
    "pt": ("segoeui.ttf", ["Deseja salvar o progresso?", "Eu te disse para não abrir aquela porta."], "latin"),
    "vi": ("segoeui.ttf", ["Bạn có muốn lưu tiến trình không?", "Tôi đã bảo bạn đừng mở cánh cửa đó."], "latin"),
    "ru": ("segoeui.ttf", ["Сохранить прогресс игры?", "Я же говорил тебе не открывать эту дверь."], "eslav"),
    "th": ("LeelawUI.ttf", ["คุณต้องการบันทึกเกมหรือไม่", "ฉันบอกแล้วว่าอย่าเปิดประตูนั้น"], "th"),
    "ar": ("tahoma.ttf", ["هل تريد حفظ اللعبة؟", "قلت لك لا تفتح ذلك الباب."], "arabic"),
    "hi": ("Nirmala.ttc", ["क्या आप गेम सेव करना चाहते हैं?", "मैंने कहा था वह दरवाज़ा मत खोलो।"], "devanagari"),
}

STYLES = ["dark-on-light", "light-on-dark", "outlined-on-busy", "shadow-on-busy"]

SIZES = [12, 14, 18]

LAYOUTS = ["tight", "loose"]

def background(w, h, style, seed):
    rng = np.random.default_rng(seed)
    if style == "dark-on-light": base = np.full((h, w, 3), (240, 220, 225), np.int16)
    elif style == "light-on-dark": base = np.full((h, w, 3), (26, 30, 38), np.int16)
    else:
        x = np.linspace(0, 1, w)[None, :, None]; y = np.linspace(0, 1, h)[:, None, None]
        base = (np.array([90, 160, 220]) * (1 - x) + np.array([230, 190, 120]) * x) * (0.7 + 0.3 * y)
        base = base.astype(np.int16) + rng.integers(-35, 36, (h, w, 1))
    return Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))

def render(font_file, text, size, style, layout, seed):
    font = ImageFont.truetype(os.path.join(F, font_file), size)
    tw = int(font.getlength(text))
    if layout == "tight": w, h, x, y = tw + 16, int(size * 1.9) + 8, 8, None
    else: w, h, x, y = tw + 160, size * 7 + 40, 60, size * 3
    img = background(w, h, style, seed)
    y = (h - size) // 2 - 2 if y is None else y
    d = ImageDraw.Draw(img)
    if style == "dark-on-light": d.text((x, y), text, font=font, fill=(32, 32, 32))
    elif style == "light-on-dark": d.text((x, y), text, font=font, fill=(235, 235, 235))
    elif style == "outlined-on-busy": d.text((x, y), text, font=font, fill=(255, 255, 255), stroke_width=1, stroke_fill=(20, 20, 20))
    else: d.text((x + 1, y + 1), text, font=font, fill=(10, 10, 10)); d.text((x, y), text, font=font, fill=(255, 250, 235))
    return img

OCR = dict(zip(LANGS, ['eng','kor','jpn','chi_sim','chi_tra','fra','deu','spa','por','vie','rus','tha','ara','hin']))
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=Path('build/ocr-benchmark'))
    parser.add_argument('--smoke', action='store_true', help='14 readable regression images, 24 px')
    args=parser.parse_args()
    if not features.check_feature('raqm'):
        raise RuntimeError('Pillow with libraqm is required for Arabic and Hindi shaping')
    args.output.mkdir(parents=True,exist_ok=True)
    rows=[]
    for lang,(font,texts,_) in LANGS.items():
        for ti,text in enumerate(texts[:1] if args.smoke else texts):
            for size in ([24] if args.smoke else SIZES):
                for style in (['dark-on-light'] if args.smoke else STYLES):
                    for layout in (['tight'] if args.smoke else LAYOUTS):
                        file=f'{lang}-{ti}-{size}-{style}-{layout}.png'
                        render(font,text,size,style,layout,size*7+ti).save(args.output/file)
                        rows.append(dict(file=file,lang=lang,ocr=OCR[lang],truth=text,size=size,style=style,layout=layout))
    (args.output/'manifest.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Generated',len(rows),'images in',args.output)
if __name__ == '__main__': main()
