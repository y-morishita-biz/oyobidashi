#!/usr/bin/env python3
# M5Dial のシリアルログを決まった秒数だけ読んで終了する（pio device monitor は対話的で止まらないため）。
#
#   ~/.platformio/penv/bin/python tools/serial_log.py            # 70秒読む
#   ~/.platformio/penv/bin/python tools/serial_log.py 30 --reset # リセットして起動ログから30秒読む
#   ~/.platformio/penv/bin/python tools/serial_log.py --port /dev/cu.usbmodemXXXX
#
# ポートを省略すると、ESP32-S3 の USB（VID 303A）を自動で探す。
import argparse
import sys
import time

import serial
from serial.tools import list_ports


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x303A:
            return p.device
    sys.exit("M5Dial が見つかりません（pio device list で確認してください）")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seconds", nargs="?", type=float, default=70)
    ap.add_argument("--port")
    ap.add_argument("--reset", action="store_true", help="開いた直後に M5Dial をリセットする")
    args = ap.parse_args()

    s = serial.Serial()
    s.port = args.port or find_port()
    s.baudrate = 115200
    s.timeout = 0.5
    # DTR/RTS を下げた状態で開く（ESP32-S3 の USB では RTS が EN＝リセットにつながっている）
    s.dtr = False
    s.rts = False
    s.open()
    if args.reset:
        s.rts = True
        time.sleep(0.1)
        s.rts = False

    t0 = time.time()
    while time.time() - t0 < args.seconds:
        data = s.read(1024)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()


if __name__ == "__main__":
    main()
