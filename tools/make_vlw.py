#!/usr/bin/env python3
# OTF / TTF フォントから、ファームウェアで使う文字だけを取り出して VLW（M5GFX のなめらかフォント）にし、
# C のヘッダーファイルに書き出す。
#
#   ~/.platformio/penv/bin/python tools/make_vlw.py \
#       --src call-button/src/main.cpp --out call-button/src/fonts_noto.h \
#       --font NOTO_BOLD_20=~/Library/Fonts/NotoSansJP-Bold.otf:20 \
#       --font NOTO_REGULAR_13=~/Library/Fonts/NotoSansJP-Regular.otf:13
#
# 文字は「--src のファイルにある "..." の中の文字」と ASCII（0x20〜0x7E）。
# 画面の文言を変えたら、このスクリプトを実行し直す。freetype-py が必要（pip install freetype-py）。
import argparse
import os
import re
import struct
import sys

import freetype

VLW_VERSION = 11


def collect_chars(paths):
    chars = {chr(c) for c in range(0x20, 0x7F)}
    for path in paths:
        with open(path, encoding="utf-8") as f:
            src = f.read()
        for lit in re.findall(r'"((?:[^"\\\n]|\\.)*)"', src):
            chars.update(lit)
    # BMP の外（絵文字など）は VLW に入らないので除く
    return sorted(c for c in chars if 0x20 <= ord(c) <= 0xFFFF and c.isprintable())


def make_vlw(font_path, px, chars):
    face = freetype.Face(font_path)
    face.set_pixel_sizes(0, px)
    glyphs = []
    missing = []
    for ch in chars:
        if ch != " " and face.get_char_index(ch) == 0:
            missing.append(ch)
            continue
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        g = face.glyph
        bm = g.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        buf = bytes(bm.buffer)
        data = b"".join(buf[r * pitch : r * pitch + w] for r in range(h))
        advance = (g.advance.x + 32) >> 6
        glyphs.append((ord(ch), h, w, advance, g.bitmap_top, g.bitmap_left, data))
    if missing:
        print(f"  warning: {os.path.basename(font_path)} has no glyph for {''.join(missing)!r}", file=sys.stderr)

    ascent = face.size.ascender >> 6
    descent = -(face.size.descender >> 6)
    out = struct.pack(">6i", len(glyphs), VLW_VERSION, px, 0, ascent, descent)
    out += b"".join(struct.pack(">7i", u, h, w, adv, top, left, 0) for u, h, w, adv, top, left, _ in glyphs)
    out += b"".join(g[6] for g in glyphs)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", action="append", required=True, help="文字を集めるソースファイル")
    ap.add_argument("--out", required=True, help="書き出すヘッダーファイル")
    ap.add_argument("--font", action="append", required=True, help="NAME=フォントのパス:ピクセル数")
    args = ap.parse_args()

    chars = collect_chars(args.src)
    lines = [
        "// tools/make_vlw.py で生成したなめらかフォント（VLW）。手で編集しない。",
        "// フォント：Noto Sans JP（SIL Open Font License 1.1）",
        f"// 文字数：{len(chars)}（{''.join(c for c in chars if ord(c) > 0x7E)}）",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
    ]
    for spec in args.font:
        name, rest = spec.split("=", 1)
        path, px = rest.rsplit(":", 1)
        data = make_vlw(os.path.expanduser(path), int(px), chars)
        print(f"{name}: {len(data)} bytes")
        lines.append(f"// {os.path.basename(path)} {px}px")
        lines.append(f"static const uint8_t {name}[] PROGMEM = {{")
        for i in range(0, len(data), 24):
            lines.append("  " + ",".join(str(b) for b in data[i : i + 24]) + ",")
        lines.append("};")
        lines.append("")
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
