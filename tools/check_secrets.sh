#!/usr/bin/env bash
# 秘密情報がリポジトリに入っていないかを確かめる。
#
#   tools/check_secrets.sh            # git で管理しているファイルをすべて調べる（CI 用）
#   tools/check_secrets.sh --staged   # コミットしようとしているファイルだけを調べる（pre-commit 用）
#
# 調べること
#   1. secrets.h がコミット対象に入っていない
#   2. WIFI_SSID / WIFI_PASS / NTFY_TOPIC にダミー以外の値が書かれていない
#   3. 秘密鍵（BEGIN ... PRIVATE KEY）が入っていない
#   4. ntfy.sh の URL に本物らしきトピック名が直接書かれていない
set -euo pipefail

mode="${1:-all}"
cd "$(git rev-parse --show-toplevel)"

if [ "$mode" = "--staged" ]; then
  files=$(git diff --cached --name-only --diff-filter=ACMR)
  read_file() { git show ":$1" 2>/dev/null || true; }
else
  files=$(git ls-files)
  read_file() { cat -- "$1" 2>/dev/null || true; }
fi

# secrets.example.h や README で使ってよいダミーの値
allowed='^(|your-tethering-ssid|your-password|oyobidashi-x+)$'

fail=0
report() { echo "✗ $1" >&2; fail=1; }

while IFS= read -r f; do
  [ -z "$f" ] && continue
  case "$f" in
    */secrets.h|secrets.h) report "$f: secrets.h はコミットしないでください（.gitignore 済みのはずです）"; continue ;;
  esac
  # 大きな生成物・バイナリは飛ばす
  case "$f" in
    *.png|*.jpg|*.stl|*.step|*.vlw|*/fonts_noto.h|docs/plate-model.js) continue ;;
  esac
  content=$(read_file "$f")
  [ -z "$content" ] && continue

  while IFS= read -r line; do
    [ -z "$line" ] && continue
    key=$(sed -E 's/.*#define[[:space:]]+([A-Z_]+).*/\1/' <<<"$line")
    val=$(sed -E 's/.*"([^"]*)".*/\1/' <<<"$line")
    if ! [[ "$val" =~ $allowed ]]; then
      report "$f: $key にダミー以外の値が書かれています（secrets.h に移してください）"
    fi
  done < <(grep -E '#define[[:space:]]+(WIFI_SSID|WIFI_PASS|NTFY_TOPIC)[[:space:]]+"' <<<"$content" || true)

  if grep -qE -- '-----BEGIN [A-Z ]*PRIVATE KEY-----' <<<"$content"; then
    report "$f: 秘密鍵が含まれています"
  fi
  if grep -qE 'ntfy\.sh/oyobidashi-[0-9A-Za-z]*[0-9][0-9A-Za-z]{6,}' <<<"$content"; then
    report "$f: ntfy のトピック名が直接書かれているようです"
  fi
done <<<"$files"

if [ "$fail" -ne 0 ]; then
  echo "秘密情報のチェックに引っかかりました。SECURITY.md を確認してください。" >&2
  exit 1
fi
echo "✓ 秘密情報は見つかりませんでした"
