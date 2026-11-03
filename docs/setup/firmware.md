# 1. ファームウェアを自分でビルドして書き込む

> 製品カードや画面の文言を変えないなら、ツールを入れずにブラウザで書き込める [かんたんセットアップ](quick-start.md) の方が手軽です。

パソコンに開発ツールを入れて、M5Dial に呼び出しボタンのプログラム（`call-button`）を書き込みます。ここでは、無料のエディタ **VS Code** と、その拡張機能 **PlatformIO** を使います。Windows と macOS のどちらでも同じ流れで進められます。違うところはそれぞれ書き分けています。

## 用意するもの

- パソコン（Windows 10 / 11 または macOS）
- M5Dial と、パソコンにつなぐ USB-C ケーブル。ケーブルは**データ通信ができるもの**を使ってください。充電専用のケーブルだと、パソコンが M5Dial を認識しません
- ブース用スマホのテザリングの名前（SSID）とパスワード。まだ決めていなければ、先に [テザリング設定](tethering.md) を済ませてください

## 1. VS Code と PlatformIO を入れる

1. [VS Code](https://code.visualstudio.com/) をダウンロードしてインストールする
2. VS Code を開き、左の縦に並んだアイコンから **拡張機能**（四角が4つのアイコン）を選ぶ
3. 検索欄に `PlatformIO IDE` と入れて **インストール** を押す
4. インストールが終わったら VS Code を再起動する。左にアリの顔のアイコン（PlatformIO）が出れば完了です

初回は PlatformIO が裏で必要なものをダウンロードするので、数分かかります。

## 2. リポジトリをダウンロードする

GitHub のこのリポジトリのページで、緑の **Code** ボタン → **Download ZIP** を押し、好きな場所に展開します。

git を使える人は `git clone https://github.com/y-morishita-biz/oyobidashi.git` でもかまいません。

## 3. トピック名を決める

ntfy のトピック名は、通知を受け取るための合言葉です。**自分で考えた単語ではなく、ランダムな文字列**にしてください。

作り方の例は次のとおりです。

- Windows：スタートメニューから「PowerShell」を開いて次を実行し、出てきた文字列をそのまま使う
  ```powershell
  "oyobidashi-" + [guid]::NewGuid().ToString("N").Substring(0,16)
  ```
- macOS：「ターミナル」を開いて次を実行し、出てきた文字列をそのまま使う
  ```sh
  echo "oyobidashi-$(openssl rand -hex 8)"
  ```
- パスワードマネージャー（Google パスワードマネージャー、iCloud キーチェーン、1Password など）で英数字16文字以上のパスワードを作り、頭に `oyobidashi-` を付ける

記号は使わず、半角の英数字とハイフンだけにしてください。トピック名を作る Web サイトは使わないでください（作った文字列をサイト側に知られてしまうためです）。

決めたトピック名は、あとで ntfy アプリにも入れます。パスワードマネージャーに保存しておくと楽です。

## 4. secrets.h を作る

Wi-Fi のパスワードとトピック名は、`secrets.h` というファイルにだけ書きます。このファイルは git の管理対象外なので、うっかり公開されることはありません。

> `secrets.h` を作らずに書き込んでもかまいません。その場合、M5Dial は起動すると「設定モード」になり、スマホから設定できます（手順は [かんたんセットアップの 3.](quick-start.md#3-スマホで-m5dial-を設定する)）。設定モードで保存した値は `secrets.h` より優先されます。

1. VS Code の **ファイル → フォルダーを開く** で、展開したリポジトリの中の **`call-button`** フォルダを開く
   - リポジトリ全体ではなく、`call-button` フォルダを開いてください。PlatformIO はこのフォルダの `platformio.ini` を見て動きます
2. 左のファイル一覧で `include/secrets.example.h` を右クリック → **コピー**、`include` フォルダを右クリック → **貼り付け**
3. できたコピーを右クリック → **名前の変更** で `secrets.h` にする
4. `secrets.h` を開き、3か所の `"..."` の中を自分の値に書き換えて保存する

```c
#define WIFI_SSID "your-tethering-ssid"          // ← ブース用スマホのテザリングの名前
#define WIFI_PASS "your-password"                // ← テザリングのパスワード
#define NTFY_TOPIC "oyobidashi-xxxxxxxxxxxxxxxx" // ← 3. で決めたトピック名
```

- 大文字・小文字も含めて、テザリングの設定と完全に同じにしてください
- 返事のやり取りには、トピック名の後ろに `-reply` を付けたトピックを自動で使います。こちらは何もしなくて大丈夫です

## 5. M5Dial に書き込む

1. M5Dial を USB-C ケーブルでパソコンにつなぐ
2. VS Code のいちばん下の青い帯（ステータスバー）にある **→（Upload）** を押す
   - 初回はライブラリのダウンロードとビルドで数分かかります
3. 下のターミナルに `SUCCESS` と出れば書き込み完了です。M5Dial が再起動して、オレンジの「TAP」の画面が出ます

### うまくいかないとき

- `Failed to connect` と出たら、もう一度 → を押す（1回目だけ失敗することがあります）
- 何度やってもつながらないときは、ケーブルを替えてみる（充電専用ケーブルのことが多いです）
- Windows で M5Dial が見つからないときは、デバイスマネージャーの「ポート（COM と LPT）」に M5Dial をつないだときだけ増える項目があるかを確かめる
- それでもだめなら、[M5Dial の公式ドキュメント](https://docs.m5stack.com/ja/core/M5Dial) で書き込みモード（ダウンロードモード）での起動のしかたを確認する

## 6. 動いているか確かめる

この時点で、ブース用スマホのテザリングをオンにしておいてください。

1. M5Dial の画面の一番下に小さな点が出ていなければ、Wi-Fi につながっています（点が出ているあいだは接続中か、つながっていない状態です）
2. 画面に触れると「呼び出しています」→「制作者に届きました」と変わる
3. ntfy アプリの設定（[receiver-ntfy.md](receiver-ntfy.md)）が済んでいれば、持ち歩くスマホに通知が届く

### ログを見たいとき

つながらない原因を調べたいときは、ステータスバーの **コンセントのアイコン（Serial Monitor）** を押すと、M5Dial のログが流れます。

```
[call] wifi connected, ip 192.168.x.x, rssi -40   ← Wi-Fi につながった
[net] listening (since ...)                         ← 返事を待ち受け中
[net] published "ブースで呼ばれています" in 2500 ms   ← 通知を送れた
```

ログにトピック名やパスワードは出ないので、人に見せても大丈夫です。止めるときは、ターミナルの右上のゴミ箱のアイコンで閉じます。

## 自分用に変える

`call-button/src/main.cpp` の先頭に、変えやすい設定がまとまっています。初期値はサンプル（`example.com` と「Product A」などの仮の製品名）です。自分のものに書き換えてから書き込んでください。画面に出る「制作者」という呼び方も、ここで変えられます。変えたときは、フォントを作り直してください（[画面の文言を変えたとき](#画面の文言を変えたとき)）。

| 定数 | 内容 |
|---|---|
| `CALL_TITLE` / `CALL_MESSAGE` など | 通知のタイトルと本文 |
| `ACTIONS` | 通知に付く返事のボタン（最大3つ） |
| `CARDS` / `STORE_URL` | 待っている間にダイヤルで見られる製品カード |
| `QR_URL` / `QR_CAPTION` / `QR_LABEL` | 待たせているときに画面に出す QR コードの行き先と、その下の文字（設定モードで入れた値があれば、そちらが優先） |
| `QR_AFTER_MS` など | QR を出すまで・再通知・お待たせまでの時間（ミリ秒） |
| `TZ_JST` / `NTP_SERVER1` | 時刻のタイムゾーンと NTP サーバー |

## 画面の文言を変えたとき

画面の日本語は、使う文字だけを Noto Sans JP から取り出したフォント（`src/fonts_noto.h`）で描いています。新しい文字を使うと画面に表示されないので、フォントを作り直してください。

少し手間がかかるので、文言を変えないなら飛ばしてかまいません。

1. [Google Fonts の Noto Sans JP](https://fonts.google.com/noto/specimen/Noto+Sans+JP) をダウンロードして展開する。中の `static` フォルダに `NotoSansJP-Bold.ttf` と `NotoSansJP-Regular.ttf` があります。見つからないときは、ファイル名に Bold と Regular が付いたものを使い、下のコマンドのファイル名をそれに合わせる
2. その2つのファイルを、リポジトリの `call-button` フォルダにコピーする。ここに置けば、下のコマンドをそのまま使えます。このフォントのファイルはコミットしないでください
3. VS Code のステータスバーの **ターミナルのアイコン（PlatformIO: New Terminal）** を押す
4. 開いたターミナルで次を実行する

**Windows（PowerShell）**

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pip.exe" install freetype-py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" ..\tools\make_vlw.py --src src/main.cpp --out src/fonts_noto.h `
  --font NOTO_BOLD_20=NotoSansJP-Bold.ttf:20 --font NOTO_BOLD_15=NotoSansJP-Bold.ttf:15 `
  --font NOTO_REGULAR_13=NotoSansJP-Regular.ttf:13 --font NOTO_REGULAR_11=NotoSansJP-Regular.ttf:11
```

**macOS**

```sh
~/.platformio/penv/bin/pip install freetype-py
~/.platformio/penv/bin/python ../tools/make_vlw.py --src src/main.cpp --out src/fonts_noto.h \
  --font NOTO_BOLD_20=NotoSansJP-Bold.ttf:20 --font NOTO_BOLD_15=NotoSansJP-Bold.ttf:15 \
  --font NOTO_REGULAR_13=NotoSansJP-Regular.ttf:13 --font NOTO_REGULAR_11=NotoSansJP-Regular.ttf:11
```

5. もう一度 → で書き込む

## 改造して git で管理する人へ

`secrets.h` をうっかりコミットしないように、コミット前に止めてくれるフックが入っています。リポジトリのフォルダで一度だけ次を実行すると有効になります。

```sh
git config core.hooksPath tools/git-hooks
```

## battery-test

`battery-test/` は、モバイルバッテリーが「電流が小さい」と判断して勝手に出力を止めないかを確かめるためのテスト用プログラムです。手持ちのバッテリーで不安があるときに、同じ手順（`battery-test` フォルダを開いて → で書き込み）で使えます。詳しくは [battery-test/README.md](../../battery-test/README.md) を見てください。
