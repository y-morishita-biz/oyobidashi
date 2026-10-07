# call-button

展示会ブースの呼び出しボタン本体のファームウェア。来場者が BIG スイッチ(または M5Dial の画面)を押すと、ntfy.sh 経由で担当者の iPhone に通知を送る。

## 準備

1. `include/secrets.example.h` を `include/secrets.h` にコピーして、次の3つを入れる
   - `WIFI_SSID` / `WIFI_PASS`:ブースに置く Android のテザリング(2.4GHz)
   - `NTFY_TOPIC`:通知の合言葉になるトピック名。`oyobidashi-` のあとにランダムな16文字
2. M5Dial を USB でつないで書き込む(`pio run -t upload`)

## iPhone 側の設定

1. App Store で [ntfy](https://apps.apple.com/jp/app/ntfy/id1625396347) を入れる
2. アプリを開き、右上の「+」からトピックを購読する。トピック名に `secrets.h` の `NTFY_TOPIC` を入れる。「Use another server」はオフのまま(ntfy.sh を使う)
3. 通知を許可する

## 動き

| 状態 | 画面 | 次に進む条件 |
|---|---|---|
| 待機 | 押してください | 押される |
| 送信中 | 呼び出し中… | 送信が終わる |
| 呼び出し済み | 呼び出しました | 60秒たつ(その間は押しても再送しない) |
| 送信失敗 | 通信できませんでした | もう一度押すと再送。15秒たつと待機に戻る |

- Wi-Fi が切れると画面の下に「Wi-Fi 未接続」と出る。自動で接続し直す
- 通知の文言は `src/main.cpp` の先頭(`NOTIFY_TITLE` / `NOTIFY_MESSAGE`)にある。本文の後ろには押された時刻が付く(例:「話を聞きたい人がいます！(10:42)」)。時刻は Wi-Fi につながったときにインターネット(NTP)から合わせ、まだ合っていなければ時刻なしで送る
- 画面のレイアウトと文言は仮。デザインが決まったら作り直す

## 配線

BIG スイッチは PORT.B の **G1** と **GND** の間につなぐ(押すと G1 が GND に落ちる)。
