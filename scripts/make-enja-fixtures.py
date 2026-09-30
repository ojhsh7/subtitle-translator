"""Generate a reproducible English + Japanese game-subtitle OCR evaluation set (dev and test splits).

Python/Pillow/numpy (and fontTools, if installed, for glyph coverage) are test tools only.

Command used for the committed numbers (Windows, repo root, PowerShell):
    $env:PYTHONPATH = 'D:\\SubtitleTranslatorToolchain\\bench-python'; $env:PYTHONDONTWRITEBYTECODE = '1'
    C:\\Users\\User\\AppData\\Local\\Programs\\Python\\Python312\\python.exe scripts\\make-enja-fixtures.py `
        --output D:\\SubtitleTranslatorToolchain\\enja-lab\\data --split both --seed 20260930

Output (paths in each manifest are relative to that manifest's folder):
    <output>/dev/manifest.json   + <output>/dev/*.png
    <output>/test/manifest.json  + <output>/test/*.png
    <output>/generation-report-<split>.json   counts, skipped font/text combinations, library versions

The output is deterministic for a given --seed, font files and Pillow/FreeType/raqm build. Sentences are
original (written for this set) and split into disjoint dev/test halves; the test split uses the dev fonts
plus test-only fonts (half of the test images use a test-only font, see row["fontUnseen"]).

Groups per language per split (370 images):
    line      120  one text line; styles dark-on-light, light-on-dark, outlined-on-busy, shadow-on-busy;
                   layouts line-tight / line-loose (margins)
    scene     150  whole dialog box selected over a busy game scene; styles notebook, dark-window,
                   bare-outline, framed; optional name tag; 1-3 dialogue lines (wrapped long sentences)
    special    40  en: italic (true italic font), oblique (synthetic slant), caps (ALL CAPS);
                   ja: ruby (furigana over some kanji words, scenes only)
    degraded   60  jpeg (quality 55-80), blur (downscale 0.6-0.75 then upscale), low-contrast (~2:1),
                   fringe (ClearType-like R/B channel shift of the text layer), translucent (see-through box
                   over a bright scene)

Truth conventions:
    * visible text lines in top-to-bottom reading order joined with "\\n"; a wrapped sentence is split exactly
      where the image breaks it (the metric collapses whitespace, so "\\n" scores like a space).
    * a name tag, when drawn, is the first line (row["name"]).
    * notebook style: the small UI button row (e.g. "Log Auto Save", drawn with wide gaps) is visible text, so
      it IS included as the LAST truth line, like make-scene-fixtures.py. It is marked in row["ui"]; to score
      dialogue only, drop the last line when row["ui"] is non-empty.
    * furigana (ruby) is drawn but is NOT part of the truth (an OCR used for translation should ignore it);
      row["ruby"] lists the [base, reading] pairs that were drawn.
"""
import argparse
import importlib.util
import io
import json
import os
import platform
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.dont_write_bytecode = True  # importing the sibling generators must not leave __pycache__ in scripts/

import numpy as np
import PIL
from PIL import Image, ImageDraw, ImageFilter, ImageFont, features

try:
    from fontTools.ttLib import TTFont
except ImportError:  # the Pillow-based fallback below is used instead
    TTFont = None

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("scene_fixtures", HERE / "make-scene-fixtures.py")
scene_fixtures = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(scene_fixtures)
scene = scene_fixtures.scene                       # busy game background: sky, buildings, foliage, character
busy_gradient = scene_fixtures.multilang.background  # noisy gradient used by the single-line fixtures
FONT_DIR = Path(scene_fixtures.F)                  # C:\Windows\Fonts

# --------------------------------------------------------------------------------------------------------------
# Sentences. All written for this set. Split: even index within a category -> dev, odd -> test.
# Japanese ruby markup: {base|reading}; the base is drawn, the reading only in the "ruby" variant.
# --------------------------------------------------------------------------------------------------------------
EN = {
    "talk": [
        "I told you not to touch the lever, didn't I?",
        "We're not leaving until we find Mira.",
        "You're late again. The caravan left an hour ago.",
        "Don't worry, I've handled worse than a few goblins.",
        "Is that really the key to the old lighthouse?",
        "Hey! Wait for me!",
        "What do you mean, the bridge is gone?",
        "If we hurry, we'll reach Port Calder by nightfall.",
        "I can't believe you actually won.",
        "They'll be back. They always come back.",
        "Thanks, Rowan. I owe you one.",
        "Who's there? Show yourself!",
        "Let's split up and search the east wing.",
        "That's the third time this week someone's stolen my lunch.",
        "You shouldn't have come here alone.",
        "How did you know my name?",
        "Sorry, but the shop's closed for the festival.",
        "Well, that could've gone better.",
        "The storm won't let up until morning.",
        "Keep your voice down, or the guards will hear us.",
        "Can you read this? My eyes aren't what they used to be.",
        "Oh, it's you. Come in, the tea's still warm.",
        "Please, take this. It belonged to my mother.",
        "Stay close. The fog gets thicker past the old mill.",
        "I'm starving. Is there anything left in the pot?",
        "Quiet! Something's moving in the reeds.",
        "You've got guts, kid. I'll give you that.",
        "The café on Elm Street makes the best pie in town.",
        "It's locked. We'll need another way in.",
        "Didn't anyone tell you? The king's been missing for weeks.",
        "Look out! Behind you!",
        "I'd trade all this gold for one more day with her.",
        "Are you ready? There's no turning back after this.",
        "Good morning, Professor Hale!",
        "Oops. I think I pressed the wrong button.",
        "Take the left path. Trust me on this one.",
        "Fine, fine. I'll carry the bags.",
        "You call that a fireball? Watch this!",
        "Welcome back, traveler. The usual room?",
        "Hmm? You're still awake?",
    ],
    "quote": [
        "She said, \"Meet me at the fountain at dawn.\"",
        "\"Run!\" shouted Captain Voss. \"Now!\"",
        "The sign reads \u201cKeep Out \u2014 By Order of the Duke.\u201d",
        "He called it a \u2018minor setback,\u2019 but the tower is on fire.",
        "\u201cI\u2019m fine,\u201d she lied, clutching her arm.",
        "Don\u2019t say \u2018maybe\u2019 when you mean \u2018no.\u2019",
        "The old man whispered, \"The river remembers.\"",
        "\u201cWhere\u2019s the map?\u201d \u201cIn your pocket, genius.\u201d",
        "Why does everyone keep calling me \"the chosen one\"?",
        "What's the password? \"Pickled turnip\"? Seriously?",
    ],
    "dash": [
        "Wait\u2014did you hear that?",
        "I thought\u2026 no, never mind.",
        "The door was open... and the room was empty.",
        "It's not a sword\u2014it's a key.",
        "Maybe tomorrow\u2026 if we're still alive.",
        "You\u2014you're supposed to be dead!",
        "Hmm... I'd rather not answer that.",
        "So this is it, huh\u2026 the end of the road.",
    ],
    "num": [
        "That'll be 3,500 G. Pay up front, please.",
        "HP 120/120 MP 45/60",
        "You found 12 Silver Coins and a Rusty Key.",
        "The ferry leaves at 6:45, so don't be late.",
        "Only 3 days left until the eclipse.",
        "Level 27 reached! Strength +2, Agility +1.",
        "I'll give you 250 G for the whole crate.",
        "Floor 9 of 10. Proceed with caution.",
        "Your party gained 1,280 EXP.",
        "Chapter 4: The Glass Orchard",
        "Only 1 in 100 travelers ever returns from the marsh.",
        "Room 204 is at the end of the hall.",
        "Item obtained: Moonstone Ring x1",
        "The lights went out on floor B2.",
    ],
    "caps": [
        "GAME OVER",
        "NEW QUEST: THE LANTERN THIEF",
        "WARNING: HULL INTEGRITY AT 15%",
        "CHECKPOINT REACHED",
        "YOU WERE DEFEATED. RETRY?",
        "ACHIEVEMENT UNLOCKED: NIGHT OWL",
        "BOSS BATTLE! PREPARE YOURSELF!",
        "MISSION COMPLETE",
        "ENEMY REINFORCEMENTS APPROACHING FROM THE SOUTH!",
        "DO NOT TURN OFF THE POWER.",
    ],
    "ui": [
        "Press any button to start.",
        "Save before returning to the map?",
        "Are you sure you want to overwrite this file?",
        "Loading... Please wait.",
        "Hold R to skip the cutscene.",
        "Return to the title screen?",
        "Not enough gold.",
        "Your inventory is full.",
        "Select a character to continue.",
        "Auto-save in progress. Do not turn off the power.",
        "Settings have been restored to default.",
        "New item added to your journal.",
        "Press Esc to open the menu.",
    ],
    "long": [
        "When the bells stop ringing, the gates will close, and anyone still outside the walls will have to "
        "survive the night on their own.",
        "I spent twelve years searching for the Sunken Library, and now that I've finally found it, I'm not sure "
        "I want to go inside.",
        "If you bring me three wolf pelts and a jar of honey from the northern farms, I'll teach you how to forge "
        "a proper blade.",
        "The merchant swore the amulet was genuine, but the moment I put it on, every candle in the tavern went "
        "out at once.",
        "Listen carefully, because I'm only going to explain this once: the red switch opens the vault, and the "
        "blue one floods the room.",
        "Nobody in this village has seen the lighthouse keeper since the storm, and his dog keeps waiting by the "
        "harbor every evening.",
        "We could take the mountain pass, which is faster but dangerous, or follow the river, which adds two days "
        "to the journey.",
        "You may think I'm just an old fisherman, but I sailed with the Crimson Fleet before you were even born.",
        "The professor's notes mention a hidden room beneath the observatory, but half of the pages have been "
        "torn out.",
        "Honestly, I don't care about the reward; I just want to know what happened to my brother on that island.",
        "Before you open that chest, remember that the last three adventurers who tried it are still stuck to "
        "the ceiling.",
        "Every lantern in the harbor is lit tonight, because tomorrow the fleet sails north and some of those "
        "ships won't return.",
        "If the reactor temperature climbs above 900 degrees, the emergency doors will seal and nobody gets "
        "out\u2014including you.",
        "\u201cWe\u2019ll meet again at the crossroads,\u201d he promised, but the crossroads had been underwater "
        "for nearly forty years.",
    ],
}

JA = {
    "vn": [
        "おはよう！今日も{早起|はやお}きだね。",
        "「ちょっと待って、忘れ物してない？」",
        "べつに、あんたのために作ったわけじゃないんだから。",
        "ねえ、放課後{屋上|おくじょう}に来てくれる？",
        "本当にそれでいいの？",
        "やっと見つけた……ずっと探してたんだよ。",
        "雨、やみそうにないね。",
        "ごめん、{約束|やくそく}の時間に遅れちゃった。",
        "{先輩|せんぱい}って、意外と甘いもの好きなんですね。",
        "大丈夫、私がついてるから。",
        "えっ、今なんて言ったの？",
        "明日のお祭り、いっしょに行こうよ～。",
        "そんな顔しないでよ、{照|て}れるじゃない。",
        "{湊|みなと}くん、ちょっといいかな？",
        "静かに！誰か来る。",
        "{紬|つむぎ}ちゃんのお弁当、すっごくおいしそう！",
        "俺のことは気にするな。先に行け！",
        "ふふっ、びっくりした？",
        "もう、しょうがないなぁ。今回だけだよ？",
        "忘れないで。私はいつでもここにいる。",
        "{神楽|かぐら}さんなら、さっき図書室にいたよ。",
        "{柊|ひいらぎ}先生、ひとつ質問してもいいですか？",
    ],
    "rpg": [
        "{勇者|ゆうしゃ}よ、よくぞ参った。",
        "この先は{魔物|まもの}の{巣|す}だ。気をつけろ。",
        "伝説の剣『{星詠|ほしよ}みの{刃|やいば}』を手に入れた！",
        "村の{井戸|いど}に何かが{潜|ひそ}んでいるらしい。",
        "{封印|ふういん}が解かれる前に、急がなければ。",
        "見慣れない顔だな。旅の者か？",
        "{宿屋|やどや}に泊まりますか？一晩50ゴールドです。",
        "{竜|りゅう}の{咆哮|ほうこう}が谷じゅうに響き渡った。",
        "お前の力、見せてもらおうか。",
        "{王女|おうじょ}様がさらわれただと!?",
        "北の{砦|とりで}まで、あと少しだ。",
        "{鍛冶屋|かじや}のガンツに頼めば、直してくれるはずだ。",
        "ここから先は、一人で行かせてくれ。",
        "裏切り者め……！",
        "{闇|やみ}の{眷属|けんぞく}どもが目を覚ました。",
        "この{扉|とびら}、びくともしないぞ。",
        "ありがとう、旅のお方。これはほんのお礼です。",
        "敵の{伏兵|ふくへい}だ！囲まれたぞ！",
        "世界の{命運|めいうん}は、君たちに{託|たく}された。",
        "{魔王|まおう}ヴァルガスの城は、あの山の向こうにある。",
    ],
    "system": [
        "セーブしますか？",
        "データを読み込んでいます…",
        "このデータに上書きしてもよろしいですか？",
        "タイトル画面に戻ります。",
        "アイテムがいっぱいで持てません。",
        "ゴールドが足りません。",
        "オートセーブ中です。電源を切らないでください。",
        "新しいクエストが追加されました。",
        "ボタンを押してください",
        "設定を初期状態に戻しました。",
        "経験値を1280獲得した！",
        "レベルが上がった！ちからが2上がった。",
        "ミッション完了",
        "ゲームオーバー",
        "実績解除：夜ふかし名人",
        "チャプター4『{硝子|ガラス}の{果樹園|かじゅえん}』",
    ],
    "kata": [
        "ティーカップはキッチンの棚にあるよ。",
        "フィールドマップを開くには、メニューボタンを押してね。",
        "ヴァイオリンの音色が、ホールいっぱいに広がった。",
        "このダンジョン、トラップだらけじゃないか！",
        "ウェイトレスのフィオナさんが、コーヒーを運んできた。",
        "シャッターが閉まる前に、ガレージから出よう。",
        "チョコレートケーキとミルクティーをください。",
        "パーティーメンバーを選んでください。",
        "ジェットコースターなんて、ぜったい乗らないからね！",
        "ファイル・フォルダ・ショートカットの名前を変更できます。",
        "スキル「ファイアボール」を習得した！",
        "ヴェルトハイム行きのフェリーは、もう出航したよ。",
    ],
    "punct": [
        "え……嘘でしょ？",
        "――それが、彼女の最後の言葉だった。",
        "「行ってきます！」「いってらっしゃ～い」",
        "『明日、駅前で待ってる』……そう書いてあった。",
        "なんで!?どうしてこうなるの!?",
        "（……聞こえてないふりをしよう）",
        "はぁ～、疲れた。",
        "ちょ、ちょっと！押さないでってば！",
        "本当に？本当の本当に？",
        "うん……ありがと。",
        "{紅茶|こうちゃ}・{珈琲|コーヒー}・ジュース、どれにする？",
        "ふんふふ～ん♪今日はいい天気！",
    ],
    "num": [
        "HPが残り12しかない！",
        "集合は明日の朝9時、駅の{改札|かいさつ}前ね。",
        "このポーション、1本300ゴールドだよ。",
        "第3章「約束の場所」",
        "残り時間は、あと30秒！",
        "2019年の夏、僕たちはこの町で出会った。",
        "3番目の扉だけは、開けてはいけない。",
        "地下5階に{隠|かく}し部屋があるらしい。",
        "10分だけ休憩しよう。",
        "攻撃力が15%アップした！",
    ],
    "long": [
        "{幼|おさな}いころに交わした約束を、あの子はまだ覚えているだろうか。夏が来るたびに、そんなことを考えてしまう。",
        "この町の地下には、百年前に封じられた古い{遺跡|いせき}が眠っていると、祖父はよく話してくれた。",
        "「もし明日、世界が終わるとしたら、君は何をする？」そう聞かれて、僕は答えられなかった。",
        "{船長|せんちょう}の話によれば、霧の向こうにある島には、誰も見たことのない花が咲いているそうだ。",
        "放課後の図書室は、いつも静かで、窓から差し込む夕日がページをオレンジ色に染めていた。",
        "王国の{騎士団|きしだん}が到着するまで、あと三日。それまでに、この村の人たちを守り抜かなければならない。",
        "忘れられた{神殿|しんでん}の奥で、少女は目を覚ました。自分の名前すら、思い出せないまま。",
        "お前が本当にこの国を救いたいのなら、まずは{北方|ほっぽう}の{賢者|けんじゃ}を訪ねるがいい。話はそれからだ。",
        "駅前のカフェでアルバイトを始めてから、もう半年。常連さんの注文も、すっかり覚えてしまった。",
        "{魔導書|まどうしょ}に書かれた{呪文|じゅもん}を最後まで唱えた瞬間、部屋じゅうの{蝋燭|ろうそく}がいっせいに消えた。",
        "あの日、{灯台|とうだい}の下で見た流れ星のことを、僕は今でもはっきりと覚えている。",
        "ギルドの{掲示板|けいじばん}には、ドラゴン退治からペット探しまで、さまざまな依頼が貼り出されていた。",
        "雪が降りしきる{峠道|とうげみち}を越えれば、ようやく{国境|こっきょう}の{関所|せきしょ}が見えてくるはずだ。",
        "気がつくと、見知らぬ天井があった。ここはどこだろう……ぼんやりした頭で、必死に記憶をたどる。",
    ],
}

NAMES = {  # name tags; even index -> dev, odd -> test
    "en": ["Mira", "Captain Voss", "Rowan", "Professor Hale", "Old Tom", "Elena", "Kael", "Sister Agnes",
           "Jun", "Innkeeper", "Lady Seraphine", "Guard", "Pip", "???", "Dorian", "Merchant"],
    "ja": ["さくら", "湊", "紬", "神崎蓮", "リリィ", "ガンツ", "村長", "謎の少女", "店主", "アリシア",
           "ユウキ", "柊先生", "エルザ", "老騎士", "ミコト", "？？？"],
}
BUTTONS = {"en": ["Log Auto Save", "Log Auto Skip Save Load", "Back Log Auto Skip Menu"],
           "ja": ["ログ オート セーブ", "ログ オート スキップ セーブ ロード", "履歴 自動 早送り 保存"]}

# --------------------------------------------------------------------------------------------------------------
# Fonts (C:\Windows\Fonts). Test split = dev fonts + test-only fonts.
# --------------------------------------------------------------------------------------------------------------
FONTS = {
    "en": {"dev": ["segoeui.ttf", "arial.ttf", "times.ttf", "georgia.ttf", "verdana.ttf", "trebuc.ttf",
                   "tahoma.ttf", "calibri.ttf", "consola.ttf", "comic.ttf", "pala.ttf", "GARA.TTF",
                   "segoeuib.ttf", "ariali.ttf", "timesi.ttf"],
           "test-only": ["corbel.ttf", "Candara.ttf", "constan.ttf", "bahnschrift.ttf", "BOOKOS.TTF", "cour.ttf",
                         "lucon.ttf"]},
    "ja": {"dev": ["YuGothM.ttc", "YuGothB.ttc", "meiryo.ttc", "msgothic.ttc", "msmincho.ttc",
                   "BIZ-UDGothicR.ttc", "yumin.ttf", "NotoSansJP-VF.ttf"],
           "test-only": ["BIZ-UDMinchoM.ttc", "UDDigiKyokashoN-R.ttc", "NotoSerifJP-VF.ttf", "YuGothR.ttc",
                         "meiryob.ttc", "NotoSansJP-Bold.otf"]},
}
ITALIC_FONTS = ["ariali.ttf", "timesi.ttf"]  # en "italic" variant (dev fonts, also used in the test split)
FACES = {  # collection faces worth using (0 = first face); proportional/UI faces add realistic variety
    "msgothic.ttc": [0, 2], "msmincho.ttc": [0, 1], "meiryo.ttc": [0, 2], "meiryob.ttc": [0, 2],
    "BIZ-UDGothicR.ttc": [0, 1], "BIZ-UDMinchoM.ttc": [0, 1], "UDDigiKyokashoN-R.ttc": [0, 1],
}
WEIGHTS = {"NotoSansJP-VF.ttf": [400, 500, 700], "NotoSerifJP-VF.ttf": [400, 600],  # VF defaults are Thin
           "bahnschrift.ttf": [350, 400, 600]}

SIZES = [12, 14, 16, 18, 22, 28, 36]
SIZE_WEIGHTS = [10, 14, 18, 18, 17, 13, 10]  # small sizes are common in windowed / low-res games
LINE_STYLES = ["dark-on-light", "light-on-dark", "outlined-on-busy", "shadow-on-busy"]
SCENE_STYLES = ["notebook", "dark-window", "bare-outline", "framed"]
DEGRADATIONS = ["jpeg", "blur", "low-contrast", "fringe", "translucent"]
GROUP_SIZES = {"line": 120, "scene": 150, "special": 40, "degraded": 60}
SPLITS = ["dev", "test"]
LANGS = ["en", "ja"]
OCR = {"en": "eng", "ja": "jpn"}
LAYOUT_ENGINE = ImageFont.Layout.RAQM if features.check_feature("raqm") else ImageFont.Layout.BASIC

RUBY_RE = re.compile(r"\{([^|{}]+)\|([^{}]+)\}")
JA_NO_START = set("、。，．・：；？！!?）」』】〕〉》ー～…―ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々%％♪")
JA_NO_END = set("「『（【〔〈《")


def parse_ruby(marked):
    """'{蒼井|あおい}さん' -> ('蒼井さん', [(0, 2, 'あおい')])"""
    out, rubies, last = "", [], 0
    for m in RUBY_RE.finditer(marked):
        out += marked[last:m.start()]
        start = len(out)
        out += m.group(1)
        rubies.append((start, len(out), m.group(2)))
        last = m.end()
    return out + marked[last:], rubies


def sentence_table():
    table = {}
    for lang, cats in (("en", EN), ("ja", JA)):
        for cat, items in cats.items():
            for i, marked in enumerate(items):
                text, rubies = parse_ruby(marked)
                table.setdefault((lang, SPLITS[i % 2]), []).append(
                    dict(id=f"{lang}-{cat}-{i:02d}", cat=cat, text=text, rubies=rubies, long=cat == "long"))
    return table


# --------------------------------------------------------------------------------------------------------------
# Fonts, glyph coverage
# --------------------------------------------------------------------------------------------------------------
_font_cache, _cmap_cache, _notdef_cache = {}, {}, {}


def load_font(spec, size):
    key = (spec, size)
    if key not in _font_cache:
        file, face, weight = spec
        font = ImageFont.truetype(str(FONT_DIR / file), size, index=face, layout_engine=LAYOUT_ENGINE)
        if weight:
            font.set_variation_by_axes([weight, 100] if file == "bahnschrift.ttf" else [weight])
        _font_cache[key] = font
    return _font_cache[key]


def missing_glyphs(text, spec):
    """Characters of text (whitespace ignored) that the font face cannot draw."""
    file, face, _ = spec
    chars = {c for c in text if not c.isspace()}
    if TTFont is not None:
        if (file, face) not in _cmap_cache:
            font = TTFont(str(FONT_DIR / file), fontNumber=face, lazy=True)
            _cmap_cache[(file, face)] = set(font.getBestCmap())
            font.close()
        cmap = _cmap_cache[(file, face)]
        return sorted(c for c in chars if ord(c) not in cmap)
    # Pillow fallback: a missing character renders exactly like .notdef (an unassigned code point).
    font = load_font(spec, 32)
    if spec not in _notdef_cache:
        m = font.getmask("\U0010FFFD")
        _notdef_cache[spec] = (m.size, bytes(m))
    missing = []
    for c in chars:
        m = font.getmask(c)
        if (m.size, bytes(m)) == _notdef_cache[spec] or m.size[0] * m.size[1] == 0:
            missing.append(c)
    return sorted(missing)


# --------------------------------------------------------------------------------------------------------------
# Drawing primitives: text is painted as alpha masks so degradations can act on the text layer only
# --------------------------------------------------------------------------------------------------------------
def srgb_luminance(c):
    c = np.asarray(c, float) / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return float(lin @ np.array([0.2126, 0.7152, 0.0722]))


def contrast_ratio(a, b):
    la, lb = srgb_luminance(a), srgb_luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def colour_with_contrast(bg, target):
    """Text colour on the line from bg towards white or black whose WCAG contrast with bg is ~target."""
    bg = np.asarray(bg, float)
    best = None
    for end in ((255, 255, 255), (0, 0, 0)):
        end = np.asarray(end, float)
        if contrast_ratio(end, bg) < target:
            continue
        lo, hi = 0.0, 1.0
        for _ in range(40):
            mid = (lo + hi) / 2
            if contrast_ratio(bg + (end - bg) * mid, bg) < target:
                lo = mid
            else:
                hi = mid
        cand = tuple(int(round(v)) for v in bg + (end - bg) * hi)
        if best is None or abs(contrast_ratio(cand, bg) - target) < abs(contrast_ratio(best, bg) - target):
            best = cand
        if srgb_luminance(bg) < 0.2:  # prefer light text on dark backgrounds
            break
    return best if best is not None else (255, 255, 255)


def text_extent(text, font, look, shear=0.0):
    """Ink box (l, t, r, b) relative to the pen origin on the baseline, including stroke, slant and shadow."""
    sw = look.get("stroke", 0)
    l, t, r, b = font.getbbox(text, anchor="ls", stroke_width=sw)
    if shear:
        r += int(np.ceil(shear * max(-t, 0))) + 1
        l -= int(np.ceil(shear * max(b, 0))) + 1
    if look.get("shadow"):
        dx, dy, _, blur, _ = look["shadow"]
        l, t = min(l, l + dx - 2 * blur), min(t, t + dy - 2 * blur)
        r, b = max(r, r + dx + 2 * blur), max(b, b + dy + 2 * blur)
    return l, t, r, b


def text_mask(text, font, stroke=0, shear=0.0, blur=0):
    """Local float mask of one text run; returns (mask, ox, oy) with (ox, oy) = pen origin inside the mask."""
    l, t, r, b = font.getbbox(text, anchor="ls", stroke_width=stroke)
    pad = 2 + stroke + 3 * blur
    up, down = max(-t, 0), max(b, 0)
    extra_r = int(np.ceil(shear * up)) + 1 if shear else 0
    extra_l = int(np.ceil(shear * down)) + 1 if shear else 0
    w, h = (r - l) + 2 * pad + extra_l + extra_r, (b - t) + 2 * pad
    img = Image.new("L", (max(w, 1), max(h, 1)), 0)
    ox, oy = pad + extra_l - l, pad - t
    ImageDraw.Draw(img).text((ox, oy), text, font=font, fill=255, anchor="ls",
                             stroke_width=stroke, stroke_fill=255)
    if shear:  # italic slant around the baseline: rows above it move right
        img = img.transform(img.size, Image.Transform.AFFINE, (1, shear, -shear * oy, 0, 1, 0),
                            resample=Image.Resampling.BICUBIC)
    if blur:
        img = img.filter(ImageFilter.GaussianBlur(blur))
    return np.asarray(img, np.float32) / 255.0, ox, oy


class Canvas:
    def __init__(self, image):
        self.a = np.asarray(image.convert("RGB"), np.float32).copy()
        self.h, self.w = self.a.shape[:2]
        self.ink = []  # ink boxes of everything painted (for the inside-the-box checks)

    def paint(self, mask, x0, y0, colour, opacity=1.0, fringe=0):
        ys, xs = np.nonzero(mask > 0.02)
        if len(xs) == 0:
            return
        ink = (x0 + xs.min(), y0 + ys.min(), x0 + xs.max() + 1 + abs(fringe), y0 + ys.max() + 1)
        if ink[0] - abs(fringe) < 0 or ink[1] < 0 or ink[2] > self.w or ink[3] > self.h:
            raise AssertionError(f"text ink {ink} outside {self.w}x{self.h} image")
        self.ink.append(ink)
        h, w = mask.shape
        cx0, cy0, cx1, cy1 = max(x0, 0), max(y0, 0), min(x0 + w, self.w), min(y0 + h, self.h)
        m = mask[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0] * opacity
        region = self.a[cy0:cy1, cx0:cx1]
        alphas = [m, m, m]
        if fringe:  # ClearType-like colour fringes: R and B coverage shifted by +-1 px in opposite directions
            alphas = [np.roll(m, fringe, axis=1), m, np.roll(m, -fringe, axis=1)]
        for c in range(3):
            region[..., c] = region[..., c] * (1 - alphas[c]) + colour[c] * alphas[c]

    def text(self, text, font, x, yb, look, shear=0.0, fringe=0):
        """Draw a text run with the pen origin at (x, baseline yb)."""
        sw = look.get("stroke", 0)
        if look.get("shadow"):
            dx, dy, col, blur, op = look["shadow"]
            m, ox, oy = text_mask(text, font, sw, shear, blur)
            self.paint(m, x - ox + dx, yb - oy + dy, col, op, fringe)
        if sw:
            m, ox, oy = text_mask(text, font, sw, shear)
            self.paint(m, x - ox, yb - oy, look["stroke_fill"], 1.0, fringe)
        m, ox, oy = text_mask(text, font, 0, shear)
        self.paint(m, x - ox, yb - oy, look["fill"], 1.0, fringe)

    def image(self):
        return Image.fromarray(np.clip(np.rint(self.a), 0, 255).astype(np.uint8))


def pick(rng, items):
    return items[int(rng.integers(len(items)))]


def jitter(rng, colour, amount=12):
    return tuple(int(np.clip(c + rng.integers(-amount, amount + 1), 0, 255)) for c in colour)


def flat_background(rng, w, h, colour, grad=10, noise=4):
    y = np.linspace(-1, 1, h)[:, None, None]
    base = np.asarray(colour, float)[None, None, :] + y * rng.uniform(-grad, grad)
    base = np.broadcast_to(base, (h, w, 3)) + rng.integers(-noise, noise + 1, (h, w, 1))
    return Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))


def busy_background(rng, w, h):
    if rng.random() < 0.5:
        return busy_gradient(w, h, "outlined-on-busy", int(rng.integers(1 << 30))), "gradient"
    return scene(w, h, rng), "scene"


LIGHT_BG = [(245, 240, 230), (250, 250, 250), (240, 220, 225), (225, 235, 245), (235, 230, 215), (232, 240, 228)]
DARK_TEXT = [(20, 20, 20), (40, 30, 30), (25, 35, 70), (60, 40, 20), (50, 50, 60)]
DARK_BG = [(20, 24, 32), (8, 8, 10), (35, 20, 40), (15, 35, 45), (40, 30, 20), (28, 28, 28)]
LIGHT_TEXT = [(240, 240, 240), (255, 250, 220), (255, 230, 150), (200, 230, 255), (255, 255, 255)]
OUTLINE_FILL = [(255, 255, 255), (255, 240, 120), (200, 240, 255), (255, 215, 230)]
OUTLINE_STROKE = [(10, 10, 10), (40, 20, 60), (20, 20, 40), (60, 20, 20)]
LOWC_BG = [(60, 70, 90), (200, 190, 170), (30, 30, 40), (120, 100, 140), (230, 230, 235), (90, 110, 80),
           (150, 150, 150), (20, 40, 60)]


def line_look(rng, style, size):
    if style == "dark-on-light":
        return dict(fill=jitter(rng, pick(rng, DARK_TEXT), 8))
    if style == "light-on-dark":
        return dict(fill=jitter(rng, pick(rng, LIGHT_TEXT), 6))
    if style == "outlined-on-busy":
        return dict(fill=pick(rng, OUTLINE_FILL), stroke=max(1, int(round(size * rng.uniform(0.05, 0.11)))),
                    stroke_fill=pick(rng, OUTLINE_STROKE))
    d = max(1, int(round(size * rng.uniform(0.05, 0.1))))
    blur = 1 if rng.random() < 0.3 else 0
    return dict(fill=pick(rng, OUTLINE_FILL), shadow=(d, d, (8, 8, 8), blur, float(rng.uniform(0.8, 1.0))))


# --------------------------------------------------------------------------------------------------------------
# Wrapping
# --------------------------------------------------------------------------------------------------------------
def wrap_en(text, font, max_w):
    lines, cur = [], ""
    for word in text.split(" "):
        cand = word if not cur else cur + " " + word
        if cur and font.getlength(cand) > max_w:
            lines.append(cur)
            cur = word
        else:
            cur = cand
    lines.append(cur)
    return [(line, []) for line in lines]


def ja_can_break(text, i, rubies):
    if i <= 0 or i >= len(text):
        return False
    a, b = text[i - 1], text[i]
    if b in JA_NO_START or a in JA_NO_END:
        return False
    if a.isascii() and b.isascii() and a.isalnum() and b.isalnum():
        return False
    if a == b and a in "…―":
        return False
    return not any(s < i < e for s, e, _ in rubies)


def wrap_ja(text, rubies, font, max_w):
    """Greedy character wrap with basic kinsoku (no closing marks/small kana at line start)."""
    lines, start = [], 0
    while start < len(text):
        end = start + 1
        while end < len(text) and font.getlength(text[start:end + 1]) <= max_w:
            end += 1
        if end < len(text) and not ja_can_break(text, end, rubies):
            back = end
            while back > start + 1 and not ja_can_break(text, back, rubies):
                back -= 1
            if ja_can_break(text, back, rubies):
                end = back
            else:  # nothing fits: overflow to the next legal break
                while end < len(text) and not ja_can_break(text, end, rubies):
                    end += 1
        local = [(s - start, e - start, r) for s, e, r in rubies if start <= s and e <= end]
        lines.append((text[start:end], local))
        start = end
    return lines


def wrap(lang, text, rubies, font, max_w):
    return wrap_en(text, font, max_w) if lang == "en" else wrap_ja(text, rubies, font, max_w)


# --------------------------------------------------------------------------------------------------------------
# Single-line images
# --------------------------------------------------------------------------------------------------------------
def compose_line(rng, lang, spec, size, style, layout, text, shear=0.0, fringe=0, low_contrast=None):
    font = load_font(spec, size)
    look = dict(fill=(0, 0, 0)) if low_contrast else line_look(rng, style, size)
    l, t, r, b = text_extent(text, font, look, shear + (0.0 if not fringe else 0.0))
    l -= abs(fringe)
    r += abs(fringe)
    if layout == "line-tight":
        ml, mr = (int(v) for v in rng.integers(3, 11, 2))
        mt, mb = (int(v) for v in rng.integers(2, 9, 2))
    else:
        ml, mr = (int(v) for v in rng.integers(24, 91, 2))
        mt, mb = (max(6, int(size * rng.uniform(1.0, 3.0))) for _ in range(2))
    w, h = ml + (r - l) + mr, mt + (b - t) + mb
    params = {}
    if low_contrast:
        bg = flat_background(rng, w, h, pick(rng, LOWC_BG), grad=6, noise=3)
        mean = np.asarray(bg, float).reshape(-1, 3).mean(0)
        look["fill"] = colour_with_contrast(mean, low_contrast)
        params["contrast"] = round(contrast_ratio(look["fill"], mean), 2)
        style = "light-on-dark" if srgb_luminance(look["fill"]) > srgb_luminance(mean) else "dark-on-light"
    elif style == "dark-on-light":
        bg = flat_background(rng, w, h, jitter(rng, pick(rng, LIGHT_BG), 6))
    elif style == "light-on-dark":
        bg = flat_background(rng, w, h, jitter(rng, pick(rng, DARK_BG), 6))
    else:
        bg, params["background"] = busy_background(rng, w, h)
    cv = Canvas(bg)
    cv.text(text, font, ml - l, mt - t, look, shear, fringe)
    return cv.image(), [text], style, params


# --------------------------------------------------------------------------------------------------------------
# Dialog-box scenes
# --------------------------------------------------------------------------------------------------------------
def scene_looks(rng, style, size, translucent):
    """Colours of box, text and name for one scene."""
    p = dict(style=style)
    if style == "notebook":
        fill = pick(rng, [(255, 245, 245), (250, 248, 235), (240, 246, 255), (255, 252, 240)])
        p.update(box=fill + (int(rng.integers(200, 236)),), rule=tuple(int(c * 0.8) for c in fill),
                 tag=pick(rng, [(240, 130, 120), (120, 170, 230), (150, 200, 130), (240, 190, 90)]),
                 text=dict(fill=pick(rng, [(60, 40, 40), (30, 30, 50), (40, 40, 40), (70, 50, 30)])),
                 name=dict(fill=(40, 20, 20)), ui=(130, 120, 120))
    elif style == "dark-window":
        alpha = int(rng.integers(60, 131)) if translucent else int(rng.integers(150, 211))
        p.update(box=pick(rng, [(10, 15, 40), (0, 0, 0), (30, 10, 30), (15, 30, 30)]) + (alpha,),
                 border=pick(rng, [(200, 200, 255), (255, 255, 255), (180, 160, 120)]),
                 text=dict(fill=pick(rng, [(245, 245, 245), (255, 255, 235), (230, 240, 255)])),
                 name=dict(fill=pick(rng, [(255, 225, 150), (150, 220, 255), (255, 180, 200)])))
    elif style == "bare-outline":
        sw = max(2, int(round(size * rng.uniform(0.08, 0.13))))
        p.update(text=dict(fill=pick(rng, OUTLINE_FILL[:3]), stroke=sw, stroke_fill=pick(rng, OUTLINE_STROKE)),
                 band=int(rng.integers(40, 110)) if rng.random() < 0.4 else 0)
    else:  # framed
        top, bottom = pick(rng, [((40, 60, 120), (10, 20, 50)), ((90, 30, 30), (30, 10, 10)),
                                 ((30, 70, 60), (10, 25, 20)), ((60, 40, 90), (20, 10, 35))])
        d = max(1, int(round(size / 14)))
        p.update(grad=(top, bottom), panel_alpha=int(rng.integers(90, 141)) if translucent else int(rng.integers(215, 236)),
                 border=pick(rng, [(220, 180, 90), (200, 200, 210)]),
                 text=dict(fill=pick(rng, [(255, 250, 230), (240, 240, 255)]), shadow=(d, d, (0, 0, 0), 0, 1.0)),
                 name=dict(fill=(235, 195, 100), shadow=(d, d, (0, 0, 0), 0, 1.0)))
    return p


def compose_scene(rng, lang, spec, size, style, dialogue, name, ui, *, shear=0.0, fringe=0, ruby=False,
                  low_contrast=None, translucent=False, wrap_w=0):
    """dialogue: wrapped lines [(text, [(start, end, reading)])]. Returns image, truth lines, params."""
    font = load_font(spec, size)
    small = load_font(spec, max(10, int(round(size * 0.62))))
    rfont = load_font(spec, max(7, int(round(size * 0.5))))
    L = scene_looks(rng, style, size, translucent)
    if low_contrast:  # flat text colour, no outline or shadow; colour is set after the box is drawn
        L["text"], L["name"] = dict(fill=(0, 0, 0)), dict(fill=(0, 0, 0))
        if "box" in L:
            L["box"] = L["box"][:3] + (int(rng.integers(236, 256)),)
        if "panel_alpha" in L:
            L["panel_alpha"] = int(rng.integers(236, 256))
    params = {}
    up_probe, down_probe = ("Hbdfhkl", "gjpqy") if lang == "en" else ("漢字あア「", "漢字あア」gy")
    asc = -font.getbbox(up_probe, anchor="ls")[1]
    desc = max(font.getbbox(down_probe, anchor="ls")[3], 1)
    ruby_h = 0
    if ruby:
        ruby_gap = max(1, int(round(size * 0.06)))
        ruby_h = -rfont.getbbox("あか", anchor="ls")[1] + ruby_gap + 1
    tag_style = style in ("notebook", "dark-window") and name
    inside_name = style == "framed" and name
    # rows: the name inside a framed box takes the first row
    rows = ([(name, [], L["name"], 0.0)] if inside_name else []) + [(t, r, L["text"], shear) for t, r in dialogue]
    ext = [text_extent(t, font, look, sh) for t, _, look, sh in rows]
    A = max([asc] + [-e[1] for e in ext])
    D = max([desc] + [e[3] for e in ext])
    step = int(round(size * (rng.uniform(1.8, 2.05) if ruby else rng.uniform(1.35, 1.9))))
    step = max(step, A + D + 2 + ruby_h)
    padx = max(6, int(round(size * rng.uniform(0.7, 2.0))))
    pad_top = max(4, int(round(size * rng.uniform(0.45, 1.0))))
    pad_bot = max(4, int(round(size * rng.uniform(0.45, 1.0))))
    widths = [e[2] - e[0] + 2 * abs(fringe) for e in ext]
    inner = max(widths)
    if style != "bare-outline":
        inner = max(inner, int(wrap_w))
    tag = None
    if tag_style:
        n_ext = text_extent(name, font, L["name"])
        tvp, thp = max(3, int(round(size * 0.3))), max(6, int(round(size * rng.uniform(0.6, 1.1))))
        n_a = max(asc, -n_ext[1])
        tag_w, tag_h = (n_ext[2] - n_ext[0]) + 2 * thp, n_a + max(desc, n_ext[3]) + 2 * tvp
        tag = dict(w=tag_w, h=tag_h, tvp=tvp, thp=thp, a=n_a, ext=n_ext)
        inner = max(inner, tag_w + 24 - 2 * padx)
        if style == "notebook":  # tag straddles the top edge: first row starts below it
            pad_top = max(pad_top, tag_h - int(round(tag_h * 0.55)) + max(3, size // 4))
    pad_top += ruby_h
    ui_words, ui_gap, ui_h, ui_a = [], 0, 0, 0
    if ui:
        ui_words = ui.split(" ")
        ui_gap = int(round(size * rng.uniform(1.2, 2.6)))
        ui_ext = [text_extent(wd, small, {}) for wd in ui_words]
        ui_w = sum(e[2] - e[0] for e in ui_ext) + ui_gap * (len(ui_words) - 1)
        ui_a = max(-font.getbbox("Hd漢", anchor="ls")[1] * 0 + max(-e[1] for e in ui_ext),
                   -small.getbbox(up_probe, anchor="ls")[1])
        ui_d = max([small.getbbox(down_probe, anchor="ls")[3]] + [e[3] for e in ui_ext])
        ui_h = int(round(size * 0.5)) + ui_a + ui_d
        inner = max(inner, ui_w + 2 * size - 2 * padx)
    n_slots = max(len(rows), int(rng.choice([1, 2, 3], p=[0.3, 0.4, 0.3])) + (1 if inside_name else 0))
    bw = inner + 2 * padx
    bh = pad_top + A + (n_slots - 1) * step + D + pad_bot + ui_h
    ml, mr = (int(v) for v in rng.integers(6, 49, 2))
    mt, mb = (int(v) for v in rng.integers(6, 41, 2))
    tag_extra = 0
    if tag is not None:
        tag_extra = int(round(tag["h"] * 0.55)) if style == "notebook" else tag["h"] + 4
    W, H = ml + bw + mr, mt + tag_extra + bh + mb
    bx0, by0 = ml, mt + tag_extra
    bx1, by1 = bx0 + bw, by0 + bh

    base = scene(W, H, rng)
    if translucent:  # bright snowy/sunlit scene behind a see-through box
        k = float(rng.uniform(0.45, 0.7))
        base = Image.fromarray((np.asarray(base, float) * (1 - k) + 255 * k).astype(np.uint8))
        params["brighten"] = round(k, 2)
    elif rng.random() < 0.2:  # night scene
        k = float(rng.uniform(0.35, 0.6))
        base = Image.fromarray((np.asarray(base, float) * k).astype(np.uint8))
        params["dim"] = round(k, 2)
    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    tag_rect = None
    if tag is not None:
        tx0 = bx0 + (int(rng.integers(8, 25)) if style == "notebook" else int(rng.choice([0, 0, 12])))
        ty0 = by0 - tag_extra
        tag_rect = [tx0, ty0, tx0 + tag["w"], ty0 + tag["h"]]
    if style == "notebook":
        d.rounded_rectangle([bx0, by0, bx1, by1], radius=max(4, size), fill=L["box"])
        yb = by0 + pad_top + A
        for k in range(n_slots):  # ruled lines just below each row's descenders
            ly = yb + k * step + D + max(1, size // 12)
            if ly < by1 - 4:
                d.line([(bx0 + 12, ly), (bx1 - 12, ly)], fill=L["rule"] + (255,), width=2 if size >= 16 else 1)
        if padx >= 12:
            mx = bx0 + padx // 2
            d.line([(mx, by0 + 6), (mx, by1 - 6)], fill=(210, 80, 80, 255), width=3 if size >= 16 else 2)
        if tag_rect:
            d.rectangle(tag_rect, fill=L["tag"] + (255,))
    elif style == "dark-window":
        if tag_rect:
            d.rounded_rectangle(tag_rect, radius=6, fill=L["box"], outline=L["border"] + (255,), width=2)
        d.rounded_rectangle([bx0, by0, bx1, by1], radius=12, fill=L["box"], outline=L["border"] + (255,), width=2)
    elif style == "bare-outline":
        if L["band"]:
            gy = np.abs(np.linspace(-1, 1, bh))[:, None]
            band = np.zeros((bh, W, 4), np.uint8)
            band[..., 3] = (L["band"] * (1 - gy ** 2)).astype(np.uint8)
            over.paste(Image.fromarray(band, "RGBA"), (0, by0))
    else:  # framed: gradient panel, double border
        (top, bottom), gy = L["grad"], np.linspace(0, 1, bh)[:, None, None]
        grad = np.array(top) * (1 - gy) + np.array(bottom) * gy
        panel = np.concatenate([np.broadcast_to(grad, (bh, bw, 3)), np.full((bh, bw, 1), L["panel_alpha"])], axis=2)
        over.paste(Image.fromarray(panel.astype(np.uint8), "RGBA"), (bx0, by0))
        d.rectangle([bx0, by0, bx1, by1], outline=L["border"] + (255,), width=3)
        d.rectangle([bx0 + 7, by0 + 7, bx1 - 7, by1 - 7], outline=L["border"] + (255,), width=1)
    image = Image.alpha_composite(base.convert("RGBA"), over).convert("RGB")
    cv = Canvas(image)
    if low_contrast or translucent:
        inner_bg = cv.a[by0 + 8:by1 - 8, bx0 + 8:bx1 - 8].reshape(-1, 3).mean(0)
        if low_contrast:
            L["text"]["fill"] = colour_with_contrast(inner_bg, low_contrast)
            L["name"]["fill"] = L["text"]["fill"]
            if tag_rect:
                tag_bg = cv.a[tag_rect[1] + 2:tag_rect[3] - 2, tag_rect[0] + 2:tag_rect[2] - 2].reshape(-1, 3).mean(0)
                L["name"]["fill"] = colour_with_contrast(tag_bg, low_contrast)
        params["contrast"] = round(contrast_ratio(L["text"]["fill"], inner_bg), 2)

    truth, ruby_drawn = [], []
    if tag_rect:
        nx = tag_rect[0] + tag["thp"] - tag["ext"][0]
        nyb = tag_rect[1] + tag["tvp"] + tag["a"]
        cv.text(name, font, nx, nyb, L["name"], 0.0, fringe)
        truth.append(name)
    n_before = len(cv.ink)
    yb0 = by0 + pad_top + A
    for k, (text, rubies, look, sh) in enumerate(rows):
        yb = yb0 + k * step
        e = ext[k]
        if style == "bare-outline":
            x = (W - (e[2] - e[0])) // 2 - e[0]
        else:
            x = bx0 + padx - e[0] + abs(fringe)
        cv.text(text, font, x, yb, look, sh, fringe)
        truth.append(text)
        if ruby:
            rlook = dict(look)
            if rlook.get("stroke"):
                rlook["stroke"] = max(1, rlook["stroke"] // 2)
            for s, en, reading in rubies:
                base_txt = text[s:en]
                x_start = x + font.getlength(text[:s])
                centre = x_start + font.getlength(base_txt) / 2
                top = yb + font.getbbox(base_txt, anchor="ls")[1]
                rx = int(round(centre - rfont.getlength(reading) / 2))
                cv.text(reading, rfont, rx, top - ruby_gap, rlook, 0.0, fringe)
                ruby_drawn.append([base_txt, reading])
    if ui:
        x = bx0 + (bw - ui_w) // 2 if rng.random() < 0.6 else bx1 - padx - ui_w
        yb = by1 - pad_bot - ui_h + int(round(size * 0.5)) + ui_a
        for wd, e in zip(ui_words, ui_ext):
            cv.text(wd, small, x - e[0], yb, dict(fill=L["ui"]), 0.0, fringe)
            x += (e[2] - e[0]) + ui_gap
        truth.append(ui)
    # everything except the name tag must lie inside the box (bare-outline: inside the image, checked by paint)
    if style != "bare-outline":
        for ink in cv.ink[n_before:]:
            if ink[0] < bx0 or ink[1] < by0 or ink[2] > bx1 or ink[3] > by1:
                raise AssertionError(f"ink {ink} outside box {(bx0, by0, bx1, by1)}")
    if tag_rect:
        for ink in cv.ink[:n_before]:
            if ink[0] < tag_rect[0] or ink[1] < tag_rect[1] or ink[2] > tag_rect[2] or ink[3] > tag_rect[3]:
                raise AssertionError(f"name ink {ink} outside tag {tag_rect}")
    if ruby_drawn:
        params["rubyDrawn"] = ruby_drawn
    return cv.image(), truth, params


# --------------------------------------------------------------------------------------------------------------
# Post-processing degradations
# --------------------------------------------------------------------------------------------------------------
def degrade(image, rng, kind):
    if kind == "jpeg":
        q = int(rng.integers(55, 81))
        buf = io.BytesIO()
        image.save(buf, "JPEG", quality=q)
        return Image.open(io.BytesIO(buf.getvalue())).convert("RGB"), {"quality": q}
    if kind == "blur":
        f = float(rng.uniform(0.6, 0.75))
        down_name, down = pick(rng, [("bilinear", Image.Resampling.BILINEAR), ("box", Image.Resampling.BOX),
                                     ("bicubic", Image.Resampling.BICUBIC)])
        up_name, up = pick(rng, [("bilinear", Image.Resampling.BILINEAR), ("bicubic", Image.Resampling.BICUBIC)])
        w, h = image.size
        small = image.resize((max(1, round(w * f)), max(1, round(h * f))), down)
        return small.resize((w, h), up), {"scale": round(f, 3), "down": down_name, "up": up_name}
    return image, {}


# --------------------------------------------------------------------------------------------------------------
# Planning
# --------------------------------------------------------------------------------------------------------------
def quota(rng, items, n, weights=None):
    """Exactly-proportional assignment of items to n slots, shuffled."""
    w = np.ones(len(items)) if weights is None else np.asarray(weights, float)
    raw = w / w.sum() * n
    counts = np.floor(raw).astype(int)
    order = np.argsort(-(raw - counts), kind="stable")
    counts[order[:n - counts.sum()]] += 1
    out = [it for it, c in zip(items, counts) for _ in range(c)]
    return [out[k] for k in rng.permutation(len(out))]


class Deck:
    """Draw without replacement; reshuffle when exhausted."""

    def __init__(self, items, rng):
        self.items, self.rng, self.order = list(items), rng, []

    def draw(self):
        if not self.order:
            self.order = [self.items[k] for k in self.rng.permutation(len(self.items))]
        return self.order.pop()


class Context:
    """Per (split, language) decks and bookkeeping."""

    def __init__(self, seed, split, lang, sentences):
        si, li = SPLITS.index(split), LANGS.index(lang)
        r = lambda k: np.random.default_rng([seed, si, li, 1000 + k])
        self.split, self.lang = split, lang
        s = sentences[(lang, split)]
        self.decks = {"all": Deck(s, r(0)), "short": Deck([x for x in s if not x["long"]], r(1)),
                      "long": Deck([x for x in s if x["long"]], r(2)),
                      "ruby": Deck([x for x in s if x["rubies"]], r(3)),
                      "plain": Deck([x for x in s if not x["long"] and x["cat"] != "caps"], r(4))}
        self.names = Deck(NAMES[lang][SPLITS.index(split)::2], r(5))
        self.fonts = {"seen": Deck(FONTS[lang]["dev"], r(6)), "unseen": Deck(FONTS[lang]["test-only"], r(7))}
        self.skips = []

    def font_spec(self, rng, kind, file=None):
        file = file or self.fonts[kind].draw()
        face = int(pick(rng, FACES.get(file, [0])))
        weight = pick(rng, WEIGHTS.get(file, [None]))
        return (file, face, int(weight) if weight else None)

    def covered(self, spec, text, what):
        miss = missing_glyphs(text, spec)
        if miss:
            self.skips.append(dict(font=spec[0], face=spec[1], item=what, missing="".join(miss)))
        return not miss

    def sentence(self, deck, spec, transform=None):
        for _ in range(4 * len(self.decks[deck].items) + 4):
            s = self.decks[deck].draw()
            text = transform(s["text"]) if transform else s["text"]
            if self.covered(spec, text + "".join(r for _, _, r in s["rubies"]), s["id"]):
                return s, text
        return None, None

    def name(self, spec):
        for _ in range(3 * len(self.names.items)):
            n = self.names.draw()
            if self.covered(spec, n, "name:" + n):
                return n
        return None


def scene_dialogue(ctx, rng, font, spec, max_w, *, deck_first="all", transform=None, ruby_first=False):
    """Pick 1-3 sentences and wrap them into at most three dialogue lines."""
    lang = ctx.lang
    mode = pick(rng, ["one", "one", "two", "two", "flow"] if not ruby_first else ["one", "one", "two"])
    first_deck = "ruby" if ruby_first else ("long" if mode == "one" and rng.random() < 0.6 else deck_first)
    if transform and first_deck == "long":
        first_deck = "plain"
    s1, t1 = ctx.sentence(first_deck, spec, transform)
    if s1 is None:
        return None
    picked = [(s1, t1)]
    if mode in ("two", "flow"):
        s2, t2 = ctx.sentence("plain" if transform else "short", spec, transform)
        if s2 is not None and s2["id"] != s1["id"]:
            picked.append((s2, t2))
    for _ in range(12):
        if mode == "flow" and len(picked) == 2:
            joined = picked[0][1] + (" " if lang == "en" else "") + picked[1][1]
            off = len(picked[0][1]) + (1 if lang == "en" else 0)
            rub = list(picked[0][0]["rubies"]) + [(a + off, b + off, r) for a, b, r in picked[1][0]["rubies"]]
            lines = wrap(lang, joined, rub if not transform else [], font, max_w)
        else:
            lines = []
            for s, t in picked:
                lines += wrap(lang, t, s["rubies"] if not transform else [], font, max_w)
        if len(lines) <= 3:
            return lines, [s["id"] for s, _ in picked], max_w
        if len(picked) > 1:
            picked = picked[:1]
        else:
            max_w *= 1.15
    return None


def wrap_width(rng, lang, font):
    if lang == "en":
        avg = font.getlength("the quick brown fox jumps over a lazy dog") / 41
        return avg * rng.uniform(34, 58)
    return font.getlength("あ") * rng.uniform(15, 27)


def make_image(ctx, rng, group, i, size, font_kind, layout, style, variant):
    """Render one image; returns (image, row) or None."""
    lang = ctx.lang
    spec = ctx.font_spec(rng, font_kind, pick(rng, ITALIC_FONTS) if variant == "italic" else None)
    font = load_font(spec, size)
    shear = float(rng.uniform(0.16, 0.24)) if variant == "oblique" else 0.0
    fringe = int(pick(rng, [-1, 1])) if variant == "fringe" else 0
    low_c = float(rng.uniform(1.8, 2.3)) if variant == "low-contrast" else None
    transform = str.upper if variant == "caps" else None
    params, name, ui, ids, ruby_pairs = {}, "", "", [], []
    if shear:
        params["shear"] = round(shear, 3)
    if fringe:
        params["fringe"] = fringe
    if layout.startswith("line"):
        s, text = ctx.sentence("plain" if transform else "short", spec, transform)
        if s is None:
            return None
        ids = [s["id"]]
        image, truth, style, p = compose_line(rng, lang, spec, size, style, layout, text, shear, fringe, low_c)
        params.update(p)
    else:
        mw = wrap_width(rng, lang, font)
        got = scene_dialogue(ctx, rng, font, spec, mw, transform=transform, ruby_first=variant == "ruby")
        if got is None:
            return None
        dialogue, ids, mw = got
        p_name = {"notebook": 0.85, "dark-window": 0.8, "framed": 0.75, "bare-outline": 0.0}[style]
        if rng.random() < p_name:
            name = ctx.name(spec) or ""
        if style == "notebook" and rng.random() < 0.7:
            ui = pick(rng, BUTTONS[lang])
            if not ctx.covered(spec, ui, "ui:" + ui):
                ui = ""
        image, truth, p = compose_scene(rng, lang, spec, size, style, dialogue, name, ui, shear=shear,
                                        fringe=fringe, ruby=variant == "ruby", low_contrast=low_c,
                                        translucent=variant == "translucent", wrap_w=mw)
        ruby_pairs = p.pop("rubyDrawn", [])
        params.update(p)
    if variant in ("jpeg", "blur"):
        image, p = degrade(image, rng, variant)
        params.update(p)
    file = f"{lang}-{group}-{i:03d}-{layout}-{style}-{variant}-{size}.png"
    row = dict(file=file, lang=lang, ocr=OCR[lang], truth="\n".join(truth), size=size, style=style, layout=layout,
               font=spec[0], variant=variant, split=ctx.split, lines=len(truth), group=group, fontFace=spec[1],
               fontWeight=spec[2], fontUnseen=spec[0] in FONTS[lang]["test-only"], name=name, ui=ui,
               ruby=ruby_pairs, sentences=ids, width=image.size[0], height=image.size[1], params=params)
    return image, row


def plan_group(rng, ctx, group):
    n = GROUP_SIZES[group]
    sizes = quota(rng, SIZES, n, SIZE_WEIGHTS)
    kinds = quota(rng, ["unseen", "seen"], n) if ctx.split == "test" else ["seen"] * n
    if group == "line":
        combos = quota(rng, [(lay, st) for st in LINE_STYLES for lay in ("line-tight", "line-loose")], n)
        jobs = [(lay, st, "clean") for lay, st in combos]
    elif group == "scene":
        jobs = [("scene", st, "clean") for st in quota(rng, SCENE_STYLES, n)]
    elif group == "special" and ctx.lang == "ja":
        jobs = [("scene", st, "ruby") for st in quota(rng, SCENE_STYLES, n)]
    elif group == "special":
        jobs = []
        for variant, count in (("italic", 12), ("oblique", 12), ("caps", 16)):
            for lay in quota(rng, ["line", "scene"], count):
                jobs.append((lay, variant))
        jobs = [jobs[k] for k in rng.permutation(len(jobs))]
        jobs = [(pick(rng, ["line-tight", "line-loose"]), pick(rng, LINE_STYLES), v) if lay == "line"
                else ("scene", pick(rng, SCENE_STYLES), v) for lay, v in jobs]
    else:  # degraded
        jobs = []
        for variant in DEGRADATIONS:
            lays = ["scene"] * 12 if variant == "translucent" else quota(rng, ["line", "scene"], 12)
            for lay in lays:
                if lay == "line":
                    styles = LINE_STYLES[:2] if variant == "low-contrast" else LINE_STYLES
                    jobs.append((pick(rng, ["line-tight", "line-loose"]), pick(rng, styles), variant))
                else:
                    styles = {"low-contrast": ["notebook", "dark-window", "framed"],
                              "translucent": ["dark-window", "dark-window", "framed"]}.get(variant, SCENE_STYLES)
                    jobs.append(("scene", pick(rng, styles), variant))
        jobs = [jobs[k] for k in rng.permutation(len(jobs))]
    if ctx.split == "test" and group == "special" and ctx.lang == "en":
        kinds = ["seen" if v == "italic" else k for (_, _, v), k in zip(jobs, kinds)]
    return [(sizes[k], kinds[k]) + jobs[k] for k in range(n)]


def generate(seed, split, lang, sentences, out_dir):
    ctx = Context(seed, split, lang, sentences)
    rows = []
    for gi, group in enumerate(GROUP_SIZES):
        plan = plan_group(np.random.default_rng([seed, SPLITS.index(split), LANGS.index(lang), 500 + gi]), ctx, group)
        for i, (size, kind, layout, style, variant) in enumerate(plan):
            rng = np.random.default_rng([seed, SPLITS.index(split), LANGS.index(lang), gi, i])
            got = None
            for attempt in range(5):  # a font that cannot draw any candidate text is replaced
                got = make_image(ctx, rng, group, i, size, kind, layout, style, variant)
                if got is not None:
                    break
            if got is None:
                raise RuntimeError(f"no drawable text for {split}/{lang}/{group}/{i}")
            image, row = got
            image.save(out_dir / row["file"], optimize=False)
            rows.append(row)
    return rows, ctx.skips


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=Path("build/enja-benchmark"),
                        help="root folder; images go to <output>/dev and <output>/test")
    parser.add_argument("--split", choices=["dev", "test", "both"], default="both")
    parser.add_argument("--lang", choices=["en", "ja", "both"], default="both",
                        help="for quick iterations; a partial run writes a manifest with that language only")
    parser.add_argument("--seed", type=int, default=20260930)
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    sentences = sentence_table()
    splits = SPLITS if args.split == "both" else [args.split]
    langs = LANGS if args.lang == "both" else [args.lang]
    for split in splits:
        out_dir = args.output / split
        out_dir.mkdir(parents=True, exist_ok=True)
        rows, skips = [], []
        for lang in langs:
            r, s = generate(args.seed, split, lang, sentences, out_dir)
            rows += r
            skips += s
        keep = {r["file"] for r in rows}
        pattern = re.compile(r"^(%s)-(line|scene|special|degraded)-\d{3}-.*\.png$" % "|".join(langs))
        stale = [p for p in out_dir.iterdir() if pattern.match(p.name) and p.name not in keep]
        for p in stale:
            p.unlink()
        (out_dir / "manifest.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        counts = defaultdict(Counter)
        for r in rows:
            counts[r["lang"]][f"{r['group']}/{r['layout']}/{r['variant']}"] += 1
        skip_fonts = Counter(f"{s['font']}#{s['face']}" for s in skips)
        report = dict(
            split=split, images=len(rows), seed=args.seed, command=" ".join(sys.argv), staleRemoved=len(stale),
            sentences={lang: len(sentences[(lang, split)]) for lang in langs},
            counts={lang: dict(sorted(c.items())) for lang, c in counts.items()},
            groups={lang: dict(Counter(r["group"] for r in rows if r["lang"] == lang)) for lang in langs},
            sizes={lang: dict(sorted(Counter(r["size"] for r in rows if r["lang"] == lang).items())) for lang in langs},
            fonts={lang: dict(sorted(Counter(r["font"] for r in rows if r["lang"] == lang).items())) for lang in langs},
            fontUnseenImages={lang: sum(r["fontUnseen"] for r in rows if r["lang"] == lang) for lang in langs},
            skippedCombinations=len(skips), skippedByFont=dict(sorted(skip_fonts.items())), skipped=skips,
            glyphCheck="fontTools cmap" if TTFont is not None else "Pillow .notdef comparison",
            versions=dict(python=platform.python_version(), pillow=PIL.__version__, numpy=np.__version__,
                          freetype=features.version("freetype2"), raqm=features.version("raqm"),
                          layout="raqm" if LAYOUT_ENGINE == ImageFont.Layout.RAQM else "basic"))
        (args.output / f"generation-report-{split}.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"{split}: {len(rows)} images in {out_dir} ({len(skips)} font/text combinations skipped, "
              f"{len(stale)} stale files removed)")
        for lang in langs:
            print(" ", lang, json.dumps(report["groups"][lang]))


if __name__ == "__main__":
    main()
