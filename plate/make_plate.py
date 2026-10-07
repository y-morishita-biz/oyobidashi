#!/usr/bin/env python3
# oyobidashi の M5Dial を埋め込むプレート（横長 170 x 110 mm）の 3D モデルを作り、STEP / STL を書き出す。
#
#   ~/.platformio/penv/bin/python plate/make_plate.py            # レイアウト B（おすすめ）
#   ~/.platformio/penv/bin/python plate/make_plate.py --layout A
#
# 出力（plate/out/）
#   plate_base.stl     プレートの地（1色目）。表を上にして造形する
#   plate_raised.stl   盛り上げた文字・装飾（2色目）。Bambu Studio で plate_base と一緒に
#                      「1つのオブジェクトの複数パーツ」として読み込み、色を割り当てる
#   frame_left.stl     プレートを地面から 20° に支える左の側板（外側の面を下にして造形）
#   frame_right.stl    右の側板
#   fit_test.stl       穴とネジのリングのはまり具合を確かめる、60 mm 角の試し板
#   oyobidashi_plate.step  上の部品をまとめた STEP（組み立てた状態）
#
# 固定のしかた
#   M5Dial の背面にはネジ（外径 Ø44）が切ってあり、付属のリング（外径 Ø50.8・高さ 5 mm）がナットになる。
#   プレートの穴に前から差し込み、裏からリングを締めて挟む。
#   USB-C は背面の本体の真下（リングの後ろ側）にあるので、プレートが厚いとリングが差し込み口をふさぐ。
#   公式データから、挟める厚さはおよそ 3.5 mm まで。
#
# 寸法の根拠：公式データ Dial.stl の断面（docs/plate-design.html も参照）
# 必要なもの：build123d（pip install build123d）、Noto Sans JP（~/Library/Fonts）
import argparse
import math
import os

from build123d import (
    Align, Box, Circle, Compound, Cylinder, Location, Plane, Polygon, Pos, RectangleRounded, Rot, Text,
    export_step, export_stl, extrude,
)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
FONT_B = os.path.expanduser("~/Library/Fonts/NotoSansJP-Bold.otf")

# --- プレート ----------------------------------------------------------------
W, H, CORNER = 170.0, 110.0, 6.0
T = 3.0              # 厚さ（M5Dial のネジとリングで挟める厚さに合わせる）
RAISE = 0.8          # 文字・装飾の高さ（0.2 mm で 4 層）
HOLE_D = 44.6        # M5Dial のネジ（外径 Ø44）+ すき間
EDGE = 4.0           # 左右の端のうち、側板の溝に入る幅（ここには文字を置かない）

# --- 側板（スタンド） -----------------------------------------------------------
TILT = 20.0          # 地面からの角度（度）
FRONT_LIFT = 20.0    # プレートの前の端（裏側）の、机からの高さ。裏の M5Dial・USB-C・バッテリーの逃げ
FRONT_SET = 10.0     # プレートの前の端の、側板の前の端からの奥行き
FRAME_T = 8.0        # 側板の厚さ
GROOVE_DEPTH = EDGE + 0.2
GROOVE_CLEAR = 0.15  # 溝のすき間（片側）
LIP = 3.0            # 溝の上下の縁の厚さ

# --- 試し板 -------------------------------------------------------------------
FIT_SIZE = 60.0

# --- M5Dial（確認用の仮の形） -----------------------------------------------------
DEV_KNOB_R = 24.85   # 前面の回るリング
DEV_REAR_R = 22.0    # 背面のネジ
NUT_OUT_R, NUT_IN_R, NUT_H = 25.4, 21.3, 5.0


def text(s, x, y, size, align=Align.MIN):
    return Pos(x, y) * Text(s, font_size=size, font_path=FONT_B, align=(align, Align.CENTER))


# 5x7 のドット文字（M5Dial の画面と同じデータ）
GLYPHS = {
    "A": [14, 17, 17, 31, 17, 17, 17], "C": [14, 17, 16, 16, 16, 17, 14], "E": [31, 16, 16, 30, 16, 16, 31],
    "K": [17, 18, 20, 24, 20, 18, 17], "L": [16, 16, 16, 16, 16, 16, 31], "M": [17, 27, 21, 21, 17, 17, 17],
    "R": [30, 17, 17, 30, 20, 18, 17], " ": [0] * 7,
}


def dot_word(word, x, cy, pitch, align="start"):
    cols = len(word) * 6 - 1
    x0 = x - (cols - 1) * pitch / 2 if align == "center" else x
    shapes = []
    for i, ch in enumerate(word):
        for row, bits in enumerate(GLYPHS[ch]):
            for col in range(5):
                if (bits >> (4 - col)) & 1:
                    shapes.append(Pos(x0 + (i * 6 + col) * pitch, cy + (3 - row) * pitch) * Circle(pitch * 0.44))
    return shapes


def step_badge(n, x, y, r):
    return Pos(x, y) * Circle(r) - Pos(x, y) * Text(str(n), font_size=r * 1.45, font_path=FONT_B,
                                                    align=(Align.CENTER, Align.CENTER))


STEPS = ["タッチ", "制作者の iPhone に届く", "画面でお知らせ"]
SENTENCE = ("制作者が席を外しているときは、", "画面にタッチしてください")


def steps_column(x, y_top, pitch, size):
    faces = []
    for i, ja in enumerate(STEPS):
        y = y_top - i * pitch
        faces.append(step_badge(i + 1, x + size * 0.75, y, size * 0.75))
        faces.append(text(ja, x + size * 2.0, y, size))
    return faces


def layout(name):
    """レイアウトごとの、M5Dial の中心と盛り上げる形"""
    if name == "A":  # 左にダイヤル、右に説明
        dev = (42.0, 55.0)
        col = 80.0
        faces = dot_word("MAKER CALL", col, 92.0, 1.4)
        faces += [text(SENTENCE[0], col, 72.0, 5.4), text(SENTENCE[1], col, 64.0, 5.4)]
        faces += steps_column(col, 45.0, 12.0, 5.2)
    elif name == "B":  # 上に見出しと一文、下にダイヤルと3ステップ
        dev = (44.0, 36.0)
        faces = dot_word("MAKER CALL", W / 2, 92.0, 2.2, "center")
        faces.append(text(SENTENCE[0] + SENTENCE[1], W / 2, 74.0, 5.0, Align.CENTER))
        faces += steps_column(84.0, 52.0, 14.0, 5.6)
    elif name == "C":  # 右にダイヤル、左に説明（右手で触りやすい）
        dev = (128.0, 55.0)
        col = EDGE + 8.0
        faces = dot_word("MAKER CALL", col, 92.0, 1.4)
        faces += [text(SENTENCE[0], col, 72.0, 5.4), text(SENTENCE[1], col, 64.0, 5.4)]
        faces += steps_column(col, 45.0, 12.0, 5.2)
    else:
        raise ValueError(name)
    return dev, faces


def make_plate(name):
    dev, faces = layout(name)
    base = extrude(Pos(W / 2, H / 2) * RectangleRounded(W, H, CORNER), amount=T)
    base -= Pos(dev[0], dev[1], 0) * Cylinder(HOLE_D / 2, T, align=(Align.CENTER, Align.CENTER, Align.MIN))
    raised = extrude(Plane.XY.offset(T) * Compound(faces), amount=RAISE, dir=(0, 0, 1))
    return base, raised, dev


def make_fit_test():
    plate = extrude(RectangleRounded(FIT_SIZE, FIT_SIZE, 4), amount=T)
    return plate - Cylinder(HOLE_D / 2, T, align=(Align.CENTER, Align.CENTER, Align.MIN))


# --- 側板 ---------------------------------------------------------------------
# 側面の座標：u = 奥（world の Y）、v = 上（world の Z）。
# プレートの裏の前の端を O とし、プレートに沿って奥へ d、プレートの表の向きに n。
def _frame_axes():
    c, s = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))
    O = (FRONT_SET, FRONT_LIFT)

    def P(sv, qv):  # プレートに沿って sv、厚み方向に qv
        return (O[0] + sv * c - qv * s, O[1] + sv * s + qv * c)

    return O, P, c, s


def make_frame(right=False):
    O, P, c, s = _frame_axes()
    top = T + GROOVE_CLEAR + LIP
    bottom = -GROOVE_CLEAR - LIP
    a, b, e = P(-LIP, top), P(H + 2, top), P(H + 2, bottom)
    profile = Polygon((P(-LIP, bottom)[0] - 4, 0), a, b, e, (e[0], 0), align=None)
    # 材料を減らす窓（溝より下）
    w0, w1 = P(14, bottom - 4), P(H - 14, bottom - 4)
    win = Polygon(w0, w1, (w1[0], 7), (w0[0], 7), align=None)
    body = extrude(Plane.YZ * profile, amount=FRAME_T, dir=(1, 0, 0))
    body -= extrude(Plane.YZ * win, amount=FRAME_T, dir=(1, 0, 0))
    # 溝：下の端は閉じ（プレートが止まる）、上の端は開いている（差し込む）
    groove = Polygon(P(0, -GROOVE_CLEAR), P(H + 30, -GROOVE_CLEAR), P(H + 30, T + GROOVE_CLEAR),
                     P(0, T + GROOVE_CLEAR), align=None)
    g = extrude(Plane.YZ * groove, amount=GROOVE_DEPTH, dir=(1, 0, 0))
    if right:  # 右の側板は内側が -X（x = 0）
        body -= g
    else:      # 左の側板は内側が +X（x = FRAME_T）
        body -= Pos(FRAME_T - GROOVE_DEPTH, 0, 0) * g
    return body.clean()


def for_printing(frame, right):
    """側板を、外側の面を下（溝を上）にして造形する向きにする"""
    shape = Rot(0, 90 if right else -90, 0) * frame
    return Pos(0, 0, -shape.bounding_box().min.Z) * shape


def plate_location():
    O, _, c, s = _frame_axes()
    return Location(Plane(origin=(0, O[0], O[1]), x_dir=(1, 0, 0), z_dir=(0, -s, c)))


def frame_positions():
    """側板を置く X。プレートの端の EDGE だけが溝に入る"""
    return (EDGE - FRAME_T, W - EDGE)


def dummies(dev):
    """確認用の M5Dial・付属のリング・バッテリー（プレートの座標）"""
    cx, cy = dev
    knob_back = T + 0.6
    c = (Align.CENTER, Align.CENTER, Align.MIN)
    return {
        "device": [
            Pos(cx, cy, T - 16.5) * Cylinder(DEV_REAR_R, 16.5, align=c),
            Pos(cx, cy, knob_back) * Cylinder(DEV_KNOB_R, 13.2, align=c),
        ],
        "nut": [Pos(cx, cy, -NUT_H) * (Cylinder(NUT_OUT_R, NUT_H, align=c) - Cylinder(NUT_IN_R, NUT_H, align=c))],
        "screen": [Pos(cx, cy, knob_back + 13.2) * Cylinder(16.2, 0.3, align=c)],
        "battery": [Pos(W - EDGE - 2 - 92, H - 8 - 60, -22) * Box(92, 60, 22, align=(Align.MIN, Align.MIN, Align.MIN))],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--layout", default="B", choices=["A", "B", "C"])
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for old in ("clamp_ring.stl", "foot.stl"):  # 以前の部品（押さえのリングと足）は使わない
        p = os.path.join(OUT, old)
        if os.path.exists(p):
            os.remove(p)
    base, raised, _ = make_plate(args.layout)
    shapes = {
        "plate_base": base, "plate_raised": raised,
        "frame_left": make_frame(False), "frame_right": make_frame(True), "fit_test": make_fit_test(),
    }
    for name, shape in shapes.items():
        if name.startswith("frame"):
            shape = for_printing(shape, name.endswith("right"))
        export_stl(shape, os.path.join(OUT, f"{name}.stl"), tolerance=0.05, angular_tolerance=0.2)
        bb = shape.bounding_box()
        print(f"{name:13s} {bb.size.X:6.1f} x {bb.size.Y:6.1f} x {bb.size.Z:5.1f} mm")
    loc = plate_location()
    xl, xr = frame_positions()
    asm = Compound([loc * base, loc * raised, Pos(xl, 0, 0) * shapes["frame_left"],
                    Pos(xr, 0, 0) * shapes["frame_right"]])
    export_step(asm, os.path.join(OUT, "oyobidashi_plate.step"))
    print(f"layout {args.layout} -> plate/out/")


if __name__ == "__main__":
    main()
