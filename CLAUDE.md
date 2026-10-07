# oyobidashi

展示会ブース用の呼び出しボタン。来場者が BIG キースイッチを押すと、M5Dial が ntfy 経由で持ち歩いている iPhone に通知を送る。
経緯・決定事項・次のタスクは **docs/handoff.md** にまとめてある。作業を始める前に必ず読むこと。

## ユーザーについて

- 森下陽介（UI デザイナー）。実装は Claude Code に任せている
- やり取りは日本語。専門用語は噛み砕いて説明し、次にユーザーがやること（実機で押す、バッテリーにつなぐ等）をはっきり伝える
- 画面のデザインに関わる判断（レイアウト・色・文言）は、勝手に確定させずに提案して確認をとる

## 方針

- **無料で構築する**。有料サービスやハードの追加購入が必要になりそうなら、実装する前に相談する
- 通知は ntfy（ntfy.sh の公開サーバー）。回線はブースに置いた Android のテザリング（2.4GHz）
- 秘密情報（Wi-Fi の SSID・パスワード、ntfy のトピック名）は各プロジェクトの `include/secrets.h` に書き、コミットしない。`secrets.example.h` を用意する

## リポジトリ構成

```
battery-test/   モバイルバッテリーの自動オフを確かめるテスト用ファームウェア（PlatformIO）
call-button/    呼び出しボタン本体のファームウェア
docs/           引き継ぎ資料・UX 仕様（ux-proposal.html）・プレートの設計（plate-design.html）
plate/          M5Dial を埋め込むプレートの 3D モデル（build123d で STEP / STL を書き出す）
tools/          開発用スクリプト（シリアルログの読み取りなど）
```

ファームウェアは1つのプロジェクトにつき1フォルダ（それぞれに platformio.ini を置く）。

## ハードウェア

- **M5Dial**：中身は M5StampS3（ESP32-S3、PSRAM なし、フラッシュ 8MB）。PlatformIO の board は `m5stack-stamps3`
  - 液晶：丸型 240×240（GC9A01）。描画は `M5Canvas` で 240×240 のスプライトを作り、まとめて push する
  - ダイヤル：エンコーダーは G40 / G41。`M5Dial.begin(cfg, true, false)` で有効にし、`M5Dial.Encoder.read()` で読む
  - 画面のボタン：`M5Dial.BtnA`（M5Unified の Button_Class）
  - PORT.A = G13 / G15（I2C）、**PORT.B = G1 / G2**（BIG スイッチはこちらにつなぐ予定）
  - Wi-Fi は 2.4GHz のみ
- ライブラリ：`m5stack/M5Dial`、`m5stack/M5Unified`、`m5stack/M5GFX`

## よく使うコマンド

PlatformIO は `~/.platformio/penv/bin/pio` にある（PATH に `pio` がなければこちらを使う）。

```sh
cd battery-test                     # または call-button
pio run                             # ビルド
pio run -t upload                   # M5Dial（USB 接続）に書き込み
~/.platformio/penv/bin/python ../tools/serial_log.py 70 --reset   # シリアルログ（115200）を70秒読む
```

- `pio device monitor` は対話的で終わらず、macOS には `timeout` コマンドも無いので、ログは `tools/serial_log.py` で読む（`--reset` で再起動して起動ログから読む）
- M5Unified 0.2.x は `Serial.begin()` を自動で呼ばない。`cfg.serial_baudrate = 115200;` を `M5Dial.begin()` の前に設定しないと、シリアルに何も出ない

- 書き込みのポートが見つからないときは、`pio device list` で確認する。それでもだめなら、M5Dial を書き込みモード（ダウンロードモード）で起動し直す必要がある。手順を M5Stack の公式ドキュメントで確かめてから、ユーザーに操作を頼む
- 書き込んだら、シリアルログで起動を確認してからユーザーに報告する

## 画面デザインの基本（battery-test で採用したもの）

- 黒背景・白文字・グレーの補助文字（#8C8C8C）・アクセント色 #FF5A1F
- フォントは M5GFX 内蔵の FreeSans 系（`fonts::FreeSansBold24pt7b` など）。画面の表示文字は英語、コメントやドキュメントは日本語
- 丸い画面なので、上下の端に近いほど横幅が狭くなることに注意する
