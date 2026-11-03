# PlatformIO の extra_scripts（call-button の env:release で使う）。ビルドのあと、配布用のファイルを作る。
#
#   .pio/build/<env>/oyobidashi-call-button.bin  ブートローダー・パーティション表・アプリを1つにまとめたもの。
#                                                esptool で 0x0 番地に書き込む。すき間も書くので、本体の設定（NVS）は消える
#   .pio/build/<env>/web/                         ブラウザで書き込むページ用。部品ごとのファイルと parts.json。
#                                                部品ごとに書くので、Erase device をしなければ設定が残る
Import("env")  # noqa: F821  (PlatformIO が用意する)

import json
import os
import shutil

OUT_NAME = "oyobidashi-call-button.bin"


def images(env):
    build_dir = env.subst("$BUILD_DIR")
    result = [(int(env.subst(o), 0), env.subst(p)) for o, p in env.get("FLASH_EXTRA_IMAGES", [])]
    result.append((int(env.subst("$ESP32_APP_OFFSET"), 0), os.path.join(build_dir, env.subst("${PROGNAME}.bin"))))
    return sorted(result)


def make_dist(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    parts = images(env)

    # 1つにまとめたファイル
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    cmd = [
        '"$PYTHONEXE"', '"%s"' % esptool,
        "--chip", env.BoardConfig().get("build.mcu"),
        "merge_bin", "-o", '"%s"' % os.path.join(build_dir, OUT_NAME),
        "--flash_mode", "keep", "--flash_freq", "keep",
        "--flash_size", env.BoardConfig().get("upload.flash_size", "keep"),
    ]
    for offset, path in parts:
        cmd += [hex(offset), '"%s"' % path]
    env.Execute(" ".join(cmd))

    # ブラウザ用の部品
    web = os.path.join(build_dir, "web")
    shutil.rmtree(web, ignore_errors=True)
    os.makedirs(web)
    listed = []
    for offset, path in parts:
        name = os.path.basename(path)
        shutil.copy(path, os.path.join(web, name))
        listed.append({"path": name, "offset": offset})
    with open(os.path.join(web, "parts.json"), "w") as f:
        json.dump(listed, f, indent=2)
    print("dist: %s and web/ (%s)" % (OUT_NAME, ", ".join("%s@0x%x" % (p["path"], p["offset"]) for p in listed)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", make_dist)  # noqa: F821
