# oyobidashi

展示会ブース用の呼び出しボタン。来場者がプレートに埋め込んだ M5Dial の画面に触れると、ntfy 経由で持ち歩いている iPhone に通知を送る（BIG キースイッチは使わない方針になった）。
経緯・決定事項・次のタスクは **docs/handoff.md** にまとめてある。作業を始める前に必ず読むこと。

## ユーザーについて

- モリシタヨウスケ（UI デザイナー）。実装は Claude Code に任せている
- やり取りは日本語。専門用語は噛み砕いて説明し、次にユーザーがやること（実機で押す、バッテリーにつなぐ等）をはっきり伝える
- 画面のデザインに関わる判断（レイアウト・色・文言）は、勝手に確定させずに提案して確認をとる

## 方針

- **無料で構築する**。有料サービスやハードの追加購入が必要になりそうなら、実装する前に相談する
- 通知は ntfy（ntfy.sh の公開サーバー）。回線はブースに置いた Android のテザリング（2.4GHz）
- 秘密情報（Wi-Fi の SSID・パスワード、ntfy のトピック名）は各プロジェクトの `include/secrets.h` に書き、コミットしない。`secrets.example.h` を用意する
- リポジトリは公開する前提。コミットの前に `tools/check_secrets.sh --staged` が通ることを確かめる（SECURITY.md）。ログにトピック名やパスワードを出さない

## リポジトリ構成

```
battery-test/   モバイルバッテリーの自動オフを確かめるテスト用ファームウェア（PlatformIO）
call-button/    呼び出しボタン本体のファームウェア
docs/           引き継ぎ資料・UX 仕様（ux-proposal.html）・プレートの設計（plate-design.html）・設定手順（setup/）・ブラウザ書き込みページ（flasher/）
plate/          M5Dial を埋め込むプレートの 3D モデル（build123d で STEP / STL を書き出す）
tools/          開発用スクリプト（シリアルログの読み取り・秘密情報チェックなど）
```

ファームウェアは1つのプロジェクトにつき1フォルダ（それぞれに platformio.ini を置く）。
call-button の配布用は `pio run -e release`（secrets.h を読まない）。リリースは GitHub の Actions → firmware → Run workflow（main、version に v1.0.0 など）。`.github/workflows/firmware.yml` がタグ・プレリリース・GitHub Pages の書き込みページを作る（この環境からはタグをプッシュできない）。

## ハードウェア

- **M5Dial**：中身は M5StampS3（ESP32-S3、PSRAM なし、フラッシュ 8MB）。PlatformIO の board は `m5stack-stamps3`
  - 液晶：丸型 240×240（GC9A01）。描画は `M5Canvas` で 240×240 のスプライトを作り、まとめて push する
  - ダイヤル：エンコーダーは G40 / G41。`M5Dial.begin(cfg, true, false)` で有効にし、`M5Dial.Encoder.read()` で読む
  - 画面のボタン：`M5Dial.BtnA`（M5Unified の Button_Class）
  - PORT.A = G13 / G15（I2C）、PORT.B = G1 / G2（今は使っていない）
  - タッチ（FT3267）は `M5.Touch`、ブザーは `M5Dial.Speaker`。背面はネジ（Ø44）で、付属のリングでパネルに挟んで留められる
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

## 画面デザインの基本

- call-button の画面・音・通知の仕様は **docs/ux-proposal.html**（実機と同じ動きのプロトタイプ付き）。変えるときは、まずそこを読む
- 色：オレンジ #FF5A1F = 来場者が触れる番、黒 = 待つ番、白 = QR を見る番。補助の文字はグレー #8C8C8C
- 画面の文言は日本語。装飾やタイトルで英語の方がかっこいい箇所（TAP、CALLING などのドット文字）は英語
- 日本語は Noto Sans JP のなめらかフォント（`tools/make_vlw.py` で使う文字だけを取り出す。文言を変えたら作り直す）
- 丸い画面なので、上下の端に近いほど横幅が狭くなることに注意する
