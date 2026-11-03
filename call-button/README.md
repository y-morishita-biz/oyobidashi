# call-button

展示会ブースの呼び出しボタン本体のファームウェア。来場者が M5Dial の画面に触れると、ntfy.sh 経由で制作者の iPhone に通知を送る。制作者が通知のボタンで返事をすると、M5Dial の画面に「向かっています」「12:15 ごろ戻ります」と出る。

画面・音・通知・ダイヤル操作の設計は [docs/ux-proposal.html](../docs/ux-proposal.html) にまとめてある(実機と同じ動きのプロトタイプ付き)。

## 準備

配布しているファームウェア（リリース）を使うなら、ビルドは要らない。手順は [docs/setup/quick-start.md](../docs/setup/quick-start.md)。

自分でビルドするとき：

1. `include/secrets.example.h` を `include/secrets.h` にコピーして、次の3つを入れる（作らなくてもよい。その場合は起動すると設定モードになる）
   - `WIFI_SSID` / `WIFI_PASS`:ブースに置く Android のテザリング(2.4GHz)
   - `NTFY_TOPIC`:通知の合言葉になるトピック名。`oyobidashi-` のあとにランダムな16文字
   - 返事は `NTFY_TOPIC` の後ろに `-reply` を付けたトピックでやり取りする(自動)
2. M5Dial を USB でつないで書き込む(`pio run -t upload`)

## 設定モード

**実機未検証のトライアル機能**(ビルドが通ることだけ確認済み)。

設定がないとき、または画面を押したまま電源を入れたときに入る(`src/portal.cpp`)。

1. M5Dial が Wi-Fi の親機 `oyobidashi-XXXX` になり、白地に接続用の QR とパスワードを出す
2. スマホがつながると、黒地に「スマホの画面で設定を続けてください」。スマホには設定の画面(captive portal、`http://192.168.4.1/`)が開く
3. SSID・パスワード・トピック名(ランダムな初期値入り)・QR の行き先と下の文字(任意)を入れて保存すると、本体の NVS(名前空間 `config`)に書いて再起動する
4. 設定済みで入ったときは、3秒長押しで何も変えずに再起動する

読む順番は NVS → `secrets.h` → `main.cpp` の初期値。

## ビルドの種類

| env | 用途 |
|---|---|
| `m5dial` | ふだんの開発。`secrets.h` があれば初期値として使う。製品カードと QR のサンプル入り |
| `release` | 配布用(GitHub のリリース)。`secrets.h` を読まない。製品カードなし・QR の初期値なし。`tools/merge_bin.py` が `.pio/build/release/` に1つにまとめた bin と、ブラウザ書き込み用の部品(`web/`)を作る |

リリースは GitHub の Actions → firmware → Run workflow で、main を選び version に `v1.0.0` などを入れて実行する。`.github/workflows/firmware.yml` がタグとリリース(プレリリース)を作り、書き込みページを GitHub Pages に公開する(Pages の公開先は main からしか受け付けないので、タグのプッシュではなく main からの実行にしている)。

## iPhone 側の設定

詳しい手順（Android・集中モード・テザリングの設定を含む）は [docs/setup/](../docs/setup/README.md) にあります。


1. App Store で [ntfy](https://apps.apple.com/jp/app/ntfy/id1625396347) を入れる
2. アプリを開き、右上の「+」からトピックを購読する。トピック名に `secrets.h` の `NTFY_TOPIC` を入れる。「Use another server」はオフのまま(ntfy.sh を使う)
3. 通知を許可する
4. 通知を長押しすると、返事のボタン(向かいます / 5分で戻ります / 30分で戻ります)が出る。通知をタップしても返事にはならない
5. Mac で iPhone の通知を受け取る設定(iPhone ミラーリング)を使っているなら、設定 → 通知 → ntfy の「Mac に表示」をオフにする。オンのままだと、iPhone がロック中で近くの Mac を使っているときに、通知が Mac にだけ出て iPhone の画面がつかない(振動だけになる)

## 操作

| 誰が | 操作 | 起きること |
|---|---|---|
| 来場者 | 画面に触れる(タッチでも押し込みでも) | 制作者を呼ぶ |
| 来場者 | 待機中にダイヤルを回す | 外周の光がついてくる |
| 来場者 | 待っている間にダイヤルを回す | 製品カードがめくれる(8秒で元の画面へ) |
| 制作者 | 通知のボタン | 向かいます / 5分で戻ります / 30分で戻ります |
| 制作者 | 画面に触れたまま、ダイヤルを1段回して離す | 担当者メニュー:閉じる / 在席にする / 離席にする / 15・30・60分で戻る / 音量 |
| 制作者 | 呼び出しのあと・不在中に、画面を3秒長押し | 待機に戻す |

## 状態

| 状態 | 画面 | 次に進む条件 |
|---|---|---|
| 待機 | TAP / 制作者と話してみたい！ | 触れる |
| 呼び出し中 | CALLING / 呼び出しています | 送れた(失敗したら5秒・15秒後にやり直し、3回だめなら失敗) |
| 届いた | SENT / 制作者に届きました。1分後に白地で X の QR | 2分で再通知、5分でお待たせへ |
| 向かっています | COMING / 制作者が向かっています | 5分で待機へ |
| 戻り時刻 | 白地に 12:15 / ごろ戻ります / QR | その時刻で待機へ。触れると制作者に目立たない通知(3分に1回まで) |
| お待たせ | 白地にお待たせしています / QR | 2分で待機へ。触れると新しく呼ぶ |
| 失敗 | RETRY / うまく届きませんでした | 触れると呼び直す。30秒で待機へ |
| 在席 | HELLO / 制作者がいます | 触れても通知しない。60分で待機へ |

- Wi-Fi が切れていると、画面の一番下に小さな点が出る。自動で接続し直す
- 呼び出し中に何度触れても、通知は増えない
- 音量は担当者メニューで変えられ、本体に保存される

## 変えたいとき

- 通知の文言、返事のボタン、製品カード、QR の行き先、時間の設定:`src/main.cpp` の先頭
- 画面の文言を変えたら、フォントを作り直す(画面で使う文字だけを Noto Sans JP から取り出している)

```sh
~/.platformio/penv/bin/python ../tools/make_vlw.py --src src/main.cpp --out src/fonts_noto.h \
  --font NOTO_BOLD_20=~/Library/Fonts/NotoSansJP-Bold.otf:20 \
  --font NOTO_BOLD_15=~/Library/Fonts/NotoSansJP-Bold.otf:15 \
  --font NOTO_REGULAR_13=~/Library/Fonts/NotoSansJP-Regular.otf:13 \
  --font NOTO_REGULAR_11=~/Library/Fonts/NotoSansJP-Regular.otf:11
```

`make_vlw.py` には freetype-py が要る(`~/.platformio/penv/bin/pip install freetype-py`)。
