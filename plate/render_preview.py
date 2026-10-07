#!/usr/bin/env python3
# make_plate.py で作る部品を色付きで描き、プレビュー画像（plate/out/preview_*.png）を書き出す。
# 組み立て図には、確認用に M5Dial とバッテリーの仮の形（Dial.stl の寸法から）も置く。
#
#   ~/.platformio/penv/bin/python plate/render_preview.py
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import PolyCollection
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_plate as mp  # noqa: E402
from build123d import Align, Box, Cylinder, Location, Pos  # noqa: E402

# 造形の色（仮。手持ちのブランドカラーに合わせて Bambu Studio で割り当てる）
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


def shade(tri, color, light=(0.35, -0.6, 0.72)):
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
    l = np.array(light) / np.linalg.norm(light)
    k = 0.45 + 0.55 * np.clip(n @ l, 0, 1)
    rgb = np.array(matplotlib.colors.to_rgb(color))
    return np.clip(rgb[None, :] * k[:, None], 0, 1)


def front_view(base, raised, path):
    """正面図（真正面から。盛り上げた部分を上に重ねる）"""
    fig, ax = plt.subplots(figsize=(10, 6.8), dpi=140)
    for shape, color in ((base, YELLOW), (raised, BLACK)):
        tri = mesh(shape, 0.02)
        top = tri[np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])[:, 2] > 1e-9]
        ax.add_collection(PolyCollection(top[:, :, :2], facecolors=color, edgecolors=color, linewidths=0.05))
    # M5Dial の位置（回るリングと画面）
    cx, cy = mp.DEV
    ax.add_patch(plt.Circle((cx, cy), 24.85, color=DEVICE))
    ax.add_patch(plt.Circle((cx, cy), 16.2, color=SCREEN))
    ax.text(cx, cy, "M5Dial", ha="center", va="center", color="black", fontsize=9)
    ax.set_xlim(-5, mp.W + 5)
    ax.set_ylim(-5, mp.H + 5)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title(f"front  {mp.W:.0f} x {mp.H:.0f} mm  (raised {mp.RAISE} mm)", fontsize=10)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def assembled_parts():
    base, raised = mp.make_plate()
    ring = mp.make_clamp_ring()
    foot = mp.make_foot()
    _, loc = mp.assembly(base, raised, ring, foot)
    dx, dy = mp.DEV
    # M5Dial の仮の形（プレートの座標。フランジの裏がプレートの裏から 0.3 mm 出る）
    fb = -0.3
    dev_rear = loc * Pos(dx, dy, fb - 9) * Cylinder(22.0, 9, align=(Align.CENTER, Align.CENTER, Align.MIN))
    dev_flange = loc * Pos(dx, dy, fb) * Cylinder(25.45, 4.5, align=(Align.CENTER, Align.CENTER, Align.MIN))
    dev_groove = loc * Pos(dx, dy, fb + 4.5) * Cylinder(21.5, 3.6, align=(Align.CENTER, Align.CENTER, Align.MIN))
    dev_ring = loc * Pos(dx, dy, fb + 8.1) * Cylinder(24.85, 12.9, align=(Align.CENTER, Align.CENTER, Align.MIN))
    dev_screen = loc * Pos(dx, dy, fb + 21.0) * Cylinder(16.2, 0.3, align=(Align.CENTER, Align.CENTER, Align.MIN))
    battery = loc * Pos(74, 35, -22) * Box(92, 60, 22, align=(Align.MIN, Align.MIN, Align.MIN))
    parts = [
        (loc * base, YELLOW), (loc * raised, BLACK),
        (loc * Location((dx, dy, fb - mp.RING_T)) * ring, GRAY),
        (dev_rear, DEVICE), (dev_flange, DEVICE), (dev_groove, DEVICE), (dev_ring, DEVICE), (dev_screen, SCREEN),
        (battery, BATTERY),
    ]
    for fx in mp.FOOT_X:
        parts.append((Pos(fx, 0, 0) * foot, GRAY))
    return parts


def view3d(parts, path, elev, azim, title):
    fig = plt.figure(figsize=(10, 7.5), dpi=130)
    ax = fig.add_subplot(projection="3d")
    for shape, color in parts:
        tri = mesh(shape, 0.08)
        ax.add_collection3d(Poly3DCollection(tri, facecolors=shade(tri, color), edgecolor="none"))
    ax.set_xlim(-5, 175)
    ax.set_ylim(-60, 120)
    ax.set_zlim(-20, 160)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()
    ax.set_title(title, fontsize=10)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def main():
    out = mp.OUT
    os.makedirs(out, exist_ok=True)
    base, raised = mp.make_plate()
    front_view(base, raised, os.path.join(out, "preview_front.png"))
    parts = assembled_parts()
    # 斜めの図は matplotlib では前後の重なりが崩れるので、真横だけ描く（立体は docs/plate-design.html のビューアで見る）
    view3d(parts, os.path.join(out, "preview_side.png"), 0, 0, f"side (tilt {mp.TILT:.0f} deg)")
    print("wrote preview_front / side")


if __name__ == "__main__":
    main()
