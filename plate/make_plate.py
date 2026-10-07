#!/usr/bin/env python3
# oyobidashi の M5Dial を埋め込むプレート（B 横長 170 x 110 mm）の 3D モデルを作り、STEP / STL を書き出す。
#
#   ~/.platformio/penv/bin/python plate/make_plate.py
#
# 出力（plate/out/）
#   plate_base.stl    プレートの地（1色目）。表を上にして造形する
#   plate_raised.stl  盛り上げた文字・装飾（2色目）。Bambu Studio で plate_base と一緒に
#                     「1つのオブジェクトの複数パーツ」として読み込み、色を割り当てる
#   clamp_ring.stl    裏から M5Dial を押さえるリング（M3 ねじ 3本）
#   foot.stl          プレートの下の辺を差し込む足（2個造形する）
#   oyobidashi_plate.step  上の部品をまとめた STEP（組み立てた状態）
#
# 寸法の根拠（docs/plate-design.html も参照）
#   M5Dial：公式データ Dial.stl の断面から。前面の回るリング Ø49.7、その後ろの回らないフランジ Ø50.9
#   （奥行 4.5 mm）、背面の本体 Ø44。USB-C は背面の本体の真下、フランジの後ろ 2〜6 mm
#   バッテリー：Anker PowerCore 10000（92 x 60 x 22 mm）を背面に両面テープで貼る
#
# 必要なもの：build123d（pip install build123d）、Noto Sans JP（~/Library/Fonts）
import math
import os

from build123d import (
    Align, Box, BuildPart, Circle, Compound, Cylinder, Location, Plane, Polygon, Pos, Rectangle,
    RectangleRounded, Rot, Text, export_step, export_stl, extrude,
)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
FONT_B = os.path.expanduser("~/Library/Fonts/NotoSansJP-Bold.otf")
FONT_R = os.path.expanduser("~/Library/Fonts/NotoSansJP-Regular.otf")

# --- プレート ----------------------------------------------------------------
W, H, CORNER = 170.0, 110.0, 8.0
T = 4.7              # 厚さ。M5Dial のフランジ（4.5 mm）が裏へ 0.3 mm 出て、押さえのリングで締まる
RAISE = 0.8          # 文字・装飾の高さ（0.2 mm で 4 層）
DEV = (42.0, 58.0)   # M5Dial の中心（プレートの左下が原点）
HOLE_D = 51.5        # フランジ Ø50.9 + すき間
SCREW_R = 29.5       # 押さえのリングのねじの位置（M5Dial の中心から）
SCREW_ANGLES = (90, 210, 330)  # 真下（270°）は USB-C のために空ける
PILOT_D = 2.6        # M3 ねじを直接ねじ込む下穴
PILOT_DEPTH = T - 0.8

# --- 押さえのリング ------------------------------------------------------------
RING_IN = 22.4       # 背面の本体 Ø44 + すき間
RING_OUT = 33.0
RING_T = 3.0
RING_HOLE_D = 3.4
USB_SLOT_W = 18.0    # USB-C のプラグとケーブルを逃がす切り欠き

# --- 足 ---------------------------------------------------------------------
TILT = 18.0          # 後ろへの傾き（度）
FOOT_W = 20.0
FOOT_X = (12.0, 138.0)  # 左右の足の左端（プレートの x）
SLOT_CLEAR = 0.3
SLOT_BOTTOM = 4.0    # 溝の底（プレートの下の辺）の、足の底からの高さ（プレートに沿って）
FOOT_BACK = 80.0     # 足が後ろへ伸びる長さ
FOOT_BASE = 4.0


def text(s, x, y, size, font=FONT_B, align=Align.MIN):
    return Pos(x, y) * Text(s, font_size=size, font_path=font, align=(align, Align.CENTER))


# 5x7 のドット文字（M5Dial の画面と同じデータ）
GLYPHS = {
    "A": [14, 17, 17, 31, 17, 17, 17], "C": [14, 17, 16, 16, 16, 17, 14], "E": [31, 16, 16, 30, 16, 16, 31],
    "K": [17, 18, 20, 24, 20, 18, 17], "L": [16, 16, 16, 16, 16, 16, 31], "M": [17, 27, 21, 21, 17, 17, 17],
    "R": [30, 17, 17, 30, 20, 18, 17], " ": [0] * 7,
}


def dot_word(word, x0, cy, pitch, dot_d):
    shapes = []
    for i, ch in enumerate(word):
        for row, bits in enumerate(GLYPHS[ch]):
            for col in range(5):
                if (bits >> (4 - col)) & 1:
                    shapes.append(Pos(x0 + (i * 6 + col) * pitch, cy + (3 - row) * pitch) * Circle(dot_d / 2))
    return shapes


def arc_arrow(cx, cy, r, width, a_from, a_to):
    """M5Dial のまわりの時計回りの矢印（角度は度、x 軸から反時計回り。a_from > a_to で時計回り）"""
    band = Pos(cx, cy) * (Circle(r + width / 2) - Circle(r - width / 2))
    pts = [(cx, cy)]
    n = 24
    for k in range(n + 1):
        a = math.radians(a_from + (a_to - a_from) * k / n)
        pts.append((cx + (r + 5) * math.cos(a), cy + (r + 5) * math.sin(a)))
    arc = band & Polygon(*pts, align=None)
    # 矢じり：弧の終わりから、時計回りの向きへ
    a1 = math.radians(a_to)
    tip = math.radians(a_to - 9)
    head = Polygon(
        (cx + (r - 3.2) * math.cos(a1), cy + (r - 3.2) * math.sin(a1)),
        (cx + (r + 3.2) * math.cos(a1), cy + (r + 3.2) * math.sin(a1)),
        (cx + r * math.cos(tip), cy + r * math.sin(tip)),
        align=None,
    )
    return [arc, head]


def step_badge(n, x, y, r):
    return Pos(x, y) * Circle(r) - Pos(x, y) * Text(str(n), font_size=r * 1.45, font_path=FONT_B,
                                                    align=(Align.CENTER, Align.CENTER))


def make_plate():
    base = extrude(Pos(W / 2, H / 2) * RectangleRounded(W, H, CORNER), amount=T)
    base -= Pos(DEV[0], DEV[1], 0) * Cylinder(HOLE_D / 2, T, align=(Align.CENTER, Align.CENTER, Align.MIN))
    for a in SCREW_ANGLES:
        x = DEV[0] + SCREW_R * math.cos(math.radians(a))
        y = DEV[1] + SCREW_R * math.sin(math.radians(a))
        base -= Pos(x, y, 0) * Cylinder(PILOT_D / 2, PILOT_DEPTH, align=(Align.CENTER, Align.CENTER, Align.MIN))

    col = 78.0  # 右の列の左端
    faces = []
    # 見出し（ドット）
    faces += dot_word("MAKER CALL", col + 0.6, 94.0, 1.42, 1.25)
    # 一文
    faces.append(text("制作者が席を外しているときは、", col, 79.5, 5.0))
    faces.append(text("画面にタッチしてください", col, 72.5, 5.0))
    faces.append(text("Tap the screen to call the maker", col, 66.0, 3.5, FONT_R))
    # 3ステップ（縦に3行）
    steps = [("タッチ", "Tap"), ("制作者の iPhone に届く", "We get notified"), ("画面でお知らせ", "Check the screen")]
    for i, (ja, en) in enumerate(steps):
        y = 53.5 - i * 12.0
        faces.append(step_badge(i + 1, col + 3.4, y - 1.6, 3.4))
        faces.append(text(ja, col + 9.5, y, 4.5))
        faces.append(text(en, col + 9.5, y - 5.0, 3.0, FONT_R))
    # ダイヤルのサイン（M5Dial の左側を上へ、時計回り）
    faces += arc_arrow(DEV[0], DEV[1], 31.5, 1.8, 215, 140)
    faces.append(text("回すと製品が見られます", DEV[0], 19.5, 4.2, align=Align.CENTER))
    faces.append(text("Turn to browse", DEV[0], 14.0, 3.2, FONT_R, align=Align.CENTER))
    # ブランド
    faces.append(text("dotting dots", W - 9.0, 8.0, 3.6, FONT_R, align=Align.MAX))

    raised = extrude(Plane.XY.offset(T) * Compound(faces), amount=RAISE, dir=(0, 0, 1))
    return base, raised


def make_clamp_ring():
    ring = Cylinder(RING_OUT, RING_T, align=(Align.CENTER, Align.CENTER, Align.MIN))
    ring -= Cylinder(RING_IN, RING_T, align=(Align.CENTER, Align.CENTER, Align.MIN))
    ring -= Pos(0, -(RING_IN + RING_OUT) / 2, 0) * Box(USB_SLOT_W, RING_OUT - RING_IN + 2, RING_T,
                                                       align=(Align.CENTER, Align.CENTER, Align.MIN))
    for a in SCREW_ANGLES:
        x, y = SCREW_R * math.cos(math.radians(a)), SCREW_R * math.sin(math.radians(a))
        ring -= Pos(x, y, 0) * Cylinder(RING_HOLE_D / 2, RING_T, align=(Align.CENTER, Align.CENTER, Align.MIN))
    return ring


def make_foot():
    """側面の形（u = 後ろ向き, v = 上向き）を、幅 FOOT_W だけ押し出す"""
    c, s = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))

    def P(sv, tv):  # プレートの厚み方向 sv（前面 = 0、後ろへ +）、高さ方向 tv から (u, v) へ
        return (sv * c + tv * s, -sv * s + tv * c)

    slot_t = T + SLOT_CLEAR

    def prism(*pts):  # 側面の多角形を、幅 FOOT_W の立体にする
        return extrude(Plane.YZ * Polygon(*pts, align=None), amount=FOOT_W, dir=(1, 0, 0))

    block = prism(P(-4, -12), P(slot_t + 5, -12), P(slot_t + 5, 22), P(-4, 14))
    slab = prism((-12, 0), (FOOT_BACK, 0), (FOOT_BACK, FOOT_BASE), (-12, FOOT_BASE))
    brace = prism(P(slot_t + 5, 16), (FOOT_BACK, FOOT_BASE), P(slot_t + 5, 0))
    slot = prism(P(0, SLOT_BOTTOM), P(slot_t, SLOT_BOTTOM), P(slot_t, 200), P(0, 200))
    floor = prism((-40, 0), (200, 0), (200, 200), (-40, 200))
    foot = ((block + slab + brace) - slot) & floor
    return foot.clean()


def assembly(base, raised, ring, foot):
    """組み立てた状態（world：X 右、Y 奥、Z 上）"""
    c, s = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))
    # プレートの (0,0,0)（裏の左下）は、溝の底で厚み方向 sv = T の位置
    origin = (0, T * c + SLOT_BOTTOM * s, -T * s + SLOT_BOTTOM * c)
    plate_frame = Plane(origin=origin, x_dir=(1, 0, 0), z_dir=(0, -c, s))
    loc = Location(plate_frame)
    ring_loc = loc * Location((DEV[0], DEV[1], -0.3 - RING_T))
    parts = [loc * base, loc * raised, ring_loc * ring]
    for fx in FOOT_X:
        parts.append(Pos(fx, 0, 0) * foot)
    return Compound(parts), loc


def main():
    os.makedirs(OUT, exist_ok=True)
    base, raised = make_plate()
    ring = make_clamp_ring()
    foot = make_foot()
    for name, shape in (("plate_base", base), ("plate_raised", raised), ("clamp_ring", ring), ("foot", foot)):
        path = os.path.join(OUT, f"{name}.stl")
        export_stl(shape, path, tolerance=0.05, angular_tolerance=0.2)
        bb = shape.bounding_box()
        print(f"{name:13s} {bb.size.X:6.1f} x {bb.size.Y:6.1f} x {bb.size.Z:5.1f} mm  -> {os.path.relpath(path)}")
    asm, _ = assembly(base, raised, ring, foot)
    export_step(asm, os.path.join(OUT, "oyobidashi_plate.step"))
    print("step          ->", os.path.relpath(os.path.join(OUT, "oyobidashi_plate.step")))


if __name__ == "__main__":
    main()
