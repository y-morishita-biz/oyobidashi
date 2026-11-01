# 1. ファームウェアを書き込む

M5Dial に `call-button` を書き込みます。macOS で確認した手順です（Windows / Linux でも PlatformIO が動けば同じ流れです）。

## 1. PlatformIO を入れる

どちらか一方で大丈夫です。

- **VS Code を使う**：VS Code の拡張機能「PlatformIO IDE」を入れる。以降の `pio` コマンドは、VS Code の PlatformIO のターミナルで実行するか、左のメニューの Build / Upload ボタンで代わりにできます
- **ターミナルだけで使う**：[PlatformIO Core のインストール手順](https://docs.platformio.org/en/latest/core/installation/index.html)に従う。`pio` が PATH に無いときは `~/.platformio/penv/bin/pio` を使います

## 2. リポジトリを手元に持ってくる

```sh
git clone https://github.com/y-morishita-biz/oyobidashi.git
cd oyobidashi
```

## 3. 秘密情報（secrets.h）を作る

### トピック名を決める

ntfy のトピック名は合言葉の代わりです。**自分で考えた単語ではなく、ランダムな文字列**にしてください。ターミナルで次を実行すると作れます。

```sh
echo "oyobidashi-$(openssl rand -hex 8)"
# 例: oyobidashi-3f9c0a7b12d4e865（これをそのまま使わないこと）
```

### secrets.h を書く

```sh
cp call-button/include/secrets.example.h call-button/include/secrets.h
```

`call-button/include/secrets.h` を開いて、3つの値を入れます。

```c
#define WIFI_SSID "your-tethering-ssid"          // ← ブース用スマホのテザリング名
#define WIFI_PASS "your-password"                // ← テザリングのパスワード
#define NTFY_TOPIC "oyobidashi-xxxxxxxxxxxxxxxx" // ← 上で作ったトピック名
```

- SSID とパスワードは、次の [テザリング設定](tethering.md) で決めるものと同じにします。先にテザリング側を決めてもかまいません
- 返事のやり取りには、`NTFY_TOPIC` の後ろに `-reply` を付けたトピックを自動で使います。設定は要りません
- `secrets.h` は `.gitignore` に入っているので、普通に `git add .` してもコミットされません。`git status` に `secrets.h` が出てこないことを確認してください

## 4. うっかりコミットを防ぐ（任意）

このリポジトリには、`secrets.h` や本物のパスワードらしき文字列をコミットしようとしたときに止める pre-commit フックが入っています。一度だけ次を実行すると有効になります。

```sh
git config core.hooksPath tools/git-hooks
```

## 5. ビルドして書き込む

M5Dial を USB-C でパソコンにつないでから実行します。

```sh
cd call-button
pio run -t upload
```

- 1回目に「Failed to connect」と出ることがあります。もう一度実行すると通ることが多いです
- それでもつながらないときは `pio device list` で M5Dial が見えているか確かめてください

## 6. 動いているか確かめる

シリアルログを読むと、Wi-Fi につながったか・通知を送れたかが分かります。

```sh
# pyserial が必要（PlatformIO の Python には入っています）
~/.platformio/penv/bin/python ../tools/serial_log.py 30 --reset
```

次のような行が出れば成功です。

```
[call] wifi connected, ip 192.168.x.x, rssi -40
[net] listening (since ...)
```

画面に触れて `[net] published "ブースで呼ばれています"` と出れば、通知が送れています。ntfy アプリ側の設定は [receiver-ntfy.md](receiver-ntfy.md) を見てください。

> ログにトピック名やパスワードは出しません。ログを人に見せても大丈夫です。

## 画面の文言を変えたとき

画面の日本語は、使う文字だけを Noto Sans JP から取り出したフォント（`src/fonts_noto.h`）で描いています。`src/main.cpp` の文言を変えて新しい文字が増えたら、フォントを作り直してください。

1. [Google Fonts の Noto Sans JP](https://fonts.google.com/noto/specimen/Noto+Sans+JP) をダウンロードし、Bold と Regular を入れる
2. `freetype-py` を入れる：`~/.platformio/penv/bin/pip install freetype-py`
3. `call-button` フォルダで実行する（フォントのパスは自分の環境に合わせる）

```sh
~/.platformio/penv/bin/python ../tools/make_vlw.py --src src/main.cpp --out src/fonts_noto.h \
  --font NOTO_BOLD_20=~/Library/Fonts/NotoSansJP-Bold.otf:20 \
  --font NOTO_BOLD_15=~/Library/Fonts/NotoSansJP-Bold.otf:15 \
  --font NOTO_REGULAR_13=~/Library/Fonts/NotoSansJP-Regular.otf:13 \
  --font NOTO_REGULAR_11=~/Library/Fonts/NotoSansJP-Regular.otf:11
```

## 自分用に変えるところ

`call-button/src/main.cpp` の先頭にまとまっています。

| 定数 | 内容 |
|---|---|
| `CALL_TITLE` / `CALL_MESSAGE` など | 通知のタイトルと本文 |
| `ACTIONS` | 通知に付く返事のボタン（最大3つ） |
| `CARDS` / `STORE_URL` | 待っている間にダイヤルで見られる製品カード |
| `QR_URL` | 待たせているときに画面に出す QR コードの行き先 |
| `QR_AFTER_MS` などのタイミング | QR を出すまで・再通知・お待たせまでの時間 |
| `TZ_JST` / `NTP_SERVER1` | 時刻のタイムゾーンと NTP サーバー |

初期値は作者のブース用（X の @penomo、dotting dots のショップ）なので、自分のものに書き換えてください。

## battery-test について

`battery-test/` は、モバイルバッテリーが「電流が小さい」と判断して勝手に出力を止めないかを確かめるためのテスト用ファームウェアです。手持ちのバッテリーで不安があるときに使ってください。手順は [battery-test/README.md](../../battery-test/README.md) にあります。
