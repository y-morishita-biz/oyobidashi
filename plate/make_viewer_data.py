#!/usr/bin/env python3
# 組み立てた状態の形を、docs/plate-design.html の 3D ビューア用のデータ（docs/plate-model.js）に書き出す。
#
#   ~/.platformio/penv/bin/python plate/make_viewer_data.py
import base64
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_preview as rp  # noqa: E402

# 部品の名前（ビューアで色を替えたり隠したりするのに使う。文字コードの影響を受けないよう英字）
NAMES = ["plate", "raised", "ring", "device", "device", "device", "device", "screen", "battery", "foot", "foot"]


def main():
    parts = rp.assembled_parts()
    out = []
    for (shape, color), name in zip(parts, NAMES):
        verts, tris = shape.tessellate(0.08, 0.25)
        # world（X 右、Y 奥、Z 上）→ three.js（x 右、y 上、z 手前）
        v = np.array([(p.X, p.Z, -p.Y) for p in verts], dtype=np.float32)
        idx = np.array(tris, dtype=np.uint32).reshape(-1)
        out.append({
            "name": name, "color": color,
            "pos": base64.b64encode(v.tobytes()).decode(),
            "idx": base64.b64encode(idx.tobytes()).decode(),
        })
        print(f"{name:8s} {len(tris):7d} tris")
    path = os.path.join(os.path.dirname(rp.mp.HERE), "docs", "plate-model.js")
    with open(path, "w") as f:
        f.write("// plate/make_viewer_data.py で生成。手で編集しない\nwindow.PLATE_MODEL = ")
        json.dump(out, f, ensure_ascii=False)
        f.write(";\n")
    print("->", os.path.relpath(path), os.path.getsize(path) // 1024, "KB")


if __name__ == "__main__":
    main()
