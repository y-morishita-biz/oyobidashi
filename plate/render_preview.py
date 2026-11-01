#!/usr/bin/env python3
# make_plate.py の形を描いて、プレビュー画像を書き出す。
#   plate/out/preview_front_A.png / _B.png / _C.png  レイアウト3案の正面図
#   plate/out/preview_side.png                         横から見た図（レイアウト B、側板・M5Dial・バッテリー付き）
# 組み立てた状態は docs/plate-design.html の 3D ビューアで見る（make_viewer_data.py）。
#
#   ~/.platformio/penv/bin/python plate/render_preview.py
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import PolyCollection

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_plate as mp  # noqa: E402
from build123d import Pos  # noqa: E402

# 造形の色（手持ちのブランドカラー。正確な色はスライサーで割り当てる）
YELLOW = "#F7C600"
BLACK = "#1C1C1C"
GRAY = "#8E9094"
DEVICE = "#2B2B2B"
SCREEN = "#FF5A1F"
BATTERY = "#3B3F45"


def mesh(shape, tol=0.05):
    verts, tris = shape.tessellate(tol, 0.2)
    v = np.array([(p.X, p.Y, p.Z) for p in verts])
    return v[np.array(tris)]


def front_view(name, path):
    base, raised, dev = mp.make_plate(name)
    fig, ax = plt.subplots(figsize=(10, 6.8), dpi=140)
    for shape, color in ((base, YELLOW), (raised, BLACK)):
        tri = mesh(shape, 0.02)
        top = tri[np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])[:, 2] > 1e-9]
        ax.add_collection(PolyCollection(top[:, :, :2], facecolors=color, edgecolors=color, linewidths=0.05))
    # 側板の溝に入る左右の端
    for x in (0, mp.W - mp.EDGE):
        ax.add_patch(plt.Rectangle((x, 0), mp.EDGE, mp.H, color=GRAY, alpha=0.55, lw=0))
    cx, cy = dev
    ax.add_patch(plt.Circle((cx, cy), mp.DEV_KNOB_R, color=DEVICE))
    ax.add_patch(plt.Circle((cx, cy), 16.2, color=SCREEN))
    ax.text(cx, cy, "M5Dial", ha="center", va="center", color="black", fontsize=9)
    ax.set_xlim(-5, mp.W + 5)
    ax.set_ylim(-5, mp.H + 5)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.savefig(path, bbox_inches="tight", facecolor="white", pad_inches=0.05)
    plt.close(fig)


def side_view(path):
    """真横からの図。側面の形だけを描く（matplotlib の 3D は前後の重なりが崩れるため 2D で）"""
    _, P, c, s = mp._frame_axes()
    fig, ax = plt.subplots(figsize=(8, 4.5), dpi=140)
    ax.axhline(0, color="#999", lw=1)
    # 側板
    top, bottom = mp.T + mp.GROOVE_CLEAR + mp.LIP, -mp.GROOVE_CLEAR - mp.LIP
    pts = [(P(-mp.LIP, bottom)[0] - 4, 0), P(-mp.LIP, top), P(mp.H + 2, top), P(mp.H + 2, bottom), (P(mp.H + 2, bottom)[0], 0)]
    ax.add_patch(plt.Polygon(pts, color=GRAY, alpha=0.6))
    # プレート
    ax.add_patch(plt.Polygon([P(0, 0), P(mp.H, 0), P(mp.H, mp.T), P(0, mp.T)], color=YELLOW))
    # M5Dial（レイアウト B の位置）
    dev = mp.layout("B")[0]
    y = dev[1]
    ax.add_patch(plt.Polygon([P(y - 24.85, mp.T + 0.6), P(y + 24.85, mp.T + 0.6), P(y + 24.85, mp.T + 13.8), P(y - 24.85, mp.T + 13.8)], color=DEVICE))
    ax.add_patch(plt.Polygon([P(y - 22, mp.T - 16.5), P(y + 22, mp.T - 16.5), P(y + 22, 0), P(y - 22, 0)], color=DEVICE, alpha=0.85))
    ax.add_patch(plt.Polygon([P(y - 25.4, -5), P(y + 25.4, -5), P(y + 25.4, 0), P(y - 25.4, 0)], color="#555"))
    # USB-C のプラグ（真下へ。おおよそ 12 x 6.5 x 25 mm）
    ax.add_patch(plt.Polygon([P(y - 22 - 25, -7), P(y - 22, -7), P(y - 22, -12), P(y - 22 - 25, -12)], color="#777"))
    # バッテリー
    ax.add_patch(plt.Polygon([P(mp.H - 68, 0), P(mp.H - 8, 0), P(mp.H - 8, -22), P(mp.H - 68, -22)], color=BATTERY, alpha=0.9))
    ax.text(P(mp.H, mp.T)[0] + 2, P(mp.H, mp.T)[1], f"{mp.TILT:.0f}°", color="#C8430E")
    ax.set_xlim(-10, 130)
    ax.set_ylim(-5, 75)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.savefig(path, bbox_inches="tight", facecolor="white", pad_inches=0.05)
    plt.close(fig)


def assembled_parts(name="B"):
    """組み立てた状態の部品（world：X 右、Y 奥、Z 上）と名前・色"""
    base, raised, dev = mp.make_plate(name)
    loc = mp.plate_location()
    xl, xr = mp.frame_positions()
    parts = [
        ("plate", YELLOW, loc * base), ("raised", BLACK, loc * raised),
        ("frame", GRAY, Pos(xl, 0, 0) * mp.make_frame(False)), ("frame", GRAY, Pos(xr, 0, 0) * mp.make_frame(True)),
    ]
    colors = {"device": DEVICE, "nut": "#555555", "screen": SCREEN, "battery": BATTERY}
    for key, shapes in mp.dummies(dev).items():
        for sh in shapes:
            parts.append((key, colors[key], loc * sh))
    return parts


def main():
    os.makedirs(mp.OUT, exist_ok=True)
    for name in ("A", "B", "C"):
        front_view(name, os.path.join(mp.OUT, f"preview_front_{name}.png"))
    side_view(os.path.join(mp.OUT, "preview_side.png"))
    old = os.path.join(mp.OUT, "preview_front.png")
    if os.path.exists(old):
        os.remove(old)
    print("wrote preview_front_A/B/C, preview_side")


if __name__ == "__main__":
    main()
