#!/usr/bin/env bash
# 10曲を video2frames.py で一括変換する使い捨てバッチ（非コミット運用の素材生成）。
# PSRAM 上限(16kHz mono で約230秒)を超える曲だけ 8000Hz に落としてある。
set -u
cd "$(dirname "$0")/.." || exit 1

# name url samplerate の3つ組
songs=(
  "aaa|https://youtu.be/52_RSSOK2xI|16000"
  "raise|https://youtu.be/DO-DkrNVyjg|16000"
  "ruri|https://youtu.be/YDOARwO2SNk|16000"
  "dontsaylazy|https://youtu.be/5CSNv9MNEC4|8000"
  "gogomaniac|https://youtu.be/IwYZTBzbsYs|8000"
  "natsukage|https://youtu.be/Zx8RT4PoYYs|16000"
  "itsame|https://youtu.be/hJHJP3iskJo|8000"
  "noudou|https://youtu.be/NTKwzRAdY7w|16000"
  "aporia|https://youtu.be/fhTFysCtF6g|8000"
  "passeul|https://youtu.be/LxX_bfWuAzc|8000"
)

fail=0
for s in "${songs[@]}"; do
  IFS='|' read -r name url sr <<< "$s"
  echo "=================================================="
  echo "[convert] $name ($url) sample-rate=$sr"
  echo "=================================================="
  if python3 tools/video2frames.py "$url" --name "$name" --sample-rate "$sr"; then
    echo "[ok] $name"
  else
    echo "[FAIL] $name"
    fail=$((fail+1))
  fi
done

echo "=================================================="
echo "[done] 失敗 $fail 件"
